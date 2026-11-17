"""Build the release archive on the desktop with Unreal's BuildPlugin command.

    py tools/build_native_release.py [--out private/release-<version>]

Started from the laptop by tools/release.py build, which sends the tag's files and the source record
(private/release-source.json) first. The build refuses when the mirror's files differ from the record,
when retoc.exe or repak.exe differ from the hashes pinned in tools/release_tools.json, or when the built
P4Stubs plugin is missing. It writes Kadan-UE4-<version>.zip (-prerelease appended for a pre-release)
and SHA256SUMS into the output folder. No game data or private fixtures are staged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
from string import Template
import subprocess
import sys
import time
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE.parent))
OWNER = 'kadan-native-release-build-v1'
MODULES = {'Kadan', 'KadanShaderFormat', 'P4BlueprintImport'}
# The two third-party tools Kadan runs, shipped beside the plugin in Kadan/Tools where
# P4KadanPathDefaults looks first. Each needs its executable and its license files.
TOOLS = {'retoc': ('retoc.exe', ('LICENSE',)),
         'repak': ('repak.exe', ('LICENSE-MIT', 'LICENSE-APACHE'))}
# Their pinned SHA-256 and size, and what each is built from.
PINS = HERE / 'release_tools.json'
README = HERE / 'release_readme.txt'
# The source record tools/release.py sends, and the folders whose tracked files it lists: every
# folder the build reads.
RECORD = REPO / 'private/release-source.json'
RECORD_FORMAT = 'kadan-release-source'
SOURCE_FOLDERS = ('Kadan', 'core', 'compiler', 'own_shaders', 'navigation', 'tools')
# Where tools/stubs/regenerate.py --build leaves the built P4Stubs plugin (its default --host).
STUBS = REPO / 'private/stubs/Plugins/P4Stubs'
STUB_MODULES = ('P4StubsCore', 'Carrot4', 'AkAudio', 'EasyParticle', 'UIParticle', 'SwitchRuntimeSettings')
STUB_REBUILD = 'py -3.11 tools/stubs/regenerate.py --build'


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


def archive_name(version, prerelease):
    """The published name of the archive, as the releases on GitHub name it."""
    return 'Kadan-UE4-' + version + ('-prerelease' if prerelease else '') + '.zip'


def plugin_version(repo):
    return json.loads((Path(repo) / 'Kadan/Kadan.uplugin').read_text(encoding='utf-8-sig'))['VersionName']


def read_source_record(path):
    """The source record tools/release.py wrote. Refuses a missing or malformed one."""
    path = Path(path)
    if not path.is_file():
        raise ValueError('No source record at ' + str(path) + ': start the release from the laptop with '
                         'py -3.11 tools/release.py build, which writes and sends it')
    try:
        record = json.loads(path.read_text(encoding='utf-8'))
    except ValueError as error:
        raise ValueError('The source record ' + str(path) + ' is not JSON: run tools/release.py build again') from error
    if not isinstance(record, dict):
        record = {}
    files, version = record.get('files'), record.get('kadan_version')
    if record.get('format') != RECORD_FORMAT or \
            not isinstance(record.get('commit'), str) or not re.fullmatch(r'[0-9a-f]{40}', record['commit']) or \
            not isinstance(version, str) or not re.fullmatch(r'[0-9A-Za-z.+-]+', version) or \
            record.get('tag') != 'v' + version or not isinstance(record.get('prerelease'), bool) or \
            not isinstance(files, dict) or not files:
        raise ValueError('The source record ' + str(path) + ' is incomplete: run tools/release.py build again')
    for name, digest in files.items():
        parts = Path(name).parts
        if not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest) or '\\' in name or \
                not parts or parts[0] not in SOURCE_FOLDERS or '..' in parts or Path(name).is_absolute():
            raise ValueError('The source record ' + str(path) + ' lists a file outside the release folders: ' + name)
    return record


def check_source(record, repo):
    """Refuse a mirror whose files differ from the tag's: a listed file missing or with other bytes."""
    repo = Path(repo)
    missing, changed = [], []
    for name, digest in sorted(record['files'].items()):
        path = repo / name
        if not path.is_file():
            missing.append(name)
        elif sha256(path) != digest:
            changed.append(name)
    if missing or changed:
        listed = ['missing ' + name for name in missing] + ['changed ' + name for name in changed]
        raise ValueError('The mirror is not the content of tag {} (commit {}): {} of its files differ, {}{}. Run '
                         'tools/release.py build again from a clean checkout of the tag; it sends the files.'.format(
                             record['tag'], record['commit'][:12], len(listed), ', '.join(listed[:5]),
                             ', ...' if len(listed) > 5 else ''))
    if plugin_version(repo) != record['kadan_version']:
        raise ValueError('Kadan/Kadan.uplugin in the mirror is not version ' + record['kadan_version'] +
                         ': run tools/release.py build again')
    return record


def pinned_tools(path=PINS):
    return json.loads(Path(path).read_text(encoding='utf-8'))['tools']


