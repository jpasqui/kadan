"""Exercise each original map through Kadan's normal native importer and exporter.

Use an isolated project with the tested game declarations. With --session --keep-project,
bounded editor sessions import maps in order, reusing real assets and conversion/shader caches as a
creator does. The default still isolates individual maps. CSV rows record the level reached;
no editor result is reported as game evidence.

Two ways to run:

- Clean (the default): the project's Content is emptied before every map, and the test removes
  what it imported. Every map pays its whole import, shaders included (15-20 minutes).
- Kept (--keep-project): one project for many maps, as a creator works. What earlier maps
  imported stays, and each map's import resumes over it, so a texture, material, mesh or
  Blueprint is imported once per project and its shaders compile once. Failed files created by
  this editor invocation are removed; retained files survive a refused reimport. Every failed
  reply remains reported. The row's counts are the map's whole closure: what this map imported plus what it
  found already imported (from the project's ledger of earlier maps).

Every run also requires four shader-ready SceneCapture2D JPEGs per map. Missing captures
are reported as not verified, even if the controller passed. The gallery is kept beside the report.

--import-only stops each map once it is imported, opened and every original layer reads
unchanged (phase import_verified). The export checks (unchanged export, then a moved and a removed
actor and a placed enemy) run in a second pass over the kept project without --import-only.
"""
import argparse
import csv
from dataclasses import dataclass
import filecmp
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
from datetime import datetime
import kadan_sweep_gallery

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
SELECTOR = 'P4MapKit.Export.EveryMapImportExport'
REFERENCE_SELECTOR = 'P4MapKit.Export.EveryMapWithGameReferencesImportExport'
REFERENCE_QUALIFICATION = 'original-game-overlay-with-supported-editor-content'
FIELDS = ('map', 'category', 'night_layers', 'hero_layers', 'reached', 'imported', 'levels', 'loaded_levels',
          'objects', 'assets', 'missing_assets', 'reference_only_assets', 'unchanged_exported', 'edited_exported',
          'game_seen', 'seconds', 'exit', 'editor_exit', 'error', 'crash_frames')
MODES = ('day', 'night', 'hero')
FIELDS += ('variant', 'variants_requested') + tuple(mode + suffix for mode in MODES for suffix in
           ('_reached', '_activation', '_unchanged_exported', '_unchanged_verified_noop', '_unchanged_outcome', '_edited_exported', '_exit'))
FIELDS += ('kept_project', 'map_preexisting', 'new_assets', 'preserved_assets', 'purged_leftovers', 'phase_seconds', 'worker', 'import_seconds')
FIELDS += ('failed_reply_assets', 'retained_failed_files')
FIELDS += ('unchanged_verified_noop', 'unchanged_outcome')
FIELDS += ('qualification', 'game_backed_levels', 'editable_original_actors', 'uneditable_original_actors')
FIELDS += kadan_sweep_gallery.FIELDS
PRESERVED = 'existing asset preserved'


def map_inventory(text):
    """Actual directory-index World paths, including every game non-sublevel root."""
    paths = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[2] != 'ExportBundleData':
            continue
        path = parts[-1].replace('\\', '/')
        prefix = '../../../Carrot4/Content/'
        if not path.startswith(prefix) or not path.endswith('.umap'):
            continue
        package = '/Game/' + path[len(prefix):-5]
        if '/Maps/' in package:
            paths.add(package)
    result = []
    for package in sorted(paths):
        if '/sublevels/' in package.lower():
            continue
        folder = package.rsplit('/', 1)[0] + '/Sublevels/'
        children = [p for p in paths if p.startswith(folder)]
        category = ('area' if '/Main/Area/' in package else 'battle' if '/Madori/Ddb/' in package else
                    'cave' if '/Madori/Cave/' in package else 'ending' if '/Ending/' in package else 'system')
        result.append(dict(map=package, category=category, night_layers=any('_night' in p.lower() for p in children),
                           hero_layers=any('_hero' in p.lower() for p in children)))
    return result


def frames(text):
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines) if any(word in line for word in
                 ('Fatal error:', 'Assertion failed:', 'Unhandled Exception:', 'Critical error:'))), None)
    kept = 3
    if start is None:
        # The D3D11 error names its code a few lines below; keep every line the check read.
        kept = 4
        start = next((i for i,line in enumerate(lines) if 'LogD3D11RHI: Error:' in line and
                      any('E_OUTOFMEMORY' in nearby for nearby in lines[i:i+kept]) and
                      any('RequestExit(1)' in later for later in lines[i:])),None)
    if start is None:
        return []
    selected = [line.strip() for line in lines if '[Callstack]' in line or re.search(r'0x[0-9a-fA-F]+.*(?:UE4Editor|Unreal)', line)]
    selected = lines[start:start + kept] + selected
    return selected[:16]


def automation_errors(text):
    # dev_tests displays a bounded/truncated error summary; retain the actual editor assertions.
    return [line.split('Error: ', 1)[1] for line in text.splitlines()
            if ('LogAutomationController: Error: ' in line or 'LogAutomationTest: Error: ' in line)
            and 'Test Completed.' not in line]


