"""Read and install the fixed declaration bundle used beside the editor archive.

No private research input or executable build tool is required by this boundary.
Installation accepts ordinary archive members and refuses existing module identities.
"""
from dataclasses import dataclass
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import struct
import uuid
import zipfile


MODULES = ('P4StubsCore', 'Carrot4', 'AkAudio', 'EasyParticle', 'UIParticle', 'SwitchRuntimeSettings')
EDITOR_MODULES = ('Kadan', 'KadanShaderFormat', 'P4BlueprintImport')
DESCRIPTOR = 'P4Stubs/P4Stubs.uplugin'
MANIFEST = 'P4Stubs/Binaries/Win64/UE4Editor.modules'
DLLS = {name: 'P4Stubs/Binaries/Win64/UE4Editor-' + name + '.dll' for name in MODULES}
MEMBERS = frozenset((DESCRIPTOR, MANIFEST, *DLLS.values()))
MAX_BYTES = 256 * 1024 * 1024
ORIGINAL_DEFAULT_PATH = b'C:/home/Project/carrrot4u/Saved/Carrot4/Saved'


def _hash(data):
    return hashlib.sha256(data).hexdigest()


def _file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def _same_path(left, right):
    return os.path.normcase(str(left)) == os.path.normcase(str(right))


def _unredirected(path, message):
    """Allow ordinary Windows short names while retaining link ancestry checks."""
    original = Path(os.path.abspath(path))
    for current in (original, *original.parents):
        try:
            status = current.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(status.st_mode) or getattr(status, 'st_file_attributes', 0) & 0x400:
            raise ValueError(message)
    return original.resolve()


def _canonical_project(project):
    # Preserve the spelling supplied by the caller until ancestry is checked;
    # resolving first would erase a junction before we could refuse it.
    project = Path(os.path.abspath(project))
    if not project.is_file() or project.suffix != '.uproject':
        raise ValueError('exact existing project required')
    project = _unredirected(project, 'clean project ancestry must be ordinary canonical directories')
    plugins = project.parent / 'Plugins'
    _unredirected(plugins, 'canonical Plugins directory cannot be a junction or symlink')
    _ordinary(project, project.parent)
    if plugins.exists():
        _ordinary(plugins, project.parent, directory=True)
    return project


def _ordinary(path, base, directory=False):
    message = 'declaration path cannot use a reparse point'
    path = _unredirected(path, message)
    base = _unredirected(base, message)
    try:
        relative = path.relative_to(base)
    except ValueError:
        raise ValueError('declaration path escaped canonical project')
    current = base
    for part in relative.parts:
        current = current / part
        status = current.lstat()
        if (stat.S_ISLNK(status.st_mode) or getattr(status, 'st_file_attributes', 0) & 0x400 or
                not _same_path(current.resolve(), current)):
            raise ValueError('declaration path cannot use a reparse point: ' + str(relative))
        if current != path and not stat.S_ISDIR(status.st_mode):
            raise ValueError('declaration ancestor is not a directory')
    status = path.lstat()
    if directory:
        if not stat.S_ISDIR(status.st_mode):
            raise ValueError('declaration directory is not ordinary')
    elif not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
        raise ValueError('declaration member must be an ordinary unlinked file')


def _privacy(data, label):
    # DLLs may contain UTF-16 constants. Engine-relative Source/.../Private paths are
    # legitimate; private research roots and absolute machine paths are not.
    values = [value.decode('ascii') for value in re.findall(rb'[\x20-\x7e]{5,}', data)]
    values += [value.decode('utf-16-le') for value in re.findall(rb'(?:[\x20-\x7e]\x00){5,}', data)]
    allowance = ORIGINAL_DEFAULT_PATH.decode('ascii')
    allowed_count = 0
    for value in values:
        # Whole-string equality preserves only the two original default facts.
        if label == DLLS['Carrot4'] and value == allowance:
            allowed_count += 1
            if allowed_count > 2:
                raise ValueError('original default path exceeds its qualified source-row count')
            continue
        lower = value.lower()
        if re.search(r'[a-z]:(?:\\|/(?!/))|\\\\[a-z0-9_.-]+[\\/]', lower) or any(token in lower for token in (
                'private/blueprints', 'private\\blueprints', 'private/mapkit', 'private\\mapkit',
                '.claude/', '.claude\\', '.codex/', '.codex\\', '/users/', '\\users\\')):
            raise ValueError('local path or private provenance in declaration member: ' + label)