def release_tools(folders, pins=None):
    """The tool files to ship, as (source, member below Kadan/). Refuses a missing executable or license,
    and an executable whose SHA-256 or size is not the one pinned in tools/release_tools.json."""
    if set(folders) != set(TOOLS):
        raise ValueError('Expected the folders of ' + ' and '.join(sorted(TOOLS)))
    pins = pinned_tools() if pins is None else pins
    files = []
    for name, (executable, licenses) in sorted(TOOLS.items()):
        folder = Path(folders[name])
        for file in (executable,) + licenses:
            source = folder / file
            if not source.is_file() or source.stat().st_size == 0:
                raise ValueError('Missing ' + name + ' file for the release: ' + str(source))
            if file == executable:
                pin = pins[name]
                if pin['file'] != executable:
                    raise ValueError('tools/release_tools.json names ' + pin['file'] + ' for ' + name + ', not ' + executable)
                digest, size = sha256(source), source.stat().st_size
                if (digest, size) != (pin['sha256'], pin['bytes']):
                    raise ValueError('{} has SHA-256 {} ({} bytes), not the pinned {} ({} bytes) in tools/release_tools.json: '
                                     'put the pinned build there, or, to ship another build, change its entry in '
                                     'tools/release_tools.json in a commit before tagging.'.format(
                                         source, digest, size, pin['sha256'], pin['bytes']))
            # The layout the 0.1.0 test archive used, so an update replaces the same files.
            member = 'Tools/' + (file if file == executable else 'licenses/' + name + '/' + file)
            files.append((source, member))
    return files


def stub_files(folder, build_id=None):
    """The built P4Stubs plugin's files to ship, as (source, member): its descriptor, module list and
    DLLs, never its PDBs or Source. Refuses a plugin that is not built, not enabled by default, or built
    for another Unreal build than Kadan."""
    folder = Path(folder)
    descriptor = folder / 'P4Stubs.uplugin'
    binary = folder / 'Binaries/Win64'
    mapping = binary / 'UE4Editor.modules'
    if not descriptor.is_file() or not mapping.is_file():
        raise ValueError('No built P4Stubs plugin at ' + str(folder) + ': build it on this desktop with ' +
                         STUB_REBUILD + ', or name the built plugin with --stubs')
    try:
        data = json.loads(descriptor.read_text(encoding='utf-8-sig'))
        installed = json.loads(mapping.read_text(encoding='utf-8-sig'))
    except ValueError as error:
        raise ValueError('The P4Stubs descriptor or module list in ' + str(folder) + ' is not JSON: rebuild it with ' +
                         STUB_REBUILD) from error
    rows = data.get('Modules', [])
    if sorted(row.get('Name', '') for row in rows) != sorted(STUB_MODULES) or \
            any((row.get('Type'), row.get('LoadingPhase')) != ('Runtime', 'PreDefault') for row in rows) or \
            data.get('CanContainContent') is not False:
        raise ValueError(str(descriptor) + ' does not declare exactly the six runtime modules of the game types: '
                         'regenerate and rebuild it with ' + STUB_REBUILD)
    if data.get('EnabledByDefault') is not True:
        raise ValueError(str(descriptor) + ' is not enabled by default: it was written before the generator set '
                         'EnabledByDefault. Regenerate and rebuild it with ' + STUB_REBUILD)
    category = json.loads((REPO / 'Kadan/Kadan.uplugin').read_text(encoding='utf-8-sig')).get('Category')
    if data.get('Category') != category:
        raise ValueError('{} has the category {!r}, Kadan/Kadan.uplugin {!r}: regenerate and rebuild it with {}'.format(
            descriptor, data.get('Category'), category, STUB_REBUILD))
    if installed.get('Modules') != {name: 'UE4Editor-' + name + '.dll' for name in STUB_MODULES}:
        raise ValueError(str(mapping) + ' does not list the six P4Stubs modules: rebuild it with ' + STUB_REBUILD)
    if build_id is not None and installed.get('BuildId') != build_id:
        raise ValueError('P4Stubs in {} was built for Unreal build {}, Kadan for {}: rebuild it on this desktop with {}'.format(
            folder, installed.get('BuildId'), build_id, STUB_REBUILD))
    files = [(descriptor, 'P4Stubs/P4Stubs.uplugin'), (mapping, 'P4Stubs/Binaries/Win64/UE4Editor.modules')]
    for name in sorted(STUB_MODULES):
        path = binary / ('UE4Editor-' + name + '.dll')
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError('Missing built P4Stubs module ' + str(path) + ': rebuild it with ' + STUB_REBUILD)
        files.append((path, 'P4Stubs/Binaries/Win64/' + path.name))
    return files


def readme_text(record, pins=None):
    """README.txt at the archive's root, from tools/release_readme.txt."""
    pins = pinned_tools() if pins is None else pins
    tools = '\n'.join('    {}: {}. License: {}, in Kadan/Tools/licenses/{}.'.format(
        name, pins[name]['built_from'], pins[name]['license'], name) for name in sorted(TOOLS))
    return Template(README.read_text(encoding='utf-8')).substitute(
        version=record['kadan_version'] + (' (pre-release)' if record['prerelease'] else ''),
        commit=record['commit'], tag=record['tag'], tools=tools)


