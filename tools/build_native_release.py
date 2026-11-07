"""Package the current native plugin with Unreal's BuildPlugin command.

Run on the authorized Unreal build machine. The reusable output is owned by this task; no game data
or private fixtures are staged. Acceptance is performed on the resulting archive.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
OWNER = 'kadan-native-release-build-v1'
MODULES = {'Kadan', 'KadanShaderFormat', 'P4BlueprintImport'}
# The two third-party tools Kadan runs, shipped beside the plugin in Kadan/Tools where
# P4KadanPathDefaults looks first. Each needs its executable and its license files.
TOOLS = {'retoc': ('retoc.exe', ('LICENSE',)),
         'repak': ('repak.exe', ('LICENSE-MIT', 'LICENSE-APACHE'))}


def compiler_from_log(log):
    """Use the toolchain actually selected by UE4.26, including newer MSVCs."""
    selected = re.findall(r'Using Visual Studio \d+ [^\r\n]+? toolchain \(([^\r\n]+?)\) and Windows', log)
    paths = {str(Path(path) / 'bin/Hostx64/x64/cl.exe') for path in selected}
    if len(paths) != 1:
        raise ValueError('BuildPlugin did not identify one actual MSVC toolchain')
    return Path(paths.pop())


def sha256(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def runtime_files(plugin):
    """Use actual module mappings and refuse missing or escaping binary names."""
    descriptor = plugin / 'Kadan.uplugin'
    data = json.loads(descriptor.read_text(encoding='utf-8'))
    names = [row['Name'] for row in data['Modules']]
    if len(names) != len(MODULES) or set(names) != MODULES:
        raise ValueError('Expected the canonical production modules')
    if __package__:
        from .native_release_install import require_module_rows
    else:
        from native_release_install import require_module_rows
    require_module_rows(data['Modules'])
    binary = plugin / 'Binaries/Win64'
    mapping = binary / 'UE4Editor.modules'
    installed = json.loads(mapping.read_text(encoding='utf-8'))['Modules']
    if set(installed) != MODULES:
        raise ValueError('Module map includes missing or non-production modules')
    config = plugin / 'Config/DefaultKadan.ini'
    if not config.is_file() or config.stat().st_size == 0:
        raise ValueError('Missing plugin config with the old script package redirect')
    files = [descriptor, mapping, config]
    for name in sorted(names):
        filename = installed[name]
        if not isinstance(filename, str) or Path(filename).name != filename or filename != 'UE4Editor-' + name + '.dll':
            raise ValueError('Invalid module DLL path')
        path = binary / filename
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError('Missing compiled production module: ' + name)
        files.append(path)
    return files


def release_tools(folders):
    """The tool files to ship, as (source, member below Kadan/). Refuses a missing executable or license."""
    if set(folders) != set(TOOLS):
        raise ValueError('Expected the folders of ' + ' and '.join(sorted(TOOLS)))
    files = []
    for name, (executable, licenses) in sorted(TOOLS.items()):
        folder = Path(folders[name])
        for file in (executable,) + licenses:
            source = folder / file
            if not source.is_file() or source.stat().st_size == 0:
                raise ValueError('Missing ' + name + ' file for the release: ' + str(source))
            # The layout the 0.1.0 test archive used, so an update replaces the same files.
            member = 'Tools/' + (file if file == executable else 'licenses/' + name + '/' + file)
            files.append((source, member))
    return files


def archive_runtime(plugin, archive, declarations=None, tools=None):
    files = runtime_files(plugin)
    extra = release_tools(tools) if tools is not None else []
    if declarations is not None:
        from declaration_archive import inspect_declaration_archive
        build_id = json.loads((plugin / 'Binaries/Win64/UE4Editor.modules').read_text(encoding='utf-8'))['BuildId']
        # The generated SDK is a private acceptance input. Validate its editor
        # identity without including any game-derived files in this archive.
        inspect_declaration_archive(declarations, build_id)
    temporary = archive.with_suffix('.pending.zip')
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for path in files:
                member = 'Kadan/' + path.relative_to(plugin).as_posix()
                output.write(path, member)
            for source, member in extra:
                output.write(source, 'Kadan/' + member)
        if __package__:
            from .native_release_publish import audit_archive
        else:
            from native_release_publish import audit_archive
        errors = audit_archive(temporary)
        if errors:
            raise ValueError('Packaged archive refused: ' + '; '.join(errors))
        os.replace(temporary, archive)
    finally:
        if temporary.exists():
            temporary.unlink()
    members = {path.relative_to(plugin).as_posix(): sha256(path) for path in files}
    members.update({member: sha256(source) for source, member in extra})
    return members


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--engine', type=Path, default=Path(os.environ.get('P4_UE_ROOT', 'C:/UE_4.26')))
    parser.add_argument('--timeout', type=int, default=2400)
    parser.add_argument('--max-parallel-actions', type=int, default=4)
    parser.add_argument('--declarations', type=Path,
                        help='optional private test SDK to check against this editor; never bundled')
    parser.add_argument('--retoc', type=Path, default=HERE.parent / 'private/retoc',
                        help='folder with retoc.exe and its LICENSE')
    parser.add_argument('--repak', type=Path, default=HERE.parent / 'private/repak',
                        help='folder with repak.exe, LICENSE-MIT and LICENSE-APACHE')
    args = parser.parse_args()
    tools = dict(retoc=args.retoc, repak=args.repak)
    try:
        release_tools(tools)
    except ValueError as error:
        parser.error(str(error))
    if not 1 <= args.timeout <= 7200:
        parser.error('Timeout must be between 1 and 7200 seconds')
    if not 1 <= args.max_parallel_actions <= 64:
        parser.error('Compiler action limit must be between 1 and 64')
    # Validate the real integrated portable closure before clearing or staging
    # any owned output. Synthetic dependency fixtures cannot prove this tree.
    sys.path.insert(0, str(HERE))
    from native_core_sources import source_plan
    production_dependency_files = len(source_plan())
    folder = args.out.resolve()
    marker = folder / 'native-release-build.json'
    if folder.exists() and any(folder.iterdir()):
        if not marker.is_file() or json.loads(marker.read_text(encoding='utf-8')).get('owner') != OWNER:
            parser.error('Release output belongs to another task')
    # BuildPlugin clears only this explicitly owned sibling directory. Source is
    # staged elsewhere under the same owner, never inside its package output.
    packaged = folder / 'p'
    staged = folder / 's/Kadan'
    for target in (packaged, staged):
        if not target.resolve().is_relative_to(folder) or target.is_symlink():
            parser.error('Owned release output path escapes its folder')
    folder.mkdir(parents=True, exist_ok=True)
    record = dict(owner=OWNER, status='started', automation_tests=False, commands=[],
                  production_dependency_files=production_dependency_files)
    marker.write_text(json.dumps(record), encoding='utf-8')
    sys.path.insert(0, str(HERE))
    from dev_build import sync
    from native_test_module import assert_production_sources
    import ue_bake
    import native_release_source_labels as labels
    import native_release_build_scope as scope
    environment = dict(os.environ, P4_MAPKIT_TESTS='0')
    lock = ue_bake._lock()
    try:
        # Labeling rewrites the staged sources, so a stage left by an earlier build would
        # fail the staging checks. Every build stages afresh into its owned folder.
        if staged.parent.exists():
            shutil.rmtree(staged.parent)
        sync(staged, tests=False)
        assert_production_sources(staged)
        record['source_labels'] = labels.label_first_party_sources(staged)
        # Canonical UAT packages this production config through its own filter.
        (staged / 'Config/FilterPlugin.ini').write_text(
            '[FilterPlugin]\n/Config/DefaultKadan.ini\n', encoding='utf-8')
        environment, record['build_scope'] = scope.prepare(folder, args.engine,
                                                          args.max_parallel_actions, environment)
        project = packaged / 'HostProject/HostProject.uproject'
        commands = scope.release_commands(args.engine, staged, packaged, args.max_parallel_actions)
        with (folder / 'package-build.log').open('w', encoding='utf-8', errors='replace') as log:
            for index, command in enumerate(commands):
                if index == 2:
                    log.flush()
                    compiler = compiler_from_log((folder / 'package-build.log').read_text(encoding='utf-8'))
                    if not compiler.is_file():
                        raise ValueError('The selected MSVC compiler was not found')
                    prefixes = [folder, Path(__file__).resolve().parents[1], args.engine.resolve()]
                    probe = labels.probe_trim_arguments(compiler, folder / 'compiler-probe', prefixes, environment)
                    if not probe['ok']:
                        raise ValueError('The actual release compiler cannot produce relative source labels')
                    record.update(compiler_probe=probe, compiler_sha256=sha256(compiler))
                    # Kadan.Build.cs selects P4MapKitPCH.h, just as in dev_build. It
                    # applies precise floating point semantics before engine inlines.
                    # Changing compiler arguments also rebuilds that PCH, so its source
                    # labels are trimmed together with the production translation units.
                    # MSVC appends _CL_ after its response-file arguments. The
                    # inherited toolchain environment remains intact; only this
                    # child adds the proven diagnostic source-label trims.
                    environment['_CL_'] = (environment.get('_CL_', '') + ' ' +
                        subprocess.list2cmdline(labels.trim_arguments(prefixes))).strip()
                    record['compiler_environment_arguments'] = environment['_CL_']
                record['commands'].append(list(command))
                # One retry preserves existing objects after a transient start
                # failure. A repeated failure is reported for the lane owner.
                for attempt in range(2):
                    log.flush()
                    start = (folder / 'package-build.log').stat().st_size
                    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                            stdin=subprocess.DEVNULL, env=environment, timeout=args.timeout)
                    log.flush()
                    with (folder / 'package-build.log').open('rb') as written:
                        written.seek(start)
                        added = written.read().decode('utf-8', 'replace')
                    if not result.returncode:
                        break
                    if attempt == 0 and 'A conflicting instance of UnrealBuildTool is already running' in added:
                        time.sleep(20)
                        continue
                    if attempt == 0 and index != 2 and any(code in added for code in ('error D8000', 'error C1356', 'error C1060')):
                        time.sleep(60)
                        continue
                    break
                if result.returncode:
                    record.update(status='failed', exit=result.returncode)
                    return result.returncode
                if index == 2:
                    record['canonical_compile'] = scope.require_parallel_build(added, args.max_parallel_actions)
            log.flush()
            if compiler_from_log((folder / 'package-build.log').read_text(encoding='utf-8')) != compiler:
                raise ValueError('The release rebuild selected a different compiler')
        # The final compile and package are both Epic's canonical BuildPlugin
        # command. Manual UBT above exported actions only to identify its toolchain.
        # Require its packaged production DLLs to equal its retained host bytes.
        host_plugin = project.parent / 'Plugins/Kadan'
        record['pch_audit'] = scope.audit_pch(host_plugin,
            HERE.parent / 'Kadan/Source/Kadan/Private/P4MapKitPCH.h')
        record['source_label_audits'] = {}
        for path in runtime_files(host_plugin):
            if path.suffix == '.dll':
                audit = labels.audit_dll_source_labels(path, prefixes)
                record['source_label_audits'][path.name] = audit
                if not audit['ok']:
                    raise ValueError('Release module contains absolute source labels: ' + path.name)
            target = packaged / path.relative_to(host_plugin)
            if path.suffix != '.uplugin' and (not target.is_file() or sha256(target) != sha256(path)):
                raise ValueError('Canonical BuildPlugin package differs from its compiled host: ' + path.name)
        archive = folder / 'Kadan-UE4.zip'
        members = archive_runtime(packaged, archive, args.declarations, tools)
        record.update(status='built', exit=0, archive=archive.name,
                      archive_sha256=sha256(archive), members=members, acceptance_required=True)
        print('Packaged native candidate:', archive, flush=True)
        return 0
    except (OSError, ValueError, subprocess.TimeoutExpired):
        record.update(status='failed')
        raise
    finally:
        marker.write_text(json.dumps(record, indent=2), encoding='utf-8')
        lock.close()


if __name__ == '__main__':
    raise SystemExit(main())