def _json(data, label):
    if len(data) > 128 * 1024:
        raise ValueError('declaration metadata exceeds bound: ' + label)
    _privacy(data, label)
    return json.loads(data.decode('utf-8-sig'))


def _pe(data, label):
    if len(data) < 128 or data[:2] != b'MZ':
        raise ValueError('declaration DLL is not a PE image: ' + label)
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if offset < 64 or offset > 1024 * 1024 or offset + 26 > len(data) or data[offset:offset + 4] != b'PE\x00\x00':
        raise ValueError('declaration PE header invalid: ' + label)
    machine, sections = struct.unpack_from('<HH', data, offset + 4)
    optional, flags = struct.unpack_from('<HH', data, offset + 20)
    if (machine != 0x8664 or not 1 <= sections <= 96 or not flags & 0x2000 or optional < 112 or
            offset + 24 + optional + 40 * sections > len(data) or struct.unpack_from('<H', data, offset + 24)[0] != 0x20b):
        raise ValueError('declaration DLL must be a bounded Win64 image: ' + label)
    _privacy(data, label)


@dataclass(frozen=True)
class DeclarationArchive:
    archive: Path
    sha256: str
    build_id: str
    members: tuple

    def binding(self):
        return dict(schema='p4-fixed-declarations-archive/v1', archive_sha256=self.sha256,
                    build_id=self.build_id, modules=list(MODULES),
                    original_default_path_allowance=dict(module='Carrot4', property='SaveLoadConfig.HostMountPath',
                        value=ORIGINAL_DEFAULT_PATH.decode('ascii'), qualified_source_rows=2),
                    members={name: dict(sha256=digest, bytes=size) for name, digest, size in self.members})


def inspect_declaration_archive(archive, expected_build_id=None):
    archive = Path(archive).resolve()
    digest = _file_hash(archive)
    with zipfile.ZipFile(archive) as opened:
        infos = opened.infolist()
        names = [item.filename for item in infos]
        editor_members = {'Kadan/Kadan.uplugin', 'Kadan/Config/DefaultKadan.ini', 'Kadan/Binaries/Win64/UE4Editor.modules',
                         *('Kadan/Binaries/Win64/UE4Editor-' + name + '.dll' for name in EDITOR_MODULES)}
        expected = MEMBERS | editor_members if any(name.startswith('Kadan/') for name in names) else MEMBERS
        if len(names) != len(expected) or set(names) != expected or len({name.casefold() for name in names}) != len(expected):
            raise ValueError('declaration archive must have exactly its descriptor, module manifest and six DLLs')
        if sum(item.file_size for item in infos) > MAX_BYTES:
            raise ValueError('declaration archive exceeds bound')
        for item in infos:
            mode = stat.S_IFMT(item.external_attr >> 16)
            if item.flag_bits & 1 or mode not in (0, stat.S_IFREG) or item.file_size > 64 * 1024 * 1024:
                raise ValueError('declaration archive member is not an ordinary bounded file')
        descriptor = _json(opened.read(DESCRIPTOR), DESCRIPTOR)
        if any(field in descriptor for field in ('PreBuildSteps', 'PostBuildSteps', 'LocalizationTargets', 'AdditionalPluginDirectories')):
            raise ValueError('fixed declarations cannot include build hooks or extra payload roots')
        rows = descriptor.get('Modules', [])
        if (len(rows) != 6 or {row.get('Name') for row in rows} != set(MODULES) or
                any(row.get('Type') != 'Runtime' or row.get('LoadingPhase') != 'PreDefault'
                    for row in rows) or descriptor.get('CanContainContent') is not False):
            raise ValueError('fixed declarations require the six original runtime identities and no content')
        mapping = _json(opened.read(MANIFEST), MANIFEST)
        expected = {name: 'UE4Editor-' + name + '.dll' for name in MODULES}
        build_id = mapping.get('BuildId')
        if (mapping.get('Modules') != expected or not isinstance(build_id, str) or not build_id or
                len(build_id) > 128 or not re.fullmatch(r'[A-Za-z0-9._-]+', build_id)):
            raise ValueError('fixed declaration module mapping or build ID invalid')
        if expected_build_id is not None and expected_build_id != build_id:
            raise ValueError('declarations and editor archive have different build IDs')
        members = []
        for name in sorted(MEMBERS):
            data = opened.read(name)
            if name.endswith('.dll'):
                _pe(data, name)
            members.append((name, _hash(data), len(data)))
    if _file_hash(archive) != digest:
        raise ValueError('declaration archive changed while inspected')
    return DeclarationArchive(archive, digest, build_id, tuple(members))


