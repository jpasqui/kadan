"""Development test-module source staging; never writes runtime DLLs/module maps.

Call after canonical authoring/core/Blueprint source staging, before UBT, in an
isolated source snapshot. Canonical source locations remain stable during the
coordinated migration. Development test sources move to P4MapKitTests there;
release source stages omit them completely. Private typed DLL-link seams must
be qualified in the coherent native build before enabling this module in a host.
"""
import hashlib
import json
from pathlib import Path
import re
import stat

CANONICAL = Path(__file__).resolve().parents[1] / 'Kadan'
MODULE = 'P4MapKitTests'
PRODUCTS = ('Kadan', 'P4BlueprintImport')
UNTESTED = ('KadanShaderFormat',)
PRODUCT_TEST_DEFINE = 'if (System.Environment.GetEnvironmentVariable("P4_MAPKIT_TESTS") == "1") PrivateDefinitions.Add("P4_MAPKIT_TESTS=1");'
# Only a development test stage gives the Kadan module its test seams (P4KadanTestSeams.h). The
# canonical Build.cs never holds the line, so release stages and release builds have none.
SEAMS_DEFINE = '        PrivateDefinitions.Add("P4_KADAN_TEST_SEAMS=1");  // development test stage only'
# The seams' own sources live with the tests (Source/P4MapKitTests/KadanSeams). They call the Kadan
# module's internals, so a development test stage compiles them into that module.
SEAMS = 'KadanSeams'
SEAMS_TARGET = 'Source/Kadan/Private/'
OWNER = 'p4mapkit-separate-tests-v1'
# Exact first owned Build.cs scaffold before its coordinated Projects dependency.
# This is a bounded source migration, never a wildcard allowance for stale files.
LEGACY_PENDING_SCAFFOLD = {
    'P4MapKitTests.Build.cs.pending': 'ea8873f5ad9a7ce716980a72efda9c42034c4da6d4059696ecc18a64a579824b',
    'Private/P4MapKitTestsModule.cpp.pending': '0517847afc72bf1fb87dc9e00bf26ed4558ea5f1d1c9bbc69006ada1ae9468e3',
    'Private/P4MapKitTestsPCH.h.pending': '502d439288703a0701fcdf9aeae0e5cd447dc29fc99e6801b42014ba5b28361b',
}


def seam_targets(canonical=None):
    """Where a development test stage puts each seam source in the Kadan module."""
    folder = Path(canonical or CANONICAL) / 'Source' / MODULE / SEAMS
    return {SEAMS_TARGET + path.name for path in folder.iterdir() if path.is_file()} if folder.is_dir() else set()


def automation_file(path, content):
    """Fail closed for embedded/renamed automation instead of shipping it quietly."""
    if path.suffix != '.cpp':
        return False
    named = path.name.endswith(('Automation.cpp', 'Tests.cpp'))
    text = content.decode('utf-8-sig')
    declared = bool(re.search(r'\bIMPLEMENT_[A-Z_]*AUTOMATION_TEST\s*\(', text))
    if declared and not named:
        raise ValueError('Unclassified embedded automation unit: ' + path.name)
    return named


def _inside(path, root):
    # Keep every original spelling until links have been checked. Normalizing the root
    # first would hide a junction above the caller-selected snapshot directory.
    path, root = Path(path), Path(root)
    original_path, original_root = path.absolute(), root.absolute()
    aliases = dict.fromkeys((original_path, *original_path.parents, original_root, *original_root.parents))
    def alias(part):
        try:
            info = part.lstat()
        except FileNotFoundError:   # a planned output may not exist yet
            return False
        # Python 3.11 has no Path.is_junction; Windows marks all reparse aliases.
        return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) &
                                                getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400))
    # Windows can expand an ordinary 8.3 spelling during resolve(). Compare physical paths,
    # while retaining the original spellings above for the link/reparse checks.
    if any(alias(part) for part in aliases) or not path.resolve().is_relative_to(root.resolve()) or (path.is_file() and path.stat().st_nlink != 1):
        raise ValueError('Test source staging path escapes its snapshot')