def unfinished_elapsed(log, leaf):
    """A planned checkpoint is not the elapsed time of a stopped import."""
    stamp=r'\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d:\d+)\]'
    start=re.search(stamp+r'\[\s*\d+\]LogAutomationController: Display: Test Started\. Name=\{(?:\d+_)?'+re.escape(leaf)+r'\}',log)
    if not start:
        return None
    times=re.findall(stamp,log[start.start():])
    if not times:
        return None
    parsed=[datetime.strptime(value,'%Y.%m.%d-%H.%M.%S:%f') for value in times]
    return max(0.,(max(parsed)-parsed[0]).total_seconds())


def write_rows(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.pending')
    with temporary.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, FIELDS, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)
    for attempt in range(20):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            # A reader (or a scanner) holds the report for a moment.
            time.sleep(0.5)
    temporary.replace(path)


def failure_cause(row):
    """A bounded-run key; the row retains the complete original diagnostics separately."""
    crash = row.get('crash_frames', '').split(' | ') if row.get('crash_frames') else []
    diagnostics = '\n'.join(crash[:8]) if crash else row.get('error', '')
    package = row.get('map', '')
    if package:
        diagnostics = diagnostics.replace(package, '<map>').replace(package.rsplit('/', 1)[-1], '<map>')
    diagnostics = re.sub(r'/Game/[^\s\"\'|,;:]+', '<package>', diagnostics)
    diagnostics = re.sub(r'(?i)[A-Z]:[\\/][^\r\n|]*?\.(?:cpp|h|dll|exe|umap|uasset|uexp|json)', '<file>', diagnostics)
    diagnostics = re.sub(r'(?i)((?:exception\s*code|error\s*code|code|exception)\s*[:=]\s*)0x([0-9a-f]+)',
                         lambda match: match.group(1) + '<code:' + match.group(2).lower() + '>', diagnostics)
    diagnostics = re.sub(r'0x[0-9a-fA-F]+', '<address>', diagnostics)
    diagnostics = re.sub(r'<file>:\d+', '<file>:<location>', diagnostics)
    diagnostics = re.sub(r'(?i)\b(?:line|offset)\s*[:=]?\s*\d+', '<location>', diagnostics)
    diagnostics = re.sub(r'\+\s*\d+\s*bytes', '<offset>', diagnostics)
    diagnostics = re.sub(r'\[\d{4}[^\]]*\]|\[\s*\d+\]', '', diagnostics)
    # Symbolic frames and actual error codes remain; ASLR and source-line changes do not.
    return '\n'.join(sorted(set(' '.join(line.split()) for line in diagnostics.splitlines() if line.strip())))


def run_bounded(rows, run_one, keep_going=False):
    """Run actual map callbacks until an independently normalized cause fails twice."""
    failures = {}
    for row in rows:
        run_one(row)
        if row.get('exit') == 0:
            continue
        cause = failure_cause(row)
        if cause:
            failures[cause] = failures.get(cause, 0) + 1
            if failures[cause] >= 2 and not keep_going:
                print('The same failure occurred twice. The remaining maps retain their untested inventory rows.', flush=True)
                break


def variant_cases(rows, selected=(), variant='all'):
    cases = []
    for row in rows:
        if selected and row['map'] not in selected and row['map'].rsplit('/', 1)[-1] not in selected:
            continue
        modes = [mode for mode in MODES if (variant == 'all' or variant == mode) and
                 (mode == 'day' or row.get(mode + '_layers'))]
        row['variants_requested'] = ','.join(modes)
        for mode in modes:
            cases.append(dict(row, variant=mode, _root=row, _modes=modes))
    return cases


def editor_batches(cases, maximum=24):
    """Keep one project while bounding editor-resident assets between sessions."""
    if maximum<0:
        raise ValueError('Editor session size cannot be negative')
    if not cases:
        return
    if maximum==0:
        yield cases
        return
    pending=[]
    for case in cases:
        if case.get('category') in ('area','ending'):
            if pending:
                yield pending
                pending=[]
            yield [case]
        else:
            pending.append(case)
            if len(pending)==maximum:
                yield pending
                pending=[]
    if pending:
        yield pending


def wait_editor_headroom(report):
    """The detached sweep owns its guard wait; emit one waiting/ready notice."""
    if str(REPO) not in sys.path:
        sys.path.insert(0,str(REPO))
    from capture_memory_guard import wait_for_capture_memory
    waiting=False
    def event(name,state):
        nonlocal waiting
        Path(report).with_suffix('.memory.json').write_text(json.dumps(dict(event=name,**state)),encoding='utf-8')
        if name=='waiting_for_memory':
            if not waiting:
                print('Editor launch queued for 6 GiB commit and 4 GiB physical headroom.',flush=True)
            waiting=True
        elif waiting:
            print('Editor launch headroom is available.',flush=True)
    wait_for_capture_memory(event)


