"""Bake a navmesh in Unreal Engine.

Bake a navmesh for a Pikmin 4 map headlessly with an installed UE4.26 (no editor clicks).

It makes a throwaway UE4.26 project, imports the collision OBJ(s), runs the map's helper
script (bounds, areas, gimmick shapes), builds navigation and cooks the level for Windows.
The cooked map is what swap_navmesh.py converts into the game's map.

    python ue_bake.py <project dir> --floor terrain-UE4-import.obj --ini navmesh.ini
                      --run MAP_helper_volumes.py [--fill-floor fill-UE4-import.obj]
"""
import json
import os
import subprocess
import sys
import time
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
EDITOR_SCRIPT = HERE / 'ue_bake_editor.py'
COOKED = 'Saved/Cooked/WindowsNoEditor/SwapTest/Content/Maps/TerraceBake.umap'


def find_editor():
    """UE4Editor-Cmd.exe of an installed UE 4.26, or None. P4_UE_ROOT names the engine folder on a
    machine where it is not the launcher's (the desktop: tools/remote_unreal.py sets it)."""
    candidates = [Path(os.environ['P4_UE_ROOT'])] if os.environ.get('P4_UE_ROOT') else []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\EpicGames\Unreal Engine\4.26') as key:
            candidates.append(Path(winreg.QueryValueEx(key, 'InstalledDirectory')[0]))
    except OSError:
        pass
    for base in (os.environ.get('ProgramFiles', r'C:\Program Files'), r'C:\Program Files', r'D:\Program Files'):
        candidates.append(Path(base) / 'Epic Games' / 'UE_4.26')
    for root in candidates:
        exe = root / 'Engine/Binaries/Win64/UE4Editor-Cmd.exe'
        if exe.is_file():
            return exe
    return None


def obj_bounds(path):
    """(low, high, faces) of an OBJ written for UE's importer (Y mirrored in the file)."""
    lo, hi, faces = [1e30] * 3, [-1e30] * 3, 0
    with open(path, encoding='utf-8') as f:
        for line in f:
            if line.startswith('v '):
                x, y, z = (float(v) for v in line.split()[1:4])
                for k, v in enumerate((x, -y, z)):
                    lo[k], hi[k] = min(lo[k], v), max(hi[k], v)
            elif line.startswith('f '):
                faces += 1
    return lo, hi, faces


class Cancelled(RuntimeError):
    """The user pressed Cancel while a UE step ran."""


def _last_line(path):
    """The last log line of a running UE process, without its timestamp prefix."""
    try:
        with open(path, 'rb') as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4096))
            lines = [x.strip() for x in f.read().decode('utf-8', 'replace').splitlines() if x.strip()]
    except OSError:
        return ''
    line = lines[-1] if lines else ''
    while line.startswith('[') and ']' in line:  # a live log may end halfway through a prefix
        line = line.split(']', 1)[-1]
    return line.split(': ', 1)[-1][:100] if 'Log' in line[:40] else line[:100]


def package_batches(packages, limit=16000):
    """Bound explicit Cook package lists so Windows' 32767 UTF-16 command limit is safe.
    Leave room for the editor, project and other flags. Preserve order and package scope.
    """
    batch, units = [], len('-Map=') + 2
    for package in packages:
        size = len(package.encode('utf-16-le')) // 2 + 1
        if size + len('-Map=') + 2 > limit:
            raise ValueError('A cook package path exceeds the command-line budget: ' + package)
        if batch and units + size > limit:
            yield batch
            batch, units = [], len('-Map=') + 2
        batch.append(package)
        units += size
    if batch:
        yield batch


def run_logged(command, log_path, env=None, tick=None, poll=0.5, max_log_bytes=16*1024*1024, stall_timeout=None):
    """Run one owned UE process with bounded logs, cancellation and optional stalls.

    Drain stdout continuously so a verbose child cannot block on a full pipe.
    Keep the latest log and one previous segment. The importer watchdog observes
    output progress; long Lightmass builds do not use an idle timeout.
    """
    if max_log_bytes < 1024:raise ValueError('Log limit must be at least 1024 bytes')
    log_path=Path(log_path)
    previous=log_path.with_name(log_path.stem+'.previous'+log_path.suffix)
    start=time.monotonic();state={'last':start,'error':None}
    if stall_timeout is None and env and env.get('P4_KIT_IMPORT_STALL_TIMEOUT'):
        stall_timeout=float(env['P4_KIT_IMPORT_STALL_TIMEOUT'])
    proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,
                          creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    def drain():
        out=None
        try:
            out=open(str(log_path),'wb');size=0
            while True:
                chunk=proc.stdout.read1(min(65536,max_log_bytes))
                if not chunk:break
                state['last']=time.monotonic()
                if size and size+len(chunk)>max_log_bytes:
                    out.close()
                    try:
                        if previous.exists():previous.unlink()
                        log_path.replace(previous)
                    except OSError:
                        pass  # a reader holds the log open: start it again in place, the job goes on
                    out=open(str(log_path),'wb');size=0
                out.write(chunk);out.flush();size+=len(chunk)
        except BaseException as error:
            state['error']=error
        finally:
            if out:out.close()
            proc.stdout.close()
    reader=threading.Thread(target=drain,daemon=True);reader.start()
    try:
        while True:
            try:
                code=proc.wait(timeout=poll)
                reader.join(timeout=10)  # a child of the editor that outlives it can keep the pipe open
                if state['error']:raise state['error']
                return code
            except subprocess.TimeoutExpired:
                elapsed=time.monotonic()-start
                if state['error']:raise state['error']
                if tick and tick(elapsed,_last_line(log_path)):
                    raise Cancelled('Cancelled')
                if stall_timeout and time.monotonic()-state['last']>stall_timeout:
                    raise RuntimeError('UE4 stopped after {:.0f}s without progress. Run it again to continue.\nThe log is {}'.format(stall_timeout,log_path))
    finally:
        if proc.poll() is None:
            proc.kill();proc.wait()
        reader.join(timeout=5)


