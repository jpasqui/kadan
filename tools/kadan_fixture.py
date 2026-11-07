"""Keep an imported Kadan project once and give every test its own copy of it in seconds.

A cold import of Cave000 takes over 20 minutes. Most checks only need the imported project, not the
import, so the import runs once and its project is kept as a fixture. A copy copies the fixture's files
(Cave000's Content is about 150 MB), which takes a few seconds. Copies used to hard-link Content, but
a hard link shares its read-only mark with every other link: when one copy was made again, the marks put
back on the fixture also came back on files another copy's editor had unlocked to save, and that save's
delete failed. Every copy and every check compares the fixture's files with the sizes and times
recorded when it was kept, and refuses a fixture that changed.

    py -3.11 tools/kadan_fixture.py keep NAME PROJECT_FOLDER [--note TEXT]
    py -3.11 tools/kadan_fixture.py copy NAME FOLDER [--plugin BUILD_HOST]
    py -3.11 tools/kadan_fixture.py check NAME
    py -3.11 tools/kadan_fixture.py list
    py -3.11 tools/kadan_fixture.py reimport FOLDER PACKAGE... [--paks DIR] [--timeout S] [--d3d11]

keep      records PROJECT_FOLDER (an imported project, left as it is) as fixture NAME. Its Content is
          hard-linked into private/kadan-fixtures/NAME, so the folder it came from can be changed or
          removed later without changing the fixture. Saved, Intermediate and DerivedDataCache are
          left out.
copy      makes FOLDER a working copy of fixture NAME. A FOLDER made by an earlier copy is replaced.
          --plugin puts the Kadan built in a dev_build.py host into the copy.
check     says whether fixture NAME still has the files it was kept with.
reimport  imports only the named /Game packages into a working copy, through the same planner as
          the Import page: their files are removed from the copy first, and the packages they need
          that are already in the copy are not imported again. For checking an importer change on
          the assets it affects without a whole map import. It runs with no renderer, so materials
          compile no shaders; --d3d11 gives it one (over ssh, run it in the logged-in session).
"""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
STORE = REPO / 'private/kadan-fixtures'
COPY_MARKER = 'kadan-copy.json'
LINKED = ('Content',)                       # hard-linked: only replaced by Unreal's saves, never written in place
COPIED = ('Config', 'Plugins')              # copied: the test tools write into these
LEFT_OUT = {'Saved', 'Intermediate', 'DerivedDataCache', 'Binaries', 'Build'}
PACKAGE_FILES = ('.uasset', '.umap', '.uexp', '.ubulk', '.uptnl')


def stamp(path):
    info = path.stat()
    return [info.st_size, info.st_mtime_ns]


def files_under(folder):
    return sorted(p for p in folder.rglob('*') if p.is_file())


def read_only(path):
    return not os.stat(path).st_mode & stat.S_IWRITE


def remove_file(path):
    """Kadan marks the game's packages read-only, and a hard link shares that mark with the fixture's
    file. The mark is cleared to remove the link and put back on the fixture by restore_marks."""
    os.chmod(path, stat.S_IWRITE)
    os.unlink(path)


def restore_marks(name):
    folder, record = record_of(name)
    for relative in record.get('read_only', []):
        path = folder / relative
        if path.is_file() and not read_only(path):
            os.chmod(path, stat.S_IREAD)


def fixture_folder(name):
    if not name or any(c in name for c in '/\\:.') or name != name.strip():
        raise SystemExit('a fixture name is letters, digits, dash and underscore: ' + repr(name))
    return STORE / name


def project_file(folder):
    found = sorted(Path(folder).glob('*.uproject'))
    if len(found) != 1:
        raise SystemExit('expected one .uproject in ' + str(folder))
    return found[0]


