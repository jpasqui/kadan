"""Build the authoring plugin for development with Unreal's own build tool.

    py -3.11 tools/dev_build.py [--host <project folder>] [--tests] [--install <project file> ...]

The plugin's sources are copied into <host>/Plugins/Kadan (only files that changed, so
the build tool sees what changed) and UnrealBuildTool builds the editor target of the host project.
A change to one source file compiles that file and links. The host is the project the editor is
then run on. A project that also needs the stub classes or the game's content gets the built
plugin with --install (the declared module DLLs and their descriptors are copied).

--tests builds the separate P4MapKitTests development module. Production modules omit test units.

The release archive is not made by this. It is built once, strictly, and the acceptance run is made
on that archive (docs/NATIVE_REWRITE.md).
"""
import argparse
import filecmp
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

try:
    from .kadan_plugin import plugin_slot
except ImportError:
    from kadan_plugin import plugin_slot

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
SOURCE = REPO / 'Kadan'
OWNER = 'p4mapkit-dev-host'
CORE = 'Source/Kadan/Private/P4MapKitCore'
MADE = ('Binaries', 'Intermediate')


BINARIES = 'Plugins/Kadan/Binaries/Win64'
LAYOUT = 'separate-tests-module-v1'


def module_names(tests=False):
    return ('Kadan', 'KadanShaderFormat', 'P4BlueprintImport') + (('P4MapKitTests',) if tests else ())


def install_artifacts(plugin):
    """Validate all declared runtime artifacts before replacing any installed file."""
    descriptor = plugin / 'Kadan.uplugin'
    names = [row['Name'] for row in json.loads(descriptor.read_text(encoding='utf-8'))['Modules']]
    if len(names) != len(set(names)) or set(names) not in (set(module_names()), set(module_names(True))):
        raise ValueError('Unexpected development module descriptor')
    binary = plugin / 'Binaries/Win64'
    mapping = binary / 'UE4Editor.modules'
    installed = json.loads(mapping.read_text(encoding='utf-8'))['Modules']
    if set(installed) != set(names):
        raise ValueError('Development module map disagrees with its descriptor')
    files = []
    for name in names:
        filename = 'UE4Editor-' + name + '.dll'
        path = binary / filename
        if installed[name] != filename or not path.is_file() or path.stat().st_size == 0:
            raise ValueError('Missing or invalid development module DLL: ' + name)
        files.append(path)
        # The DLL names its symbols by file name only, so a profiler finds them only beside the copy the editor loads.
        symbols = path.with_suffix('.pdb')
        if symbols.is_file():
            files.append(symbols)
    config = plugin / 'Config/DefaultKadan.ini'
    return files + [mapping, descriptor] + ([config] if config.is_file() else [])


def retire_legacy_module_map(plugin):
    """Retire only obsolete entries after a successful Kadan build."""
    mapping = plugin / 'Binaries/Win64/UE4Editor.modules'
    document = json.loads(mapping.read_text(encoding='utf-8'))
    installed = document['Modules']
    names = [row['Name'] for row in json.loads((plugin / 'Kadan.uplugin').read_text(encoding='utf-8'))['Modules']]
    stale = set(installed) - set(names)
    if not stale:
        install_artifacts(plugin)
        return False
    allowed = {'P4MapKitAuthoring'}
    if 'P4MapKitTests' not in names:
        allowed.add('P4MapKitTests')
    if len(names) != len(set(names)) or set(names) not in (set(module_names()), set(module_names(True))) \
            or not stale.issubset(allowed) or set(installed) != set(names) | stale:
        raise ValueError('Unexpected obsolete module-map entries; the map was preserved')
    for name in stale:
        if installed[name] != 'UE4Editor-' + name + '.dll':
            raise ValueError('Unexpected obsolete module-map entry; the map was preserved')
    for name in names:
        filename = 'UE4Editor-' + name + '.dll'
        path = mapping.parent / filename
        if installed[name] != filename or not path.is_file() or path.stat().st_size == 0:
            raise ValueError('The Kadan build has no valid module DLL: ' + name)
    for name in stale:
        del installed[name]
    temporary = mapping.with_name(mapping.name + '.kadan-update')
    with temporary.open('xb') as stream:
        stream.write((json.dumps(document, indent=2) + '\n').encode('utf-8'))
    os.replace(temporary, mapping)
    install_artifacts(plugin)
    return True


