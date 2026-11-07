"""Stage the portable dependencies the plugin compiles, without Git or private inputs.

The files the plugin's sources include, followed from include to include, are staged. Each has to be
named in native_core_files.txt, the list of the portable files the plugin compiles; research the
plugin does not include stays out. Owned receipts permit updates and removal of unchanged staged files.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

try:
    from . import native_custom_backend_sources as backend
    from .native_test_module import automation_file, _inside
except ImportError:
    import native_custom_backend_sources as backend
    from native_test_module import automation_file, _inside

ROOT = Path(__file__).resolve().parents[1]
CORE = 'core'
TARGET = 'Source/Kadan/Private/P4MapKitCore'
RECEIPT = 'core-module-sources.json'
OWNER = 'p4mapkit-reviewed-core-v1'
LIST = 'tools/native_core_files.txt'
EXTENSIONS = {'.h', '.hpp', '.inl'}
BACKENDS = ('native_custom_backend.cpp', 'native_custom_backend_policy.cpp')


def digest(content):
    return hashlib.sha256(content).hexdigest()


def safe_name(name):
    if not isinstance(name, str) or not name or ':' in name or '\\' in name:
        raise ValueError('Invalid relative source name')
    path = PurePosixPath(name)
    if not path.parts or path.is_absolute() or '..' in path.parts or path.as_posix() != name:
        raise ValueError('Invalid relative source name')
    return name


def includes(path):
    # Preserve strings and line positions when stripping comments. Conditional
    # literal includes are retained, including inactive branches.
    token = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
    text = token.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]) if m[0].startswith(('//', '/*')) else m[0],
                     path.read_text(encoding='utf-8-sig'))
    for line in text.splitlines():
        if not re.match(r'^\s*#\s*include\b', line):
            continue
        match = re.fullmatch(r'\s*#\s*include\s*(["<])([^">]+)[">]\s*', line)
        if not match:
            raise ValueError('Nonliteral source include: ' + path.name)
        yield match[1], match[2]


def source_closure(repo=ROOT, tests=False):
    """Resolve canonical product roots; never read a stale staged module."""
    repo = Path(repo).resolve()
    core = repo / CORE
    compiler = repo / 'compiler'
    modules = [repo / 'Kadan/Source/Kadan',
               repo / 'Kadan/Source/P4BlueprintImport']
    public = [module / 'Public' for module in modules]
    # The test module's sources, and the seams a development test stage compiles into the Kadan module.
    test_module = repo / 'Kadan/Source/P4MapKitTests'
    seams = test_module / 'KadanSeams'
    test_roots = [test_module / 'Private', test_module / 'Private/Authoring', test_module / 'Private/Blueprints']
    if tests:
        public.append(test_module / 'Private')
    pending = [core / name for name in BACKENDS]
    pending += [compiler / name for name in backend.SOURCES + backend.HEADERS]
    pending.append(core / 'native_custom_backend_bits.h')
    if tests:
        pending += [path for path in sorted(test_module.rglob('*'))
                    if path.is_file() and (path.suffix in EXTENSIONS or path.suffix == '.cpp')]
    for module in modules:
        for path in sorted(module.rglob('*')):
            if path.is_file() and (path.suffix in EXTENSIONS or path.suffix == '.cpp'):
                if path.suffix == '.cpp' and automation_file(path, path.read_bytes()) and not tests:
                    continue
                pending.append(path)  # Includes UHT headers and the module PCH.
    reached = set()
    while pending:
        path = pending.pop()
        if path in reached:
            continue
        _inside(path, repo)
        path = path.resolve()
        if path in reached:
            continue
        if not path.is_relative_to(repo) or not path.is_file():
            raise ValueError('Missing or escaping canonical source')
        reached.add(path)
        module = modules[0] if path.is_relative_to(seams) else next((m for m in modules if path.is_relative_to(m)), None)
        roots = ([module / 'Private', module / 'Public'] if module else []) + public
        if module == modules[0]:
            roots.append(seams)  # a seam's include in the Kadan module, inactive outside test stages
        if not module and path.is_relative_to(test_module):
            roots = test_roots + [m / 'Private' for m in modules] + [seams] + public
        if module and path.suffix == '.cpp' and automation_file(path, path.read_bytes()):
            # Automation units compile in the test module, which sees both product modules' Private folders.
            roots += [m / 'Private' for m in modules if m != module]
        for delimiter, name in includes(path):
            safe_name(name)
            if name.endswith('.generated.h'):
                continue  # UHT, not a repository source.
            virtual = None
            if name.startswith('P4MapKitCore/'):
                virtual = core / name[len('P4MapKitCore/'):]
            elif name.startswith('KadanMaxwell/'):
                suffix = name[len('KadanMaxwell/'):]
                virtual = core / suffix if suffix == 'native_custom_backend_bits.h' else compiler / suffix
            local = path.parent / name
            candidates = [virtual] if virtual else ([local] if local.is_file() else [r / name for r in roots])
            found = {p.resolve() for p in candidates if p is not None and p.is_file()}
            if len(found) > 1:
                raise ValueError('Ambiguous repository include: ' + name)
            if found:
                dependency = found.pop()
                if not dependency.is_relative_to(repo):
                    raise ValueError('Repository include escapes source root: ' + name)
                pending.append(dependency)
            elif virtual or (delimiter == '"' and (path.is_relative_to(core) or path.is_relative_to(compiler))) \
                    or (core / name).is_file() or name.startswith('P4'):
                raise ValueError('Unresolved internal include: ' + name)
            # Remaining literals are engine/platform/standard library includes.
    return sorted(reached)


def listed(repo=ROOT):
    """The portable files the plugin compiles: native_core_files.txt, one repository path a line."""
    names = set()
    for line in (Path(repo) / LIST).read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            names.add(safe_name(line))
    return names


def source_plan(repo=ROOT, tests=False):
    repo = Path(repo).resolve()
    allowed = listed(repo)
    wanted = {}
    contents = {}
    for path in source_closure(repo, tests):
        relative = path.relative_to(repo).as_posix()
        if not (relative.startswith(CORE + '/') or relative.startswith('compiler/')):
            continue
        if relative.startswith('compiler/') and path.name not in backend.SOURCES + backend.HEADERS:
            raise ValueError('Unlisted compiler dependency: ' + relative)
        if relative not in allowed:
            raise ValueError('Portable dependency not in ' + LIST + ': ' + relative)
        content = path.read_bytes()
        contents[relative] = content
        if relative.startswith(CORE + '/'):
            wanted[path.relative_to(repo / CORE).as_posix()] = content
    adapted = backend.source_plan(repo, contents=contents)
    if wanted.keys() & adapted.keys():
        raise ValueError('Portable source collides with the fixed compiler stage')
    wanted.update(adapted)
    return wanted


def read_receipt(plugin):
    path = plugin / RECEIPT
    _inside(path, plugin)
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(data, dict) or data.get('owner') != OWNER or data.get('format') != 1 or not isinstance(data.get('files'), dict):
        raise ValueError('Invalid portable stage receipt')
    for name, value in data['files'].items():
        safe_name(name)
        if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
            raise ValueError('Invalid portable stage digest')
    return data


def write_changed(path, content):
    if not path.is_file() or path.read_bytes() != content:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return True
    return False


def write_receipt(plugin, files):
    content = (json.dumps(dict(format=1, owner=OWNER, files=files), indent=2, sort_keys=True) + '\n').encode('utf-8')
    return write_changed(plugin / RECEIPT, content)


def stage(plugin, tests=False, *, repo=ROOT):
    """Validate the list and every destination before changing one owned byte.

    Without a receipt, adopt only exact planned bytes. Unknown or excluded
    legacy files need owner review and survive refusal.
    """
    plugin = Path(plugin).absolute()
    _inside(plugin, plugin)
    plugin = plugin.resolve()
    repo = Path(repo).resolve()
    if plugin.is_relative_to(repo / 'Kadan'):
        raise ValueError('Core staging requires an isolated plugin snapshot')
    wanted = source_plan(repo, tests)
    target = plugin / TARGET
    _inside(target, plugin)
    previous = read_receipt(plugin).get('files', {})
    for path in target.rglob('*') if target.is_dir() else ():
        _inside(path, plugin)
        if not path.is_file():
            continue
        name = path.relative_to(target).as_posix()
        current = path.read_bytes()
        if name in previous:
            if digest(current) != previous[name]:
                raise ValueError('Portable stage changed outside its owner: ' + name)
        elif current != wanted.get(name):
            raise ValueError('Unowned portable stage file: ' + name)
    touched = []
    for name, content in sorted(wanted.items()):
        if write_changed(target / name, content):
            touched.append(TARGET + '/' + name)
    for name in sorted(previous.keys() - wanted.keys()):
        path = target / name
        if path.is_file():
            path.unlink()
            touched.append('removed ' + TARGET + '/' + name)
    if write_receipt(plugin, {name: digest(content) for name, content in wanted.items()}):
        touched.append(RECEIPT)
    return touched


def prepare_source_labels(plugin):
    """Verify owned bytes before release-only labeling changes anything."""
    plugin = Path(plugin).resolve()
    receipt = read_receipt(plugin)
    for name, expected in receipt.get('files', {}).items():
        path = plugin / TARGET / name
        _inside(path, plugin)
        if not path.is_file() or digest(path.read_bytes()) != expected:
            raise ValueError('Portable source changed before diagnostic labeling: ' + name)
    return receipt


def finish_source_labels(plugin, receipt, changed):
    """Refresh hashes authorized by the known label/provenance transform."""
    if not receipt:
        return
    plugin = Path(plugin).resolve()
    files = {}
    for name, previous in receipt['files'].items():
        relative = TARGET + '/' + name
        expected = changed.get(relative, previous)
        path = plugin / relative
        _inside(path, plugin)
        if digest(path.read_bytes()) != expected:
            raise ValueError('Unexpected portable diagnostic transformation: ' + name)
        files[name] = expected
    write_receipt(plugin, files)