def keep(args):
    source = Path(args.project).resolve()
    if source.suffix == '.uproject':
        source = source.parent
    project_file(source)
    target = fixture_folder(args.name)
    if target.exists():
        raise SystemExit('fixture {} exists; pick another name or remove {}'.format(args.name, target))
    start = time.time()
    staging = target.with_name(target.name + '.making')
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    manifest = {}
    for item in sorted(source.iterdir()):
        if item.name in LEFT_OUT:
            continue
        if item.is_file():
            shutil.copy2(item, staging / item.name)
        elif item.name in LINKED:
            for path in files_under(item):
                relative = path.relative_to(source)
                (staging / relative).parent.mkdir(parents=True, exist_ok=True)
                os.link(path, staging / relative)
        elif item.is_dir():
            shutil.copytree(item, staging / item.name, symlinks=False)
    marked = []
    for path in files_under(staging):
        manifest[path.relative_to(staging).as_posix()] = stamp(path)
        if read_only(path):
            marked.append(path.relative_to(staging).as_posix())
    record = dict(name=args.name, source=str(source), note=args.note or '', kept=time.strftime('%Y-%m-%d %H:%M:%S'),
                  project=project_file(staging).name, files=manifest, read_only=marked)
    (staging.parent / (args.name + '.json')).write_text(json.dumps(record, indent=1), encoding='utf-8')
    staging.rename(target)
    print('kept {}: {} files from {} in {:.1f} s'.format(args.name, len(manifest), source, time.time() - start))
    return 0


def record_of(name):
    path = STORE / (name + '.json')
    folder = fixture_folder(name)
    if not path.is_file() or not folder.is_dir():
        raise SystemExit('no fixture named ' + name + ' (see: kadan_fixture.py list)')
    return folder, json.loads(path.read_text(encoding='utf-8'))


def changes(name):
    """The fixture's files that differ from what was kept, or are missing or added."""
    folder, record = record_of(name)
    kept = record['files']
    now = {p.relative_to(folder).as_posix(): p for p in files_under(folder)}
    found = ['missing ' + r for r in sorted(set(kept) - set(now))]
    found += ['added ' + r for r in sorted(set(now) - set(kept))]
    found += ['changed ' + r for r in sorted(set(kept) & set(now)) if stamp(now[r]) != kept[r]]
    return folder, record, found


def check(args):
    _, record, found = changes(args.name)
    if found:
        print('fixture {} changed since it was kept ({} files):'.format(args.name, len(found)))
        for line in found[:20]:
            print('  ' + line)
        return 1
    print('fixture {} is as kept on {} ({} files)'.format(args.name, record['kept'], len(record['files'])))
    return 0


def make_copy(name, target):
    folder, record, found = changes(name)
    if found:
        raise SystemExit('fixture {} changed since it was kept, so no copy is made: {}'.format(name, '; '.join(found[:5])))
    target = Path(target).resolve()
    if target.exists():
        if not (target / COPY_MARKER).is_file():
            raise SystemExit(str(target) + ' exists and is not a copy made by this tool')
        earlier = json.loads((target / COPY_MARKER).read_text(encoding='utf-8')).get('fixture')
        handler = 'onexc' if sys.version_info >= (3, 12) else 'onerror'
        shutil.rmtree(target, **{handler: lambda function, path, error: remove_file(path)})
        restore_marks(earlier if earlier and (STORE / (earlier + '.json')).is_file() else name)
    start = time.time()
    target.mkdir(parents=True)
    for relative in record['files']:
        source, destination = folder / relative, target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    (target / COPY_MARKER).write_text(json.dumps(dict(fixture=name, made=time.strftime('%Y-%m-%d %H:%M:%S'))), encoding='utf-8')
    print('copied fixture {} to {}: {} files, {:.1f} s'.format(name, target, len(record['files']), time.time() - start))
    return target / record['project']


def copy(args):
    project = make_copy(args.name, args.folder)
    if args.plugin:
        sys.path.insert(0, str(HERE))
        from dev_build import install
        install(Path(args.plugin).resolve(), project)
        # The symbols too, hard-linked, so sample_stacks.py can name the plugin's own functions and lines.
        built = Path(args.plugin).resolve() / 'Plugins/Kadan/Binaries/Win64'
        into = project.parent / 'Plugins/Kadan/Binaries/Win64'
        for pdb in built.glob('UE4Editor-*.pdb'):
            target = into / pdb.name
            if target.exists():
                target.unlink()
            os.link(pdb, target)
        print('put the Kadan of {} into the copy'.format(args.plugin))
    return 0


def list_fixtures(args):
    for path in sorted(STORE.glob('*.json')) if STORE.is_dir() else []:
        record = json.loads(path.read_text(encoding='utf-8'))
        print('{:<20} {:>6} files  kept {}  {}'.format(record['name'], len(record['files']), record['kept'], record['note']))
    return 0