def runtime_snapshot(plugin):
    """Keep tiny descriptors only when all declared modules have known installed DLLs."""
    descriptor = plugin / 'Kadan.uplugin'
    modules = plugin / 'Binaries/Win64/UE4Editor.modules'
    if not descriptor.is_file() or not modules.is_file():
        return None
    try:
        installed = json.loads(modules.read_text(encoding='utf-8'))['Modules']
        names = [row['Name'] for row in json.loads(descriptor.read_text(encoding='utf-8'))['Modules']]
        files = [modules.parent / installed[name] for name in names]
        if not all(path.is_file() and path.parent == modules.parent for path in files):
            return None
    except (KeyError, ValueError, TypeError):
        return None
    return dict(descriptor=descriptor.read_bytes(), modules=modules.read_bytes(),
                dlls={path: hashlib.sha256(path.read_bytes()).digest() for path in files})


def restore_runtime_descriptor(plugin, snapshot, staged_descriptor):
    """A failed compile retains a runnable prior descriptor only if its DLLs stayed unchanged."""
    descriptor = plugin / 'Kadan.uplugin'
    if not snapshot or not descriptor.is_file() or descriptor.read_bytes() != staged_descriptor:
        return False
    modules = plugin / 'Binaries/Win64/UE4Editor.modules'
    if not modules.is_file() or modules.read_bytes() != snapshot['modules']:
        return False
    if any(not path.is_file() or hashlib.sha256(path.read_bytes()).digest() != digest
           for path, digest in snapshot['dlls'].items()):
        return False
    descriptor.write_bytes(snapshot['descriptor'])
    modules.write_bytes(snapshot['modules'])
    return True


def install(built, project, *, migrate_plugin_name=False):
    """Puts a built plugin into another project (one that has the stub classes or the game's content).
    A file is replaced, never written in place: the projects' binaries can be hard links of each other."""
    source = Path(built) / 'Plugins/Kadan'
    files = install_artifacts(source)
    target = plugin_slot(project, migrate=migrate_plugin_name)
    if source.resolve() == target.resolve():
        return
    for path in files:
        destination = target / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_file() and filecmp.cmp(path, destination, shallow=False):
            continue
        if destination.exists():
            destination.unlink()
        shutil.copyfile(path, destination)