def package_declaration_archive(plugin, archive, expected_build_id, original_defaults_proof=None, prefixes=()):
    """Package only rebuilt runtime outputs after the shared source-label audit."""
    message = 'declaration packaging requires an ordinary source and separate output'
    plugin = _unredirected(plugin, message)
    archive = _unredirected(archive, message)
    if plugin.name != 'P4Stubs' or archive == plugin or plugin in archive.parents:
        raise ValueError('declaration packaging requires an ordinary source and separate output')
    _ordinary(plugin, plugin.parent, directory=True)
    helper = Path(__file__).resolve().parents[1] / 'tools/native_release_source_labels.py'
    spec = importlib.util.spec_from_file_location('p4_declaration_source_labels', helper)
    labels = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(labels)
    source = {}
    for name in sorted(MEMBERS):
        path = plugin.parent / name
        _ordinary(path, plugin.parent)
        if name.endswith('.dll'):
            audit = labels.audit_dll_source_labels(path, prefixes,
                original_defaults_proof=original_defaults_proof if name == DLLS['Carrot4'] else None)
            if audit.get('ok') is not True:
                raise ValueError('declaration source-label audit refused: ' + name)
        source[name] = (_file_hash(path), path.stat().st_size)
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_name('.p4-declarations-archive-' + uuid.uuid4().hex)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as opened:
            for name in sorted(MEMBERS):
                data = (plugin.parent / name).read_bytes()
                if (_hash(data), len(data)) != source[name]:
                    raise ValueError('rebuilt declarations changed while packaging')
                entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                entry.create_system = 3
                entry.external_attr = (stat.S_IFREG | 0o644) << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                opened.writestr(entry, data)
        inspect_declaration_archive(temporary, expected_build_id)
        if any((_file_hash(plugin.parent / name), (plugin.parent / name).stat().st_size) != value
               for name, value in source.items()):
            raise ValueError('rebuilt declarations changed before archive publication')
        os.replace(temporary, archive)
    finally:
        if temporary.exists():
            temporary.unlink()
    return inspect_declaration_archive(archive, expected_build_id)


def _editor_binding(project):
    plugin = project.parent / 'Plugins/Kadan'
    descriptor_path = plugin / 'Kadan.uplugin'
    manifest_path = plugin / 'Binaries/Win64/UE4Editor.modules'
    descriptor = _json(descriptor_path.read_bytes(), 'installed editor descriptor')
    modules = descriptor.get('Modules', [])
    if len(modules) != len(EDITOR_MODULES) or {row.get('Name') for row in modules} != set(EDITOR_MODULES):
        raise ValueError('canonical editor bundle must be installed first')
    manifest = _json(manifest_path.read_bytes(), 'installed editor module map')
    if manifest.get('Modules') != {name: 'UE4Editor-' + name + '.dll' for name in EDITOR_MODULES}:
        raise ValueError('installed editor module mapping differs')
    files = [descriptor_path, manifest_path] + [plugin / 'Binaries/Win64' / manifest['Modules'][name] for name in EDITOR_MODULES]
    for path in files:
        _ordinary(path, project.parent)
    return dict(build_id=manifest['BuildId'], files={str(path.relative_to(project.parent)).replace('\\', '/'): _file_hash(path) for path in files})


def _collision(roots, own=None):
    for root in roots:
        root = Path(root)
        if not root.exists():
            continue
        for path in root.rglob('*.uplugin'):
            if own is not None and path.parent == own:
                continue
            descriptor = json.loads(path.read_text(encoding='utf-8-sig'))
            for row in descriptor.get('Modules', []):
                name = row.get('Name', '')
                if any(name.casefold() == expected.casefold() for expected in MODULES):
                    raise ValueError('installed original module identity collision: ' + name)
        filenames = {path.name.casefold() for path in root.rglob('*.dll')
                     if own is None or own not in path.parents}
        for module in MODULES:
            if ('UE4Editor-' + module + '.dll').casefold() in filenames:
                raise ValueError('installed original DLL identity collision: ' + module)