def build_record(record, build_id):
    """Kadan/BUILD.json: the commit and tag the archive was built from, machine-readable."""
    return json.dumps(dict(version=record['kadan_version'], tag=record['tag'], commit=record['commit'],
                           prerelease=record['prerelease'], unreal_build_id=build_id), indent=1) + '\n'


def write_sums(archive):
    """SHA256SUMS beside the archive, in the format sha256sum -c reads."""
    archive = Path(archive)
    sums = archive.with_name('SHA256SUMS')
    sums.write_text(sha256(archive) + '  ' + archive.name + '\n', encoding='ascii', newline='\n')
    return sums


def archive_runtime(plugin, archive, tools=None, stubs=None, record=None):
    """Write the archive: Kadan/ and, given them, its tools, P4Stubs/, README.txt and Kadan/BUILD.json.
    The audit runs on the finished archive before it takes its name. Returns each member's SHA-256."""
    files = runtime_files(plugin)
    extra = [(source, 'Kadan/' + member) for source, member in release_tools(tools)] if tools is not None else []
    written = {}
    if stubs is not None or record is not None:
        build_id = json.loads((plugin / 'Binaries/Win64/UE4Editor.modules').read_text(encoding='utf-8'))['BuildId']
        if stubs is not None:
            extra += stub_files(stubs, build_id)
        if record is not None:
            written = {'README.txt': readme_text(record), 'Kadan/BUILD.json': build_record(record, build_id)}
    temporary = archive.with_suffix('.pending.zip')
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for member, text in written.items():
                output.writestr(member, text.encode('utf-8'))
            for path in files:
                member = 'Kadan/' + path.relative_to(plugin).as_posix()
                output.write(path, member)
            for source, member in extra:
                output.write(source, member)
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
    members = {'Kadan/' + path.relative_to(plugin).as_posix(): sha256(path) for path in files}
    members.update({member: sha256(source) for source, member in extra})
    members.update({member: hashlib.sha256(text.encode('utf-8')).hexdigest() for member, text in written.items()})
    return members


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, help='output folder (default private/release-<version>)')
    parser.add_argument('--engine', type=Path, default=Path(os.environ.get('P4_UE_ROOT', 'C:/UE_4.26')))
    parser.add_argument('--timeout', type=int, default=2400)
    parser.add_argument('--max-parallel-actions', type=int, default=4)
    parser.add_argument('--source-record', type=Path, default=RECORD,
                        help='the source record tools/release.py sent (default private/release-source.json)')
    parser.add_argument('--stubs', type=Path, default=STUBS,
                        help='the built P4Stubs plugin folder (default private/stubs/Plugins/P4Stubs)')
    parser.add_argument('--retoc', type=Path, default=HERE.parent / 'private/retoc',
                        help='folder with retoc.exe and its LICENSE')
    parser.add_argument('--repak', type=Path, default=HERE.parent / 'private/repak',
                        help='folder with repak.exe, LICENSE-MIT and LICENSE-APACHE')
    args = parser.parse_args()
    tools = dict(retoc=args.retoc, repak=args.repak)
    try:
        source = check_source(read_source_record(args.source_record), REPO)
        release_tools(tools)
        stub_files(args.stubs)
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
    folder = (args.out or REPO / 'private' / ('release-' + source['kadan_version'])).resolve()
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
                  production_dependency_files=production_dependency_files,
                  source=dict(commit=source['commit'], tag=source['tag'], version=source['kadan_version'],
                              prerelease=source['prerelease'], files=len(source['files'])))
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
        archive = folder / archive_name(source['kadan_version'], source['prerelease'])
        members = archive_runtime(packaged, archive, tools, args.stubs, source)
        sums = write_sums(archive)
        # The stub plugin's build does not trim source paths as Kadan's does. Report what its DLLs keep.
        record['stub_source_label_audits'] = {}
        for path, member in stub_files(args.stubs):
            if path.suffix == '.dll':
                audit = labels.audit_dll_source_labels(path, prefixes)
                record['stub_source_label_audits'][path.name] = audit
                if not audit['ok']:
                    print('Notice: {} keeps {} absolute path strings, which ship in the release.'.format(
                        member, audit['counts']['utf8'] + audit['counts']['utf16']), flush=True)
        record.update(status='built', exit=0, archive=archive.name,
                      archive_sha256=sha256(archive), members=members)
        print('Packaged release:', archive, flush=True)
        print('Checksums:', sums, flush=True)
        return 0
    except (OSError, ValueError, subprocess.TimeoutExpired):
        record.update(status='failed')
        raise
    finally:
        marker.write_text(json.dumps(record, indent=2), encoding='utf-8')
        lock.close()


if __name__ == '__main__':
    raise SystemExit(main())