def sync(plugin, tests=False):
    """Copies changed files and removes files that are gone. Returns what it touched."""
    sys.path.insert(0, str(HERE))
    from native_test_module import automation_file, seam_targets, stage as stage_tests
    touched, incoming_tests = [], {}
    seams = seam_targets()
    wanted = set()
    for source in SOURCE.rglob('*'):
        if source.is_file():
            relative = source.relative_to(SOURCE)
            if relative.as_posix().startswith('Source/P4MapKitTests/'):
                continue  # The test staging owner validates and admits this module separately.
            if relative.as_posix().startswith('Source/P4BlueprintImport/'):
                continue  # install_module.py stages this module.
            wanted.add(relative)
            content = source.read_bytes()
            if relative.as_posix().startswith('Source/Kadan/') and automation_file(source, content):
                incoming_tests[relative.as_posix()] = content
                continue
            target = plugin / relative
            if not target.is_file() or target.read_bytes() != content:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
                touched.append(relative.as_posix())
    sys.path.insert(0, str(HERE))
    from native_core_sources import stage
    touched += stage(plugin, tests=tests)
    for target in list(plugin.rglob('*')):
        if not target.is_file():
            continue
        relative = target.relative_to(plugin)
        if relative.parts[0] in MADE or relative.as_posix().startswith(CORE + '/') or relative in wanted \
                or relative.as_posix().startswith('Source/P4BlueprintImport/') or relative.name in ('blueprint-module-sources.json', 'core-module-sources.json'):
            continue
        if relative.as_posix().startswith('Source/P4MapKitTests/') or relative.name == 'test-module-sources.json' \
                or relative.as_posix() in seams:
            continue  # The test staging owner keeps or removes these.
        target.unlink()
        touched.append('removed ' + relative.as_posix())
    sys.path.insert(0, str(REPO / 'tools'))
    from install_module import stage as stage_blueprints
    touched += stage_blueprints(plugin)
    touched += stage_tests(plugin, enabled=tests, fresh_product_sources=True, incoming_tests=incoming_tests)['touched']
    return touched


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', type=Path, default=REPO / 'private/mapkit-native-workflow/dev-host')
    parser.add_argument('--tests', action='store_true')
    parser.add_argument('--migrate-plugin-name', action='store_true', help='explicitly migrate only the selected host and install projects from the legacy name to Kadan')
    parser.add_argument('--engine', type=Path,
                        default=Path(os.environ.get('P4_UE_ROOT') or 'C:/Program Files/Epic Games/UE_4.26'))
    parser.add_argument('--timeout', type=float, default=2400.0, help='seconds the build may take')
    parser.add_argument('--max-parallel-actions', type=int, default=4, help='maximum simultaneous compiler actions')
    parser.add_argument('--install', type=Path, nargs='*', default=[], help='project files to put the built plugin into')
    args = parser.parse_args()
    if not 1 <= args.max_parallel_actions <= 64:
        parser.error('--max-parallel-actions must be between 1 and 64')
    host = args.host.resolve()
    marker = host / 'dev-host.json'
    projects = sorted(host.glob('*.uproject')) if host.is_dir() else []
    if host.exists() and any(host.iterdir()) and not marker.is_file() and not projects:
        parser.error('The host folder holds something else: ' + str(host))
    host.mkdir(parents=True, exist_ok=True)
    if projects:
        project = projects[0]
    else:
        project = host / 'DevHost.uproject'
        project.write_text(json.dumps(dict(FileVersion=3, EngineAssociation='4.26', Plugins=[
            dict(Name=name, Enabled=True) for name in ('Kadan', 'PythonScriptPlugin', 'EditorScriptingUtilities')]),
            indent=2), encoding='utf-8')
    state = json.loads(marker.read_text(encoding='utf-8')) if marker.is_file() else {}
    plugin = plugin_slot(project, migrate=args.migrate_plugin_name)
    prior_runtime = runtime_snapshot(plugin)
    touched = sync(plugin, tests=args.tests)
    staged_descriptor = (plugin / 'Kadan.uplugin').read_bytes()
    mode = 'tests' if args.tests else 'plain'
    # The build tool keeps one build per engine unless told otherwise, so a one-file build in one mirror
    # waited for a whole build in another (11 s of compiling in a 1,418 s build, 2026-10-16). The engine is
    # an installed build, so a build writes only its own host's files: each host builds at once, with its
    # own log instead of the engine's shared one.
    command = [str(args.engine / 'Engine/Binaries/DotNET/UnrealBuildTool.exe'), 'UE4Editor', 'Win64', 'Development',
               '-Project=' + str(project), '-NoMutex', '-Log=' + str(host / 'ubt.log'), '-NoHotReloadFromIDE',
               '-MaxParallelActions=' + str(args.max_parallel_actions)]
    stubs = host / 'Plugins/P4Stubs'
    # Kadan's setup installs the game's declarations into the engine's own Plugins folder, binaries only.
    # The build tool cannot build a project while either copy is enabled without its sources.
    if any((folder / 'P4Stubs.uplugin').is_file() and not any((folder / 'Source').glob('*/*.Build.cs'))
           for folder in (stubs, args.engine / 'Engine/Plugins/P4Stubs')):
        # Runtime-only game declarations stay enabled in the editor project; UBT builds only product source.
        command.append('-DisablePlugin=P4Stubs')
    if prior_runtime and (host / 'Plugins/P4Stubs').is_dir():
        # Other plugins of the host are built by their own tools. Only this module is built here.
        command += ['-Module=' + name for name in module_names(args.tests)]
    if state.get('mode') != mode or state.get('layout') != LAYOUT:
        command.append('-NoUBTMakefiles')    # the build tool does not see an environment variable change
    environment = dict(os.environ)
    environment['P4_MAPKIT_TESTS'] = '1' if args.tests else '0'
    log = host / 'dev-build.log'
    start = time.time()
    with open(log, 'w', encoding='utf-8', errors='replace') as stream:
        try:
            code = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                  env=environment, timeout=args.timeout,
                                  cwd=str(args.engine / 'Engine/Source')).returncode
        except subprocess.TimeoutExpired:
            code = -1
    seconds = time.time() - start
    text = log.read_text(encoding='utf-8', errors='replace')
    lines = text.splitlines()
    errors = [line for line in lines if ' error ' in line or 'error:' in line.lower() or ': fatal ' in line]
    compiled = sum(1 for line in lines if line.strip().endswith('.cpp') and '[' in line)
    if code == 0:
        if retire_legacy_module_map(plugin):
            print('Retired obsolete module-map entries; their DLLs were preserved.')
        marker.write_text(json.dumps(dict(owner=OWNER, mode=mode, layout=LAYOUT, project=project.name)), encoding='utf-8')
    elif restore_runtime_descriptor(plugin, prior_runtime, staged_descriptor):
        print('Restored the prior installed module descriptors; unchanged DLLs remain runnable.')
    print('{} in {:.0f} s: {} file(s) changed, {} compiled, exit {}'.format(
        'Built' if code == 0 else ('Timed out' if code == -1 else 'Failed'), seconds, len(touched), compiled, code))
    for line in errors[:40]:
        print('  ' + line.strip()[:300])
    if code and not errors:
        for line in lines[-15:]:
            print('  ' + line[:300])
    print('log:', log)
    if code == 0:
        for project in args.install:
            install(host, project.resolve(), migrate_plugin_name=args.migrate_plugin_name)
            print('installed into', project.parent.name)
    return 0 if code == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