def aggregate_variant(case):
    root, mode = case['_root'], case['variant']
    for key in FIELDS:
        if key in case and key not in ('unchanged_exported', 'unchanged_verified_noop', 'unchanged_outcome', 'edited_exported') and not key.startswith(tuple(m + '_' for m in MODES)):
            root[key] = case[key]
    root[mode + '_reached'] = case.get('reached', 'editor_start_failed')
    root[mode + '_activation'] = bool(case.get('variant_activated', False))
    root[mode + '_unchanged_exported'] = bool(case.get('unchanged_exported', False))
    root[mode + '_unchanged_verified_noop'] = bool(case.get('unchanged_verified_noop', False))
    root[mode + '_unchanged_outcome'] = case.get('unchanged_outcome', 'exported' if case.get('unchanged_exported') else 'failed')
    root[mode + '_edited_exported'] = bool(case.get('edited_exported', False) and case.get('exit') == 0 and case.get('reached') == 'editor_verified')
    root[mode + '_exit'] = case.get('exit')
    for field in ('unchanged_exported', 'edited_exported'):
        root[field] = all(root.get(m + '_' + field) is True for m in case['_modes'])
    root['unchanged_verified_noop'] = all(root.get(m + '_unchanged_verified_noop') is True for m in case['_modes'])
    if root['unchanged_verified_noop']:
        root['unchanged_outcome'] = 'verified_no_changes'
    elif root['unchanged_exported']:
        root['unchanged_outcome'] = 'exported'
    elif not all(m + '_unchanged_outcome' in root for m in case['_modes']):
        root['unchanged_outcome'] = 'pending'
    elif all(root.get(m + '_unchanged_exported') is True or root.get(m + '_unchanged_verified_noop') is True for m in case['_modes']):
        root['unchanged_outcome'] = 'mixed'
    else:
        root['unchanged_outcome'] = 'failed'


def no_links(path):
    for item in ([path] + list(path.rglob('*')) if path.exists() else []):
        info = item.lstat()
        if item.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('The owned sweep Content folder contains a filesystem link: ' + str(item))


def owned_sweep_path(path, repository=REPO):
    """Admit this worktree or only its dedicated short mutable test root."""
    path, repository = path.resolve(), repository.resolve()
    if path.is_relative_to(repository / 'private'):
        return True
    if repository.parent.name == 'worktrees' and repository.parent.parent.name == 'private':
        return path.is_relative_to(repository.parents[2] / 'private/kadan-pl2')
    return False


def writable(function, path, _):
    # Imported game assets are read-only on disk; the isolated project's copy is still removed.
    os.chmod(path, stat.S_IWRITE)
    function(path)


def clean_generated_content(project):
    content = project.parent / 'Content'
    resolved = content.resolve()
    if resolved.parent != project.parent.resolve() or resolved.name != 'Content':
        raise ValueError('The sweep Content folder resolves outside its isolated project')
    no_links(content)
    if content.exists():
        shutil.rmtree(content, onerror=writable)
    content.mkdir()


def engine_binaries():
    """The folder the editor's relative paths start from (Engine/Binaries/Win64)."""
    sys.path.insert(0, str(HERE))
    import ue_bake
    editor = ue_bake.find_editor()
    return editor.parent if editor else None


def original_source(header, binaries):
    path = Path(header)
    if not path.is_absolute() and binaries is not None:
        path = binaries / path
    return path.resolve()


def cache_original_sources(project, game, binaries):
    """Hard-link the normal importer's extracted originals before its next cache refresh, so the next
    map reads them from the game folder instead of converting them from the containers again."""
    folder = project.parent / 'Saved/Kadan'
    report = folder / 'import-result.json'
    if not report.is_file() or report.stat().st_size > 64 * 1024 * 1024:
        return 0
    data = json.loads(report.read_text(encoding='utf-8-sig'))
    linked = 0
    for package, reply in data.get('assets', {}).items():
        if not re.fullmatch(r'/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+', package) or not reply.get('header'):
            continue
        source = original_source(reply['header'], binaries)
        if not source.is_file() or not source.is_relative_to(folder.resolve()):
            continue
        # The body files first and the header last: a scan by another worker that shares this game
        # folder sees a package only once all of its files are there.
        for suffix in ('.uexp', '.ubulk', '.uptnl', source.suffix):
            original = source.with_suffix(suffix)
            if not original.is_file():
                continue
            target = game / 'Carrot4/Content' / (package[6:] + suffix)
            if not target.resolve().is_relative_to(game.resolve()):
                raise ValueError('An original package cache target escapes the selected game input')
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                try:
                    os.link(original, target)
                    linked += 1
                    continue
                except FileExistsError:
                    pass
            # Existing immutable original inputs are preserved. Their identity is not a mod claim.
            if not filecmp.cmp(original, target, shallow=False):
                raise ValueError('The owned original-source cache contains different game bytes for ' + package)
    return linked


def ledger_path(project):
    return project.parent / 'Saved/P4MapKit/EveryMap/ledger.json'


def load_ledger(project):
    path = ledger_path(project)
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}


def save_ledger(project, ledger):
    path = ledger_path(project)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.pending')
    temporary.write_text(json.dumps(ledger, sort_keys=True), encoding='utf-8')
    temporary.replace(path)


@dataclass(frozen=True)
class ContentSnapshot:
    root: Path
    files: frozenset


def reparse_path(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) &
                                             getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400))