def _project_collision(project, settings):
    for row in settings.get('Modules', []):
        name = row.get('Name', '')
        if any(name.casefold() == expected.casefold() for expected in MODULES):
            raise ValueError('project original module identity collision: ' + name)
    if settings.get('AdditionalPluginDirectories'):
        raise ValueError('additional project plugin directories require an explicit collision census')
    _collision([project.parent / 'Binaries'])


def install_declaration_archive(info, project, expected_editor_build_id=None, engine_plugins=None):
    if not isinstance(info, DeclarationArchive):
        raise TypeError('inspect the declaration archive first')
    project = _canonical_project(project)
    current = inspect_declaration_archive(info.archive, info.build_id)
    if current != info:
        raise ValueError('declaration archive differs from inspected binding')
    editor = _editor_binding(project)
    if editor['build_id'] != info.build_id or (expected_editor_build_id is not None and expected_editor_build_id != info.build_id):
        raise ValueError('fixed declarations do not match the installed editor build')
    plugins = project.parent / 'Plugins'
    if (plugins / 'P4Stubs').exists():
        raise ValueError('fixed declaration plugin destination already exists')
    _collision([plugins] + ([engine_plugins] if engine_plugins is not None else []))
    project_bytes = project.read_bytes()
    settings = json.loads(project_bytes.decode('utf-8-sig'))
    _project_collision(project, settings)
    entries = settings.get('Plugins', [])
    declarations = [row for row in entries if row.get('Name', '').casefold() == 'p4stubs']
    if len(declarations) > 1 or any(row.get('Name') != 'P4Stubs' or row.get('Enabled') is not True for row in declarations):
        raise ValueError('project has an ambiguous or disabled fixed declaration plugin entry')
    if not declarations:
        settings['Plugins'] = entries + [dict(Name='P4Stubs', Enabled=True)]
    after_project = (json.dumps(settings, indent=1) + '\n').encode('utf-8')
    before = dict(project_sha256=_hash(project_bytes), editor=editor)
    plugins.mkdir(parents=True, exist_ok=True)
    stage = plugins / ('.p4-declarations-' + uuid.uuid4().hex)
    if stage.resolve().parent != plugins.resolve():
        raise ValueError('declaration staging must remain inside the exact project plugin directory')
    stage.mkdir()
    made = []
    installed = False
    project_temporary = project.with_name('.p4-declarations-project-' + uuid.uuid4().hex)
    try:
        with zipfile.ZipFile(info.archive) as opened:
            for name, digest, size in info.members:
                data = opened.read(name)
                if len(data) != size or _hash(data) != digest:
                    raise ValueError('archive member changed after inspection')
                destination = stage / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
                made.append(destination)
        if _file_hash(info.archive) != info.sha256 or project.read_bytes() != project_bytes or _editor_binding(project) != editor:
            raise ValueError('archive, project or editor binding changed during declaration installation')
        (stage / 'P4Stubs').rename(plugins / 'P4Stubs')
        installed = True
        project_temporary.write_bytes(after_project)
        if project.read_bytes() != project_bytes:
            raise ValueError('project changed before plugin activation; installed declarations retained')
        os.replace(project_temporary, project)
    finally:
        if project_temporary.exists():
            project_temporary.unlink()
        if not installed:
            for path in reversed(made):
                path.unlink()
        for path in sorted(stage.rglob('*'), key=lambda item: len(item.parts), reverse=True):
            if path.is_dir():
                path.rmdir()
        stage.rmdir()
    if _editor_binding(project) != editor:
        raise ValueError('installed editor bundle changed during declaration installation')
    installed_hashes = {name: _file_hash(plugins / name) for name, digest, size in info.members}
    if installed_hashes != {name: digest for name, digest, size in info.members}:
        raise ValueError('installed declaration member hash differs')
    return dict(schema='p4-fixed-declarations-install/v1', archive=info.binding(), before=before,
                after=dict(project_sha256=_file_hash(project), editor=editor, declarations=installed_hashes))


