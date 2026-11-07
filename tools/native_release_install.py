"""Install a native release into a clean plugin slot and bind its exact bytes.

The target project may contain the game's declaration modules. The canonical
authoring plugin slot must be absent; existing plugins are never replaced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import declaration_archive

PLUGIN = 'Kadan'
# The release's modules and the type and loading phase each declares. KadanShaderFormat, the
# game's GLSL_SWITCH shader format, loads before the engine's shader types are registered.
MODULE_ROWS = {
    'Kadan': ('Editor', 'Default'),
    'KadanShaderFormat': ('UncookedOnly', 'PostConfigInit'),
    'P4BlueprintImport': ('Editor', 'Default'),
}
MODULES = tuple(MODULE_ROWS)
MEMBERS = {
    'Kadan.uplugin', 'Config/DefaultKadan.ini', 'Binaries/Win64/UE4Editor.modules',
    *('Binaries/Win64/UE4Editor-' + name + '.dll' for name in MODULES),
}
# The container tools the release ships beside the plugin (79de30cf). They are part of the archive but not
# of the runtime that is installed and bound here.
TOOLS = {
    'Tools/retoc.exe', 'Tools/repak.exe', 'Tools/licenses/retoc/LICENSE',
    'Tools/licenses/repak/LICENSE-MIT', 'Tools/licenses/repak/LICENSE-APACHE',
}


def require_module_rows(rows):
    """Refuse a descriptor without exactly the release modules, each with its own type and phase."""
    names = [row.get('Name') if isinstance(row, dict) else None for row in rows]
    if len(names) != len(MODULES) or set(names) != set(MODULES):
        raise ValueError('Archive module identity is not canonical')
    for row in rows:
        if (row.get('Type'), row.get('LoadingPhase')) != MODULE_ROWS[row['Name']]:
            raise ValueError('Release module has the wrong type or loading phase: ' + row['Name'])
    return names


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def _archive_members(archive):
    if __package__:
        from .native_release_publish import audit_archive
    else:
        from native_release_publish import audit_archive
    with zipfile.ZipFile(archive) as source:
        runtime = {PLUGIN + '/' + name for name in MEMBERS}
        expected = runtime | {PLUGIN + '/' + name for name in TOOLS}
        names = source.namelist()
        if len(names) != len(set(names)) or set(names) not in (runtime, expected):
            raise ValueError('Release archive must contain exactly the canonical runtime files')
        for member in source.infolist():
            limit = 1024 * 1024 if member.filename.endswith(('.uplugin', '.modules', '.ini')) else 256 * 1024 * 1024
            if not 0 < member.file_size <= limit:
                raise ValueError('Invalid release member size')
        descriptor = json.loads(source.read(PLUGIN + '/Kadan.uplugin'))
        require_module_rows(descriptor['Modules'])
        manifest = json.loads(source.read(PLUGIN + '/Binaries/Win64/UE4Editor.modules'))
        mapping = manifest['Modules']
        if not isinstance(manifest.get('BuildId'), str) or not manifest['BuildId']:
            raise ValueError('Release archive has no Unreal build identity')
        if mapping != {name: 'UE4Editor-' + name + '.dll' for name in MODULES}:
            raise ValueError('Archive module identity is not canonical')
        result = {}
        for name in sorted(MEMBERS):
            member = source.getinfo(PLUGIN + '/' + name)
            if member.file_size <= 0 or member.file_size > 256 * 1024 * 1024:
                raise ValueError('Invalid release member size')
            value = hashlib.sha256()
            with source.open(member) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    value.update(block)
            result[name] = value.hexdigest()
    errors = audit_archive(archive)
    if errors:
        raise ValueError('Release archive refused: ' + '; '.join(errors))
    return result


def archive_members(archive):
    try:
        return _archive_members(archive)
    except (zipfile.BadZipFile, KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError('Malformed release archive or module identity') from error


def bundled_declarations(archive, build_id=None):
    with zipfile.ZipFile(archive) as source:
        has_declarations = declaration_archive.DESCRIPTOR in source.namelist()
    return declaration_archive.inspect_declaration_archive(archive, build_id) if has_declarations else None


def verify(archive, project):
    """Refuse an altered archive, plugin, extra files, or redirected plugin path."""
    archive, project = Path(archive).resolve(), Path(project).resolve()
    archive_hash = digest(archive)
    if not project.is_file() or project.suffix != '.uproject':
        raise ValueError('A saved Unreal project is required')
    expected = archive_members(archive)
    if digest(archive) != archive_hash:
        raise ValueError('Release changed during verification')
    plugins = project.parent / 'Plugins'
    target = plugins / PLUGIN
    if plugins.is_symlink() or plugins.resolve() != plugins:
        raise ValueError('Project Plugins folder is redirected')
    if not target.is_dir() or target.is_symlink() or target.resolve().parent != plugins.resolve():
        raise ValueError('The canonical release plugin is absent or redirected')
    files = [one for one in target.rglob('*') if one.is_file()]
    if any(one.is_symlink() or not one.resolve().is_relative_to(target.resolve()) for one in target.rglob('*')):
        raise ValueError('The installed release contains a filesystem link')
    actual = {one.relative_to(target).as_posix(): digest(one) for one in files}
    if actual != expected:
        raise ValueError('The installed plugin does not equal the release archive')
    # A duplicate module elsewhere would make the loaded DLL identity ambiguous.
    for name in MODULES:
        for path in plugins.rglob('UE4Editor-' + name + '.dll'):
            if path.resolve().parent != (target / 'Binaries/Win64').resolve():
                raise ValueError('Another project plugin contains the release module: ' + name)
    manifest = json.loads((target / 'Binaries/Win64/UE4Editor.modules').read_text(encoding='utf-8'))
    build_id = manifest.get('BuildId')
    if not isinstance(build_id, str) or not build_id:
        raise ValueError('Installed release has no Unreal build identity')
    result = dict(schema='kadan-installed-release-v1', archive_sha256=archive_hash,
                  plugin_members=actual, build_id=build_id)
    return result


def require_loaded_runtime(identity, binding, project):
    """Bind independent native mapped-image observations without publishing local paths."""
    if not isinstance(identity, dict) or identity.get('valid') is not True or type(identity.get('format')) is not int or identity.get('format') != 1:
        raise ValueError('Native loaded release identity was not verified')
    rows = identity.get('modules')
    names = set(MODULES)
    if not isinstance(rows, list) or len(rows) != len(names) or any(not isinstance(row, dict) for row in rows) or \
            {row.get('module') for row in rows} != names:
        raise ValueError('Native loaded module identity is incomplete or duplicated')
    plugin = Path(project).resolve().parent / 'Plugins/Kadan'
    if plugin.parent.resolve() != plugin.parent or plugin.resolve() != plugin:
        raise ValueError('Loaded release plugin folder is redirected')
    binary = plugin / 'Binaries/Win64'
    result = {}
    for row in rows:
        name = row['module']
        filename = 'UE4Editor-' + name + '.dll'
        size = row.get('bytes')
        paths = [row.get(key) for key in ('loaded_file', 'image_path', 'manifest_file')]
        if any(not isinstance(path, str) or not path or '\x00' in path for path in paths):
            raise ValueError('Native loaded release image path is missing')
        if row.get('image_verified') is not True or not isinstance(size, int) or isinstance(size, bool) or size <= 0 or \
                Path(row.get('loaded_file', '')).resolve() != (binary / filename).resolve() or \
                Path(row['image_path']).resolve() != (binary / filename).resolve() or \
                not (binary / filename).is_file() or size != (binary / filename).stat().st_size or \
                Path(row.get('manifest_file', '')).resolve() != (binary / 'UE4Editor.modules').resolve() or \
                row.get('sha256') != binding['plugin_members']['Binaries/Win64/' + filename] or \
                row.get('build_id') != binding['build_id']:
            raise ValueError('An actual loaded module does not equal the release archive')
        result[name] = dict(sha256=row['sha256'], bytes=size, build_id=row['build_id'], image_verified=True)
    return dict(schema='kadan-loaded-release-v1', archive_sha256=binding['archive_sha256'], modules=result)


def require_loaded_receipt(receipt, binding):
    if not isinstance(receipt, dict) or receipt.get('schema') != 'kadan-loaded-release-v1' or \
            receipt.get('archive_sha256') != binding['archive_sha256']:
        raise ValueError('The editor did not identify the accepted release archive')
    modules = receipt.get('modules')
    names = set(MODULES)
    if not isinstance(modules, dict) or set(modules) != names:
        raise ValueError('The editor did not identify every actual loaded module')
    for name, row in modules.items():
        size = row.get('bytes') if isinstance(row, dict) else None
        if not isinstance(row, dict) or row.get('image_verified') is not True or not isinstance(size, int) or isinstance(size, bool) or size <= 0 or \
                row.get('build_id') != binding['build_id'] or \
                row.get('sha256') != binding['plugin_members']['Binaries/Win64/UE4Editor-' + name + '.dll']:
            raise ValueError('The editor loaded module bytes outside the accepted release')
    return receipt


def install(archive, project):
    archive, project = Path(archive).resolve(), Path(project).resolve()
    if not project.is_file() or project.suffix != '.uproject':
        raise ValueError('A saved Unreal project is required')
    expected = archive_members(archive)
    plugins = project.parent / 'Plugins'
    if plugins.is_symlink() or plugins.resolve() != plugins:
        raise ValueError('Project Plugins folder is redirected')
    target = plugins / PLUGIN
    if target.exists() or target.is_symlink():
        raise ValueError('The canonical plugin slot must be absent before release installation')
    declaration_archive._collision([plugins], own=plugins / 'P4Stubs')
    plugins.mkdir(parents=True, exist_ok=True)
    for name in MODULES:
        if any(plugins.rglob('UE4Editor-' + name + '.dll')):
            raise ValueError('Another plugin already contains release module: ' + name)
    temporary = Path(tempfile.mkdtemp(prefix='.kadan-release-', dir=str(plugins)))
    try:
        with zipfile.ZipFile(archive) as source:
            for name in expected:
                output = temporary / name
                output.parent.mkdir(parents=True, exist_ok=True)
                with source.open(PLUGIN + '/' + name) as stream, output.open('wb') as destination:
                    shutil.copyfileobj(stream, destination, 1024 * 1024)
        if {name: digest(temporary / name) for name in expected} != expected:
            raise ValueError('Release changed during installation')
        # Rename refuses a concurrently created destination on Windows.
        temporary.rename(target)
    finally:
        if temporary.exists():
            if temporary.is_symlink() or temporary.resolve().parent != plugins.resolve():
                raise ValueError('Temporary release folder was redirected')
            shutil.rmtree(temporary)
    return verify(archive, project)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--editor-only', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        receipt = (verify if args.verify_only else install)(args.archive, args.project)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    print(json.dumps(dict(receipt, setup_message=
        'Kadan is installed. This project needs game type declarations before importing maps.'), indent=1))


if __name__ == '__main__':
    main()