REIMPORT_SCRIPT = '''
import json, os, sys, time
import unreal
start = time.time()
packages = json.loads(os.environ['KF_PACKAGES'])
text = unreal.KadanBlender.import_game_packages(os.environ['KF_PAKS'], os.environ['KF_RETOC'], packages, False, os.environ['KF_REPAK'])
result = json.loads(text) if text else dict(error='no result')
result['seconds'] = round(time.time() - start, 1)
with open(os.environ['KF_OUT'], 'w', encoding='utf-8') as f:
    json.dump(result, f, indent=1)
sys.stdout.flush()
os._exit(0)
'''


def package_files(folder, package):
    if not package.startswith('/Game/') or '..' in package or '//' in package:
        raise SystemExit('a package is a /Game path: ' + package)
    stem = folder / 'Content' / package[len('/Game/'):]
    return [stem.with_suffix(suffix) for suffix in PACKAGE_FILES if stem.with_suffix(suffix).is_file()]


def reimport(args):
    folder = Path(args.folder).resolve()
    if not (folder / COPY_MARKER).is_file():
        raise SystemExit('reimport works only in a copy made by this tool, never in a fixture or a real project')
    project = project_file(folder)
    removed = 0
    for package in args.packages:
        for path in package_files(folder, package):
            remove_file(path)               # removes the copy's file; the fixture keeps its own
            removed += 1
    restore_marks(json.loads((folder / COPY_MARKER).read_text(encoding='utf-8'))['fixture'])
    paks = Path(args.paks or os.environ.get('P4_NATIVE_ORIGINAL_CONTAINERS') or REPO / 'private/game-paks').resolve()
    retoc, repak = REPO / 'private/retoc/retoc.exe', REPO / 'private/repak/repak.exe'
    for tool in (paks, retoc, repak):
        if not tool.exists():
            raise SystemExit('not found: ' + str(tool))
    run = folder / 'Saved/kadan-fixture'
    run.mkdir(parents=True, exist_ok=True)
    script, out, log = run / 'reimport.py', run / 'reimport.json', run / 'reimport-editor.log'
    script.write_text(REIMPORT_SCRIPT, encoding='utf-8')
    out.unlink(missing_ok=True)
    sys.path.insert(0, str(HERE))
    import ue_bake
    editor = ue_bake.find_editor()
    statement = "exec(compile(open({0!r}, encoding='utf-8').read(), {0!r}, 'exec'))".format(script.as_posix())
    command = subprocess.list2cmdline([str(editor), str(project), '-unattended', '-nop4', '-nosplash', '-UTF8Output',
                                       *(['-d3d11', '-NoHMD'] if args.d3d11 else ['-NullRHI']), '-AbsLog=' + str(log)]) + ' -ExecutePythonScript="{}"'.format(statement)
    environment = dict(os.environ, KF_PACKAGES=json.dumps(args.packages), KF_PAKS=str(paks), KF_RETOC=str(retoc),
                       KF_REPAK=str(repak), KF_OUT=str(out))
    start = time.time()
    lock = ue_bake._lock(project=project)
    try:
        code = subprocess.run(command, env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.STDOUT, timeout=args.timeout).returncode
    finally:
        lock.close()
    if not out.is_file():
        print('reimport wrote no result (editor exit {}); log: {}'.format(code, log))
        return 1
    result = json.loads(out.read_text(encoding='utf-8'))
    print('reimported {} packages ({} files removed first) in {:.0f} s, {:.0f} s in the importer: ok {}, {} imported, '
          '{} already present, {} not imported, {} problems'.format(
              len(args.packages), removed, time.time() - start, result.get('seconds', 0), result.get('ok'),
              len(result.get('imported', {})), result.get('already_present'), len(result.get('not_imported', [])),
              len(result.get('problems', []))))
    for line in (result.get('problems', []) + result.get('plan_problems', []) + [result.get('error', '')])[:20]:
        if line:
            print('  ' + line[:300])
    print('result:', out)
    return 0 if result.get('ok') else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('keep'); p.add_argument('name'); p.add_argument('project'); p.add_argument('--note')
    p = sub.add_parser('copy'); p.add_argument('name'); p.add_argument('folder'); p.add_argument('--plugin')
    p = sub.add_parser('check'); p.add_argument('name')
    sub.add_parser('list')
    p = sub.add_parser('reimport'); p.add_argument('folder'); p.add_argument('packages', nargs='+')
    p.add_argument('--paks'); p.add_argument('--timeout', type=float, default=1800); p.add_argument('--d3d11', action='store_true')
    args = parser.parse_args()
    return dict(keep=keep, copy=copy, check=check, list=list_fixtures, reimport=reimport)[args.action](args)


if __name__ == '__main__':
    sys.exit(main())