def _lock(tick=None, project=None):
    """One UE commandlet at a time on a project: concurrent ones fight over the project's
    Boot.ddc and fail with "Could not save memory cache". With `project` (a folder or a .uproject)
    the lock is that project's own, so runs on different projects do not wait for each other.
    Without it, the lock is the one every caller that names no project shares."""
    import hashlib
    import msvcrt
    name = 'ue-bake.lock'
    if project:
        name = 'ue-' + hashlib.sha1(str(Path(project).resolve()).lower().encode('utf-8')).hexdigest()[:12] + '.lock'
    path = Path(os.environ.get('LOCALAPPDATA', HERE)) / 'P4MapKit' / name
    path.parent.mkdir(parents=True, exist_ok=True)
    lock = open(path, 'a+')
    start = time.time()
    while True:
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            return lock
        except OSError:
            if tick and tick(time.time() - start, 'Waiting for another Pikmin 4 map kit bake to finish'):
                lock.close()
                raise Cancelled('Cancelled')
            time.sleep(0.5)


def bake(project, floor, ini_text, helpers=(), fill_floor=None, editor=None, log=print, tick=None):
    """Bake and cook, returns the cooked .umap path. Raises RuntimeError with the log to read."""
    project = Path(project).resolve()
    editor = Path(editor) if editor else find_editor()
    if not editor or not editor.is_file():
        raise RuntimeError('UE4.26 was not found. Install Unreal Engine 4.26 from the Epic Games launcher, '
                           'or bake in your own UE4.26 project and use the cooked map instead.')
    if project.exists():
        raise RuntimeError(f'{project} already exists. Choose a new folder')
    (project / 'Config').mkdir(parents=True)
    (project / 'SourceGeometry').mkdir()
    (project / 'SwapTest.uproject').write_text(json.dumps(dict(
        FileVersion=3, EngineAssociation='4.26', Plugins=[dict(Name='PythonScriptPlugin', Enabled=True),
                                                         dict(Name='EditorScriptingUtilities', Enabled=True)]),
        indent=2))
    startup = '[/Script/EngineSettings.GameMapsSettings]\nEditorStartupMap=/Game/Maps/TerraceBake\n\n'
    # Own Boot.ddc per project: a shared one breaks when another UE commandlet runs at the same time.
    startup += ('[InstalledDerivedDataBackendGraph]\n'
                'Boot=(Type=Boot, Filename="%GAMEDIR%DerivedDataCache/Boot.ddc", MaxCacheSize=512)\n\n')
    (project / 'Config/DefaultEngine.ini').write_text(startup + ini_text)
    (project / 'SourceGeometry/TerraceFloor.obj').write_bytes(Path(floor).read_bytes())
    lo, hi, faces = obj_bounds(floor)
    fill = None
    if fill_floor:
        (project / 'SourceGeometry/FillFloor.obj').write_bytes(Path(fill_floor).read_bytes())
        flo, fhi, ffaces = obj_bounds(fill_floor)
        fill = dict(low=flo, high=fhi, faces=ffaces)
    (project / 'scene.json').write_text(json.dumps(dict(
        low=lo, high=hi, faces=faces, extra_scripts=[str(Path(h).resolve()) for h in helpers],
        boxes=[], no_bounds=True, fill=fill), indent=2))
    log(f'Collision has {faces} triangles' + (f' + {fill["faces"]} filled underneath' if fill else ''))
    lock = _lock(tick)
    try:
        script = EDITOR_SCRIPT.as_posix()
        steps = [('import', ['-run=pythonscript', '-NullRHI'],
                  f' -script="exec(compile(open({script!r}).read(),{script!r},\'exec\'))"'),
                 ('navigation', ['-run=ResavePackages', '-BuildNavigationData', '-PackageFolder=/Game/Maps', '-NullRHI',
                                 '-IgnoreChangelist'], ''),
                 ('cook', ['-run=Cook', '-TargetPlatform=WindowsNoEditor', '-Map=/Game/Maps/TerraceBake',
                           '-cooksinglepackagenorefs', '-NullRHI'], '')]
        for name, args, raw in steps:
            log(f'UE4 {name}...')
            path = project / f'{name}.log'
            command = subprocess.list2cmdline([str(editor), str(project / 'SwapTest.uproject'), '-unattended', '-nop4',
                                               '-nosplash', '-UTF8Output', *args]) + raw
            code = run_logged(command, path, tick=tick and (lambda s, line, n=name: tick(s, n + ': ' + line)))
            if code:
                raise RuntimeError(f'UE4 {name} step failed ({code}). See {path}')
    finally:
        lock.close()
    cooked = project / COOKED
    if not cooked.is_file():
        raise RuntimeError(f'UE4 finished but no cooked map at {cooked}')
    return cooked


def main():
    import argparse
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('project', type=Path, help='new folder for the throwaway UE4 project')
    p.add_argument('--floor', type=Path, required=True)
    p.add_argument('--fill-floor', type=Path)
    p.add_argument('--ini', type=Path, required=True, help='navmesh ini block (DefaultEngine.ini text)')
    p.add_argument('--run', type=Path, action='append', default=[], help='helper script(s) run in the level')
    p.add_argument('--editor', type=Path, help='UE4Editor-Cmd.exe (found automatically if omitted)')
    a = p.parse_args()
    try:
        print(bake(a.project, a.floor, a.ini.read_text(), a.run, a.fill_floor, a.editor))
    except RuntimeError as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