def verify_installed_declarations(info, project, engine_plugins=None):
    """Read-only complete archive/install binding, suitable before every stage."""
    if not isinstance(info, DeclarationArchive):
        raise TypeError('inspect the declaration archive first')
    project = _canonical_project(project)
    if inspect_declaration_archive(info.archive, info.build_id) != info:
        raise ValueError('declaration archive differs from inspected binding')
    editor = _editor_binding(project)
    if editor['build_id'] != info.build_id:
        raise ValueError('declarations differ from the installed editor build')
    plugins = project.parent / 'Plugins'
    own = plugins / 'P4Stubs'
    _ordinary(own, project.parent, directory=True)
    expected = {name: (digest, size) for name, digest, size in info.members}
    actual = set()
    allowed_directories = {'P4Stubs/Binaries', 'P4Stubs/Binaries/Win64'}
    for path in own.rglob('*'):
        name = path.relative_to(plugins).as_posix()
        if path.is_dir():
            _ordinary(path, project.parent, directory=True)
            if name not in allowed_directories:
                raise ValueError('unexpected declaration directory: ' + name)
        else:
            _ordinary(path, project.parent)
            actual.add(name)
    if actual != MEMBERS:
        raise ValueError('installed declarations must contain exactly their eight archive members')
    for name, (digest, size) in expected.items():
        path = plugins / name
        if path.stat().st_size != size or _file_hash(path) != digest:
            raise ValueError('installed declaration binding changed: ' + name)
    _collision([plugins] + ([engine_plugins] if engine_plugins is not None else []), own=own)
    settings = json.loads(project.read_bytes().decode('utf-8-sig'))
    _project_collision(project, settings)
    rows = [row for row in settings.get('Plugins', []) if row.get('Name', '').casefold() == 'p4stubs']
    if len(rows) != 1 or rows[0].get('Name') != 'P4Stubs' or rows[0].get('Enabled') is not True:
        raise ValueError('installed declarations must have one exact enabled project entry')
    return dict(schema='p4-fixed-declarations-installed/v1', archive=info.binding(),
                project_sha256=_file_hash(project), editor=editor,
                declarations={name: digest for name, (digest, size) in expected.items()})


def bind_loaded_declarations(info, project, observation):
    """Bind the native actual-loaded observation; this does not qualify reflection."""
    if not isinstance(info, DeclarationArchive):
        raise TypeError('inspect the declaration archive first')
    project = _canonical_project(project)
    installed = verify_installed_declarations(info, project)
    if (not isinstance(observation, dict) or type(observation.get('format')) is not int or
            observation['format'] != 1 or observation.get('valid') is not True):
        raise ValueError('native loaded declaration observation refused')
    rows = observation.get('modules')
    if (not isinstance(rows, list) or len(rows) != 6 or any(not isinstance(row, dict) for row in rows) or
            {row.get('module') for row in rows} != set(MODULES)):
        raise ValueError('native loaded declaration observation must contain all six exact identities')
    editor = installed['editor']
    if editor['build_id'] != info.build_id:
        raise ValueError('loaded declarations differ from the installed editor build')
    expected = {name: (digest, size) for name, digest, size in info.members}
    for name, (digest, size) in expected.items():
        path = project.parent / 'Plugins' / name
        if not path.is_file() or path.stat().st_size != size or _file_hash(path) != digest:
            raise ValueError('installed declaration binding changed before loaded observation')
    def path_key(value):
        if not isinstance(value, str) or not value or '\x00' in value:
            raise ValueError('native loaded declaration path missing')
        return os.path.normcase(os.path.abspath(value)).replace('\\', '/')
    manifest_path = project.parent / 'Plugins' / MANIFEST
    for row in rows:
        digest, size = expected[DLLS[row['module']]]
        if (row.get('build_id') != info.build_id or row.get('sha256') != digest or
                row.get('image_verified') is not True or
                type(row.get('bytes')) is not int or row['bytes'] != size or
                path_key(row.get('loaded_file')) != path_key(str(project.parent / 'Plugins' / DLLS[row['module']])) or
                path_key(row.get('image_path')) != path_key(row.get('loaded_file')) or
                path_key(row.get('manifest_file')) != path_key(str(manifest_path))):
            raise ValueError('native loaded declaration identity differs from installed archive: ' + row['module'])
    return dict(schema='p4-fixed-declarations-loaded/v1', archive=info.binding(),
                project_sha256=_file_hash(project), editor=editor,
                modules=sorted(rows, key=lambda row: row['module']), image_qualified=True, reflection_qualified=False)