def stage(plugin, enabled=False, fresh_product_sources=False, incoming_tests=None):
    """Split an isolated staged source tree after validating all targets.

    Product test lifecycle blocks become inactive because only Tests defines the
    flag. Its module owns the existing pacing implementation and lifecycle. Files
    are replaced only when bytes change; existing runtime binaries are untouched.
    Set fresh_product_sources after a complete canonical product-source refresh,
    including a refresh that removed its last test unit.
    """
    plugin = Path(plugin)
    _inside(plugin, plugin.absolute())
    plugin = plugin.resolve()
    if plugin.is_relative_to(CANONICAL.resolve()):
        raise ValueError('Test-module staging requires an isolated source snapshot')
    descriptor = plugin / 'Kadan.uplugin'
    _inside(descriptor, plugin)
    source = plugin / 'Source'
    _inside(source, plugin)
    if not source.is_dir() or not descriptor.is_file():
        raise ValueError('Missing staged plugin sources or descriptor')
    data = json.loads(descriptor.read_text(encoding='utf-8'))
    # Modules without automation units (the GLSL_SWITCH shader format) are not split.
    untested = [row for row in data['Modules'] if row.get('Name') in UNTESTED]
    modules = [row for row in data['Modules'] if row.get('Name') not in (MODULE,) + UNTESTED]
    if len(modules) != len(PRODUCTS) or {row.get('Name') for row in modules} != set(PRODUCTS):
        raise ValueError('Exactly both canonical product modules must be staged first')
    receipt_path = plugin / 'test-module-sources.json'
    _inside(receipt_path, plugin)
    receipt = json.loads(receipt_path.read_text(encoding='utf-8')) if receipt_path.is_file() else {}
    if receipt and receipt.get('owner') != OWNER:
        raise ValueError('Test source snapshot belongs to another staging owner')
    previous = receipt.get('files', {})
    if not isinstance(previous, dict) or any(not name.startswith('Source/' + MODULE + '/') or Path(name).is_absolute() or '..' in Path(name).parts or ':' in name or '\\' in name or not isinstance(digest, str) or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest) for name, digest in previous.items()):
        raise ValueError('Invalid test source receipt')
    # The seams have a list of their own, which stagers from before them do not read.
    seams = seam_targets()
    previous_seams = receipt.get('seams', {})
    if not isinstance(previous_seams, dict) or any(not name.startswith(SEAMS_TARGET) or '/' in name[len(SEAMS_TARGET):] or not isinstance(digest, str) or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest) for name, digest in previous_seams.items()):
        raise ValueError('Invalid test source receipt')
    previous = dict(previous, **previous_seams)
    wanted, removals, moved, owners = {}, {}, [], {}
    def admit(relative, content):
        parts = Path(relative).parts
        if len(parts) < 4 or '..' in parts or ':' in relative or '\\' in relative or parts[:1] != ('Source',) or parts[1] not in PRODUCTS or not isinstance(content, bytes) or not automation_file(Path(relative), content):
            raise ValueError('Invalid canonical automation source')
        group = 'Authoring' if parts[1] == PRODUCTS[0] else 'Blueprints'
        target = 'Source/' + MODULE + '/Private/' + group + '/' + parts[-1]
        if target in owners and owners[target] != relative:
            raise ValueError('Duplicate automation unit filename')
        owners[target] = relative
        if enabled:
            wanted[target] = content
    for module in PRODUCTS:
        folder = source / module
        for path in sorted(folder.rglob('*.cpp')):
            _inside(path, source.resolve())
            content = path.read_bytes()
            if not automation_file(path, content):
                continue
            relative = path.relative_to(plugin).as_posix()
            removals[relative] = content
            admit(relative, content)
            moved.append(relative)
    for relative, content in (incoming_tests or {}).items():
        admit(relative, content)
    # Reuse previously staged tests when called repeatedly without re-copying product sources.
    if enabled:
        if not moved and not incoming_tests and not fresh_product_sources:
            for relative in previous:
                path = plugin / relative
                _inside(path, plugin)
                if path.is_file():
                    if hashlib.sha256(path.read_bytes()).hexdigest() != previous[relative]:
                        raise ValueError('Test source changed outside staging owner: ' + relative)
                    wanted[relative] = path.read_bytes()
        scaffold = CANONICAL / 'Source' / MODULE
        for path in scaffold.rglob('*'):
            if path.is_file():
                relative = path.relative_to(scaffold).as_posix()
                if relative.startswith(SEAMS + '/'):
                    wanted[SEAMS_TARGET + path.name] = path.read_bytes()
                    continue
                if relative.endswith('.pending'):
                    relative = relative[:-8]
                wanted['Source/' + MODULE + '/' + relative] = path.read_bytes()
        for required in (MODULE + '.Build.cs', 'Private/' + MODULE + 'Module.cpp', 'Private/' + MODULE + 'PCH.h'):
            if 'Source/' + MODULE + '/' + required not in wanted:
                raise ValueError('Incomplete canonical test-module scaffold: ' + required)
        data['Modules'] = modules + untested + [dict(Name=MODULE, Type='Editor', LoadingPhase='PostEngineInit')]
    else:
        data['Modules'] = modules + untested
    build = source / PRODUCTS[0] / (PRODUCTS[0] + '.Build.cs')
    _inside(build, plugin)
    if build.is_file():
        original = build.read_text(encoding='utf-8')
        transformed = '\n'.join(line for line in original.splitlines() if PRODUCT_TEST_DEFINE not in line and line != SEAMS_DEFINE)
        # An unexpected future variant must be coordinated rather than silently retain the flag.
        if 'P4_MAPKIT_TESTS' in transformed or 'P4_KADAN_TEST_SEAMS' in transformed:
            raise ValueError('Unrecognized production test definition rule')
        if enabled:
            lines = transformed.split('\n')
            anchor = [index for index, line in enumerate(lines) if 'PrivatePCHHeaderFile' in line]
            if len(anchor) != 1:
                raise ValueError('The Kadan Build.cs has no single place for the development test seams')
            lines.insert(anchor[0] + 1, SEAMS_DEFINE)
            transformed = '\n'.join(lines)
        wanted[build.relative_to(plugin).as_posix()] = (transformed + '\n').encode('utf-8')
    # A source-only release snapshot must not carry the test module, including its scaffold.
    for path in (source / MODULE).rglob('*') if (source / MODULE).is_dir() else ():
        if not path.is_file():
            continue
        _inside(path, source.resolve())
        relative = path.relative_to(plugin).as_posix()
        if relative not in wanted:
            if relative not in previous:
                scaffold_relative = path.relative_to(source / MODULE)
                # Early source-only scaffolds used .pending suffixes. Admit
                # migration only for the three exact scaffold names and only
                # when bytes equal the current canonical source below.
                pending_scaffolds = (MODULE + '.Build.cs.pending', 'Private/' + MODULE + 'Module.cpp.pending', 'Private/' + MODULE + 'PCH.h.pending')
                if scaffold_relative.as_posix() in pending_scaffolds:
                    scaffold_relative = Path(scaffold_relative.as_posix()[:-8])
                canonical = CANONICAL / 'Source' / MODULE / scaffold_relative
                if not canonical.is_file():
                    canonical = canonical.with_name(canonical.name + '.pending')
                legacy = LEGACY_PENDING_SCAFFOLD.get(path.relative_to(source / MODULE).as_posix())
                legacy_matches = legacy is not None and hashlib.sha256(path.read_bytes()).hexdigest() == legacy
                if not legacy_matches and (not canonical.is_file() or canonical.read_bytes() != path.read_bytes()):
                    raise ValueError('Unknown test-module source would be removed: ' + relative)
            removals[relative] = path.read_bytes()
    # Validate unknown edits across the whole planned mutation before changing the descriptor.
    for relative, content in wanted.items():
        path = plugin / relative
        _inside(path, plugin)
        if path.is_file() and path.read_bytes() != content and relative in previous:
            if hashlib.sha256(path.read_bytes()).hexdigest() != previous[relative]:
                raise ValueError('Test source changed outside staging owner: ' + relative)
    for relative in previous.keys() - wanted.keys():
        path = plugin / relative
        _inside(path, plugin)
        if path.is_file():
            if hashlib.sha256(path.read_bytes()).hexdigest() != previous[relative]:
                raise ValueError('Test source changed outside staging owner: ' + relative)
            removals[relative] = path.read_bytes()
    touched = []
    for relative, content in wanted.items():
        path = plugin / relative
        if not path.is_file() or path.read_bytes() != content:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            touched.append(relative)
    for relative in removals:
        path = plugin / relative
        if path.is_file():
            path.unlink()
            touched.append('removed ' + relative)
    text = json.dumps(data, indent=2) + '\n'
    if descriptor.read_text(encoding='utf-8') != text:
        descriptor.write_text(text, encoding='utf-8')
        touched.append(descriptor.name)
    # Only test-module paths belong to the receipt; product Build.cs is the shared source owner's.
    owned = {name: hashlib.sha256(value).hexdigest() for name, value in wanted.items() if name.startswith('Source/' + MODULE + '/')}
    owned_seams = {name: hashlib.sha256(value).hexdigest() for name, value in wanted.items() if name in seams}
    receipt_text = json.dumps(dict(owner=OWNER, enabled=bool(enabled), files=owned, seams=owned_seams), indent=2) + '\n'
    if not receipt_path.is_file() or receipt_path.read_text(encoding='utf-8') != receipt_text:
        receipt_path.write_text(receipt_text,encoding='utf-8')
    tests = sum(name.startswith('Source/' + MODULE + '/Private/' + group + '/') and name.endswith('.cpp') for name in owned for group in ('Authoring', 'Blueprints'))
    return dict(touched=touched, moved=moved, tests=tests, enabled=bool(enabled))


def assert_production_sources(plugin):
    """Require test-free product source before a release compiler ever runs."""
    source = Path(plugin) / 'Source'
    for product in PRODUCTS:
        for path in (source / product).rglob('*.cpp'):
            if automation_file(path, path.read_bytes()):
                raise ValueError('Automation source remains in production module: ' + path.name)
    if any(path.is_file() for path in (source / MODULE).rglob('*')):
        raise ValueError('Development test module remains in production source snapshot')
    if any((Path(plugin) / name).is_file() for name in seam_targets()):
        raise ValueError('Development test seam sources remain in the production Kadan module')
    build = source / PRODUCTS[0] / (PRODUCTS[0] + '.Build.cs')
    if build.is_file() and 'P4_KADAN_TEST_SEAMS' in build.read_text(encoding='utf-8'):
        raise ValueError('Development test seams remain in the production Kadan module')