def content_before_invocation(project):
    """Record names before the owned editor starts; never follow Content aliases."""
    content = (project.parent / 'Content').absolute()
    try:
        content.lstat()
    except FileNotFoundError:
        return ContentSnapshot(content.resolve(), frozenset())
    if reparse_path(content):
        raise ValueError('Sweep Content custody refuses an aliased root')
    content = content.resolve()
    files = set()
    def failed(error):
        raise error  # Missing inventory evidence must not be mistaken for absence.
    for folder, directories, names in os.walk(content, onerror=failed, followlinks=False):
        directories[:] = [name for name in directories if not reparse_path(Path(folder) / name)]
        files.update((Path(folder) / name).absolute() for name in names)
    return ContentSnapshot(content, frozenset(files))


def created_in_invocation(path, content, before):
    if not isinstance(before, ContentSnapshot) or before.root != content or path.absolute() in before.files:
        return False
    try:
        path.absolute().relative_to(content)
        path.resolve(strict=True).relative_to(content)
        # A new alias names someone else's bytes, not a newly created owned file.
        for part in (path,) + tuple(path.parents):
            if reparse_path(part):
                return False
            if part == content:
                info = path.lstat()
                # Making an inode writable also changes every hard-linked source.
                return stat.S_ISREG(info.st_mode) and info.st_nlink == 1
    except (OSError, ValueError):
        return False
    return False


def kept_closure(project, row, ledger, report=None, *, before=None, retained_packages=()):
    """The map's whole closure in a kept project: what it imported now, plus what it found imported by
    earlier maps (named by the ledger). Then the ledger learns this map's new assets, and what failed is
    removed from Content only when this invocation demonstrably created its files.
    Invalid replies never promote retained files to completed imports."""
    report = report or project.parent / 'Saved/Kadan/import-result.json'
    if not report.is_file():
        return
    data = json.loads(report.read_text(encoding='utf-8-sig'))
    timing = data.get('seconds') or {}
    if timing:
        row['import_seconds'] = ' '.join('{}={}'.format(k.replace(' ', '_'), v) for k, v in sorted(timing.items(), key=lambda kv: -kv[1]))
    assets = data.get('assets', {})
    reference_only = set(data.get('reference_only', []))
    previously_imported = set(ledger) | set(retained_packages)
    new, preserved, failed = {}, [], []
    for package, reply in assets.items():
        if reply.get('valid') and reply.get('state') == PRESERVED:
            preserved.append(package)
        elif reply.get('valid'):
            new[package] = reply
        else:
            failed.append(package)
    for package, reply in new.items():
        # The importer's own reference-only list, as a clean run counts it (a reply's flag says more).
        ledger[package] = dict(kind=reply.get('kind', ''), reference_only=package in reference_only,
                               objects=reply.get('objects', 0) if reply.get('kind') == 'World' else 0)
    closure = list(new) + preserved
    known = [ledger.get(package, {}) for package in closure]
    row['new_assets'] = len(new)
    row['preserved_assets'] = len(preserved)
    row['assets'] = len(closure)
    row['levels'] = sum(1 for one in known if one.get('kind') == 'World')
    row['objects'] = sum(int(one.get('objects') or 0) for one in known)
    row['reference_only_assets'] = len(reference_only | {p for p in preserved if ledger.get(p, {}).get('reference_only')})
    row['failed_reply_assets'] = len(failed)
    content = (project.parent / 'Content').resolve()
    purged = retained = 0
    for package in failed:
        ledger.pop(package, None)
        if not re.fullmatch(r'/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+', package):
            continue
        for suffix in ('.uasset', '.umap', '.uexp', '.ubulk', '.uptnl'):
            leftover = content / (package[6:] + suffix)
            if leftover.is_file():
                # Sessions read replies after the editor exits. A previous map's
                # successful file is retained even if a later map refuses it.
                if package not in previously_imported and created_in_invocation(leftover, content, before):
                    os.chmod(leftover, stat.S_IWRITE)
                    leftover.unlink()
                    purged += 1
                else:
                    retained += 1
    row['purged_leftovers'] = purged
    row['retained_failed_files'] = retained


def fail_missing_assets(row):
    """An asset that did not import fails the map, however far the map itself got."""
    if row.get('reached') in ('import_verified', 'editor_verified') and row.get('missing_assets'):
        row.update(reached='assets_missing', exit=row.get('exit') or 1,
                   error=' | '.join(filter(None, [row.get('error'), '{} assets did not import.'.format(row['missing_assets'])])))


def fail_unpainted_landscapes(row, warnings):
    """A landscape whose colors could not be recovered draws black, so its map is not verified."""
    unpainted = [text for text in warnings if ' landscape color preview: ' in text]
    if row.get('reached') in ('import_verified', 'editor_verified') and unpainted:
        row.update(reached='landscape_unpainted', exit=row.get('exit') or 1,
                   error=' | '.join(filter(None, [row.get('error'), '{} landscape materials draw black: {}'.format(
                       len(unpainted), unpainted[0].split(' landscape color preview: ', 1)[1][:300])])))


def import_warnings(report_file):
    """The importer's warnings, which live only in its report."""
    try:
        return [str(text)[:1000] for text in (json.loads(report_file.read_text(encoding='utf-8-sig')).get('warnings') or [])[:400]]
    except (OSError, ValueError):
        return []


def read_list(path):
    names = []
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            names.extend(line.split())
    return names


def session_environment(args, cases, game, gallery):
    environment = dict(os.environ, P4_NATIVE_SWEEP_MAPS=';'.join(case['map'] for case in cases),
                       P4_NATIVE_SWEEP_VARIANT=cases[0]['variant'], P4_NATIVE_SWEEP_CAPTURE=str(gallery),
                       P4_NATIVE_SWEEP_GAME=str(game), P4_NATIVE_ORIGINAL_CONTAINERS=str(args.containers.resolve()),
                       P4_NATIVE_RETOC=str(args.retoc.resolve()), P4_NATIVE_SWEEP_KEEP='1')
    environment.pop('P4_NATIVE_SWEEP_MAP', None)
    if args.import_only:
        environment['P4_NATIVE_SWEEP_IMPORT_ONLY'] = '1'
    else:
        environment.pop('P4_NATIVE_SWEEP_IMPORT_ONLY', None)
    if args.reimport:
        environment['P4_NATIVE_SWEEP_REIMPORT'] = '1'
    else:
        environment.pop('P4_NATIVE_SWEEP_REIMPORT', None)
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--built', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True, help='owned extracted-original source folder with original shader library')
    parser.add_argument('--containers', type=Path, default=Path(os.environ.get('APPDATA', '')) / 'eden/dump/0100B7C00933A000/romfs/Carrot4/Content/Paks')
    parser.add_argument('--retoc', type=Path, default=REPO / 'private/retoc/retoc.exe')
    parser.add_argument('--report', type=Path, default=REPO / 'private/map-sweep/results.csv')
    parser.add_argument('--gallery', type=Path, help='required render-target evidence (default: gallery/<report stem> beside report)')
    parser.add_argument('--inventory-only', action='store_true')
    parser.add_argument('--only', nargs='*', default=[], help='canonical map packages or exact world leaf names')
    parser.add_argument('--list', type=Path, help='a file naming the maps to run, one or more per line (added to --only)')
    parser.add_argument('--variant', choices=('all',) + MODES, default='all', help='actual retained editor layers; game route activation is separate')
    parser.add_argument('--timeout', type=float, default=7200)
    parser.add_argument('--keep-going', action='store_true', help='run every selected map even when one cause repeats (a survey)')
    parser.add_argument('--keep-project', action='store_true', help='one project for many maps: keep what earlier maps imported and resume over it')
    parser.add_argument('--import-only', action='store_true', help='stop each map at import_verified; export checks are a second pass')
    parser.add_argument('--game-references', action='store_true', help='qualify every original map with supported editor content, original game references, and a supported native addition; keep the strict stock-enemy gate separate')
    parser.add_argument('--redo', action='store_true', help='run maps the report already has a result for again')
    parser.add_argument('--worker', default='', help='a name recorded in each row')
    parser.add_argument('--no-input-cache', action='store_true', help='leave the game input folder as it is: do not link the packages a map extracted into it (for timing runs that must start from the same input)')
    parser.add_argument('--session', action='store_true', help='import maps sequentially in bounded editor sessions (needs --keep-project)')
    parser.add_argument('--session-size',type=int,default=24,help='maximum maps per editor; area/ending maps run alone; 0 keeps one unbounded session')
    parser.add_argument('--reimport', action='store_true', help='with --keep-project --game-references: import the map again as the Reimport button does, instead of resuming')
    parser.add_argument('--input-root', type=Path, help='the repository root holding private/mapkit-native-workflow (dev_tests --input-root)')
    args = parser.parse_args()
    if args.session_size<0:parser.error('--session-size must be nonnegative')
    project, game, report = args.project.resolve(), args.game.resolve(), args.report.resolve()
    gallery = (args.gallery or report.parent/'gallery'/report.stem).resolve()
    containers = sorted(args.containers.glob('*.utoc'))
    if not containers:
        parser.error('No original .utoc containers were found')
    listed = []
    for container in containers:
        result = subprocess.run([str(args.retoc.resolve()), 'list', '--path', str(container.resolve())],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf-8', errors='replace')[:2000])
        listed.append(result.stdout.decode('utf-8', errors='strict'))
    inventory = map_inventory('\n'.join(listed))
    if not inventory:
        parser.error('The actual containers contain no game root maps')
    rows = [dict(item, reached='inventory', game_seen=False) for item in inventory]
    for row in rows:
        for mode in MODES:
            row[mode + '_reached'] = 'inventory' if mode == 'day' or row.get(mode + '_layers') else 'absent_from_inventory'
            row[mode + '_activation'] = False
    # A stopped worker starts again where it was: the maps its report already holds keep their rows.
    previous = {}
    if report.is_file() and not args.redo:
        with report.open(encoding='utf-8-sig', newline='') as stream:
            previous = {row['map']: row for row in csv.DictReader(stream)
                        if row.get('reached') not in ('inventory', '', None) and
                        row.get('capture_status') == 'verified' and kadan_sweep_gallery.resume_verified(row,gallery) and
                        (row.get('qualification') == REFERENCE_QUALIFICATION if args.game_references
                         else row.get('qualification') != REFERENCE_QUALIFICATION)}
        for row in rows:
            if row['map'] in previous:
                row.update({key: value for key, value in previous[row['map']].items() if key in FIELDS and key != 'variants_requested'})
    write_rows(report, rows)
    report.with_suffix('.inventory.json').write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
    print(str(len(inventory)) + ' original game root maps inventoried. Report ' + str(report))
    if args.inventory_only:
        return 0
    if not project.is_file() or not game.is_dir():
        parser.error('Choose an existing isolated test project and an owned original game input folder')
    if not owned_sweep_path(game) or not owned_sweep_path(project):
        parser.error('Use this lane\'s private inputs or its dedicated short private/kadan-pl2 project and cache')
    content = project.parent / 'Content'
    no_links(content)
    ledger = load_ledger(project) if args.keep_project else {}
    holds_content = content.exists() and (any(content.rglob('*.uasset')) or any(content.rglob('*.umap')))
    if holds_content and not (args.keep_project and ledger_path(project).is_file()):
        parser.error('The sweep project must start without imported content (or be a kept project with its ledger). Use an isolated empty project')
    if args.keep_project and not ledger_path(project).is_file():
        save_ledger(project, ledger)
    selected = set(args.only) | (set(read_list(args.list)) if args.list else set())
    known = {row['map'] for row in rows} | {row['map'].rsplit('/', 1)[-1] for row in rows}
    if selected - known:
        parser.error('The requested maps are absent from the actual original containers: ' + ', '.join(sorted(selected - known)))
    variant = 'day' if args.import_only else args.variant
    cases = [case for case in variant_cases(rows, selected, variant) if case['map'] not in previous]
    if args.list:
        # A session imports in the list's order.
        order = {name: index for index, name in enumerate(read_list(args.list))}
        position = lambda case: order.get(case['map'], order.get(case['map'].rsplit('/', 1)[-1], len(order)))
        cases.sort(key=position)
    if not cases:
        print('Every selected map already has a result in the report.')
        return 0
    write_rows(report, rows)
    binaries = engine_binaries()
    # Initial DLL installation happens once. Subsequent runs skip identical installed bytes.
    def run_one(row):
        if not args.keep_project:
            clean_generated_content(project)
        result_file = project.parent / 'Saved/P4MapKit/EveryMap/result.json'
        result_file.unlink(missing_ok=True)
        # An editor that stops before its import starts must not leave the previous map's report to be read.
        (project.parent / 'Saved/Kadan/import-result.json').unlink(missing_ok=True)
        environment = dict(os.environ, P4_NATIVE_SWEEP_MAP=row['map'], P4_NATIVE_SWEEP_VARIANT=row['variant'], P4_NATIVE_SWEEP_GAME=str(game),
                           P4_NATIVE_SWEEP_CAPTURE=str(gallery),
                           P4_NATIVE_ORIGINAL_CONTAINERS=str(args.containers.resolve()), P4_NATIVE_RETOC=str(args.retoc.resolve()))
        for name, on in (('P4_NATIVE_SWEEP_KEEP', args.keep_project), ('P4_NATIVE_SWEEP_IMPORT_ONLY', args.import_only),
                         ('P4_NATIVE_SWEEP_REIMPORT', args.reimport)):
            if on:
                environment[name] = '1'
            else:
                environment.pop(name, None)
        command = [sys.executable, str(HERE / 'dev_tests.py'), '--built', str(args.built.resolve()), '--project', str(project),
                   '--only', REFERENCE_SELECTOR if args.game_references else SELECTOR, '--timeout', str(args.timeout)]
        if args.input_root:
            command += ['--input-root', str(args.input_root.resolve())]
        wait_editor_headroom(report)
        started = time.time()
        before = content_before_invocation(project) if args.keep_project else None
        outcome = subprocess.run(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=args.timeout + 120)
        output = outcome.stdout.decode('utf-8', errors='replace')
        log_path = project.parent / 'Saved/dev-tests.log'
        log = log_path.read_text(encoding='utf-8', errors='replace') if log_path.is_file() else ''
        actual = json.loads(result_file.read_text(encoding='utf-8-sig')) if result_file.is_file() else {}
        if actual and (actual.get('map') != row['map'] or actual.get('variant') != row['variant']):
            raise ValueError('The actual native sweep reply names a different map or variant')
        row.update({key: actual[key] for key in FIELDS if key in actual})
        row['variant_activated'] = bool(actual.get('variant_activated', False))
        crash = frames(log)
        editor_exit = re.search(r'\(exit (-?\d+)', output)
        row.update(reached=actual.get('phase', 'editor_start_failed'), seconds=round(time.time() - started, 1),
                   exit=outcome.returncode, editor_exit=int(editor_exit.group(1)) if editor_exit else None,
                   game_seen=False, crash_frames=' | '.join(crash), worker=args.worker,
                   phase_seconds=' '.join('{}={}'.format(k, v) for k, v in (actual.get('phase_seconds') or {}).items()))
        errors = actual.get('problems', []) + [actual.get('unchanged_error', ''), actual.get('edited_error', '')] + automation_errors(log)
        if outcome.returncode:
            errors += [line for line in output.splitlines() if 'Expected ' in line or 'Incomplete suite' in line or 'failed:' in line]
        row['error'] = ' | '.join(str(x) for x in errors if x)[:8000]
        fail_missing_assets(row)
        fail_unpainted_landscapes(row, import_warnings(project.parent / 'Saved/Kadan/import-result.json'))
        if args.keep_project:
            kept_closure(project, row, ledger, before=before)
            save_ledger(project, ledger)
        kadan_sweep_gallery.record(row,actual,gallery,project.parent/'Saved/Kadan/import-result.json')
        evidence = report.parent / 'maps' / (row['map'].rsplit('/', 1)[-1] + '_' + row['variant'] + '.json')
        evidence.parent.mkdir(parents=True, exist_ok=True)
        evidence.write_text(json.dumps(dict({key: value for key, value in row.items() if not key.startswith('_')},
                                           native=actual, run=output[-12000:]), indent=2) + '\n', encoding='utf-8')
        linked = 0 if args.no_input_cache else cache_original_sources(project, game, binaries)
        aggregate_variant(row)
        write_rows(report, rows)
        print(row['map'].rsplit('/', 1)[-1] + ' ' + row['variant'] + ' reached ' + row['reached'] + ' in ' + str(row['seconds']) + ' s, assets ' +
              str(row.get('assets', 'unknown')) + ' (' + str(row.get('preserved_assets', 0)) + ' already imported), missing ' +
              str(row.get('missing_assets', 'unknown')) + ', reference only ' + str(row.get('reference_only_assets', 'unknown')) +
              ', originals cached ' + str(linked), flush=True)
    def run_session(group):
        """Every map of the group in one editor, one after another, as a creator imports them. A crash ends
        the session at its map; the editor starts again for the maps after it."""
        folder = project.parent / 'Saved/P4MapKit/EveryMap'
        remaining = list(group)
        while remaining:
            for case in remaining:
                for suffix in ('.json', '.import.json'):
                    (folder / (case['map'].rsplit('/', 1)[-1] + '_' + case['variant'] + suffix)).unlink(missing_ok=True)
            environment = session_environment(args, remaining, game, gallery)
            budget = args.timeout * len(remaining)
            command = [sys.executable, str(HERE / 'dev_tests.py'), '--built', str(args.built.resolve()), '--project', str(project),
                       '--only', REFERENCE_SELECTOR if args.game_references else SELECTOR, '--timeout', str(budget), '--controller-timeout', str(int(min(7200, budget)))]
            if args.input_root:
                command += ['--input-root', str(args.input_root.resolve())]
            wait_editor_headroom(report)
            started = time.time()
            print('Session of {} maps started.'.format(len(remaining)), flush=True)
            # The editor writes each map's reply as it goes. Report completed imports while the same
            # process continues, rather than leaving a creator blind until the whole session ends.
            live_done = set()
            before = content_before_invocation(project)
            session_retained = set(ledger)
            process = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            deadline = started + budget + 300
            while True:
                try:
                    captured, _ = process.communicate(timeout=2)
                    output, code = captured.decode('utf-8', errors='replace'), process.returncode
                    break
                except subprocess.TimeoutExpired:
                    for case in remaining:
                        identity = (case['map'], case['variant'])
                        if identity in live_done:
                            continue
                        leaf = case['map'].rsplit('/', 1)[-1]
                        reply_file = folder / (leaf + '_' + case['variant'] + '.json')
                        try:
                            progress = json.loads(reply_file.read_text(encoding='utf-8-sig'))
                        except (OSError, ValueError):
                            continue  # a checkpoint may be in the middle of its write
                        phases = progress.get('phase_seconds') or {}
                        if progress.get('map') != case['map'] or progress.get('variant') != case['variant'] or 'ended' not in phases:
                            continue
                        live_done.add(identity)
                        print('{} {} import step ended: phase {}, {:.1f} s, {} missing assets; controller result pending.'.format(
                            leaf, case['variant'], progress.get('phase', 'unknown'), phases['ended'], progress.get('missing_assets', 'unknown')), flush=True)
                    if time.time() >= deadline:
                        process.kill()
                        captured, _ = process.communicate()
                        output, code = captured.decode('utf-8', errors='replace'), -1
                        break
            log_path = project.parent / 'Saved/dev-tests.log'
            log = log_path.read_text(encoding='utf-8', errors='replace') if log_path.is_file() else ''
            first_test = re.search(r'\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):\d+\]\[\s*\d+\]LogAutomationController: Display: Test Started', log)
            head = re.search(r'\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):\d+\]', log)
            if first_test and head:
                stamp = lambda m: time.mktime(tuple(int(x) for x in m.groups()) + (0, 0, -1))
                print('Editor start and test discovery: {:.0f} s'.format(stamp(first_test) - stamp(head)), flush=True)
            crashed = frames(log)
            # Each test's own lines: from its "Test Started" to the next.
            sections = re.split(r'(?=LogAutomationController: Display: Test Started\. Name=\{)', log)
            by_name = {}
            for section in sections[1:]:
                name = section.split('Name={', 1)[1].split('}', 1)[0]
                by_name[name.split('_', 1)[1] if re.match(r'\d{3}_', name) else name] = section
            done_now = []
            for case in remaining:
                leaf = case['map'].rsplit('/', 1)[-1]
                reply_file = folder / (leaf + '_' + case['variant'] + '.json')
                if not reply_file.is_file():
                    break
                try:
                    actual = json.loads(reply_file.read_text(encoding='utf-8-sig'))
                except ValueError:
                    # An empty or cut-off reply: the editor could not finish writing it (it died, or the disk
                    # was full). The map is reported as stopped and the session carries on after it.
                    actual = {'map': case['map'], 'variant': case['variant'], 'phase': 'reply_unreadable',
                              'problems': ['The native reply {} is empty or cut off ({} bytes).'.format(
                                  reply_file.name, reply_file.stat().st_size)]}
                if actual.get('map') != case['map'] or actual.get('variant') != case['variant']:
                    raise ValueError('The actual native session reply names a different map or variant')
                ended = 'ended' in (actual.get('phase_seconds') or {})
                section = by_name.get(leaf, '')
                case.update({key: actual[key] for key in FIELDS if key in actual})
                case['variant_activated'] = bool(actual.get('variant_activated', False))
                passed = 'Result={Passed}' in section
                seconds = (actual.get('phase_seconds') or {}).get('ended') or max((actual.get('phase_seconds') or {}).values() or [0])
                if not ended:
                    elapsed=unfinished_elapsed(log,leaf)
                    if elapsed is not None:seconds=elapsed
                case.update(reached=actual.get('phase', 'editor_start_failed'), seconds=seconds, exit=0 if passed and ended else 1,
                            editor_exit=code, game_seen=False, crash_frames='' if ended else ' | '.join(crashed), worker=args.worker,
                            phase_seconds=' '.join('{}={}'.format(k, v) for k, v in (actual.get('phase_seconds') or {}).items()))
                errors = actual.get('problems', []) + [actual.get('unchanged_error', ''), actual.get('edited_error', '')] + automation_errors(section)
                if not ended:
                    errors.append('The editor stopped during this map (exit {}).'.format(code) if code != -1 else 'The session ran out of time during this map.')
                    if any('E_OUTOFMEMORY' in frame for frame in crashed):
                        errors.append('D3D11 exhausted available memory during import; this map attempt is not verified.')
                case['error'] = ' | '.join(str(x) for x in errors if x)[:8000]
                fail_missing_assets(case)
                own_report = folder / (leaf + '_' + case['variant'] + '.import.json')
                fail_unpainted_landscapes(case, import_warnings(own_report))
                kept_closure(project, case, ledger, own_report, before=before, retained_packages=session_retained)
                kadan_sweep_gallery.record(case,actual,gallery,own_report)
                # Keep custody independently of the mutable ledger: a first
                # refusal removes its ledger row, but a second cannot remove
                # bytes successfully published by an earlier map this session.
                session_retained.update(ledger)
                save_ledger(project, ledger)
                # The class-default proof's per-class reasons, why a game package was kept for reference only and
                # the importer's warnings live only in the import report; keep them.
                class_proof, reference_reasons, warnings = {}, {}, import_warnings(own_report)
                try:
                    imported = json.loads(own_report.read_text(encoding='utf-8-sig'))
                    class_proof = imported.get('native_class_postflight') or {}
                    reference_reasons = imported.get('game_reference_reasons') or {}
                except (OSError, ValueError):
                    pass
                own_report.unlink(missing_ok=True)
                evidence = report.parent / 'maps' / (leaf + '_' + case['variant'] + '.json')
                evidence.parent.mkdir(parents=True, exist_ok=True)
                evidence.write_text(json.dumps(dict({key: value for key, value in case.items() if not key.startswith('_')},
                                                   native=actual, native_class_postflight=class_proof,
                                                   game_reference_reasons=reference_reasons, warnings=warnings,
                                                   run=section[-12000:]),
                                               indent=2) + '\n', encoding='utf-8')
                aggregate_variant(case)
                done_now.append(case)
                print(leaf + ' ' + case['variant'] + ' reached ' + case['reached'] + ' in ' + str(case['seconds']) + ' s, assets ' +
                      str(case.get('assets', 'unknown')) + ' (' + str(case.get('preserved_assets', 0)) + ' already imported), missing ' +
                      str(case.get('missing_assets', 'unknown')) + '; import ' + str(case.get('import_seconds', '')), flush=True)
                if not ended:
                    break
            write_rows(report, rows)
            print('Session ended after {:.0f} s (exit {}), {} of {} maps.'.format(time.time() - started, code, len(done_now), len(remaining)), flush=True)
            if not done_now:
                # Nothing started: the first map is reported as the editor's failure and skipped.
                remaining[0].update(reached='editor_start_failed', exit=code, error=output[-2000:], crash_frames=' | '.join(crashed), worker=args.worker)
                kadan_sweep_gallery.record(remaining[0],{},gallery)
                aggregate_variant(remaining[0])
                write_rows(report, rows)
                done_now = [remaining[0]]
            remaining = remaining[len(done_now):]

    if args.session:
        if not args.keep_project:
            parser.error('--session imports maps one after another into one kept project; add --keep-project')
        for mode in MODES:
            group = [case for case in cases if case['variant'] == mode]
            for batch in editor_batches(group,args.session_size):
                run_session(batch)
    else:
        run_bounded(cases, run_one, args.keep_going)
    done = 'import_verified' if args.import_only else 'editor_verified'
    return 0 if all(case.get('exit') == 0 and case.get('reached') == done for case in cases) else 1


if __name__ == '__main__':
    sys.exit(main())
