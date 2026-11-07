"""Exercise shipping production DLLs with the separate private automation module."""
import hashlib
import json
import os
from pathlib import Path
import zipfile

try:
    from . import native_release_install as release
except ImportError:
    import native_release_install as release


TEST = 'P4MapKitTests'
PRODUCTS = set(release.MODULES)


def verify(binding):
    """Keep the shipping DLLs and the private test DLL unchanged through the run."""
    for path, digest in binding.items():
        if not path.is_file() or path.is_symlink() or release.digest(path) != digest:
            raise ValueError('The shipping test runtime changed during the run: ' + path.name)


def install(archive, project):
    archive, project = Path(archive).resolve(), Path(project).resolve()
    archive_hash = release.digest(archive)
    expected = release.archive_members(archive)
    plugin = project.parent / 'Plugins/Kadan'
    if not project.is_file() or project.suffix != '.uproject' or plugin.resolve() != plugin or not plugin.is_dir():
        raise ValueError('A canonical isolated native test project is required')
    if any(part.is_symlink() or part.resolve() != part for part in (plugin.parent, plugin, plugin / 'Binaries', plugin / 'Binaries/Win64')):
        raise ValueError('The native test plugin folder is redirected')
    descriptor = plugin / 'Kadan.uplugin'
    mapping = plugin / 'Binaries/Win64/UE4Editor.modules'
    existing = json.loads(descriptor.read_text(encoding='utf-8'))
    names = [row['Name'] for row in existing['Modules']]
    modules = json.loads(mapping.read_text(encoding='utf-8'))
    if len(names) != len(PRODUCTS) + 1 or set(names) != PRODUCTS | {TEST} or modules['Modules'] != {
            name: 'UE4Editor-' + name + '.dll' for name in names}:
        raise ValueError('The separate native automation module is required')
    test_row = next(row for row in existing['Modules'] if row['Name'] == TEST)
    if test_row != dict(Name=TEST, Type='Editor', LoadingPhase='PostEngineInit'):
        raise ValueError('Unexpected private test module descriptor')
    test_dll = mapping.parent / ('UE4Editor-' + TEST + '.dll')
    if not test_dll.is_file() or test_dll.stat().st_size == 0 or test_dll.is_symlink():
        raise ValueError('The separate native test DLL is missing')
    test_digest = release.digest(test_dll)
    replacements = {}
    with zipfile.ZipFile(archive) as source:
        shipping = json.loads(source.read('Kadan/Kadan.uplugin'))
        shipping_map = json.loads(source.read('Kadan/Binaries/Win64/UE4Editor.modules'))
        if shipping_map['BuildId'] != modules['BuildId']:
            raise ValueError('The shipping DLLs and native tests use different Unreal builds')
        for name in PRODUCTS:
            member = 'Binaries/Win64/UE4Editor-' + name + '.dll'
            data = source.read('Kadan/' + member)
            if hashlib.sha256(data).hexdigest() != expected[member]:
                raise ValueError('The shipping archive changed during test installation')
            replacements[plugin / member] = data
        config = 'Config/DefaultKadan.ini'
        data = source.read('Kadan/' + config)
        if hashlib.sha256(data).hexdigest() != expected[config]:
            raise ValueError('The shipping archive changed during test installation')
        replacements[plugin / config] = data
    declarations = release.bundled_declarations(archive, shipping_map['BuildId'])
    if declarations is not None:
        release.declaration_archive.verify_installed_declarations(declarations, project)
    if release.digest(archive) != archive_hash:
        raise ValueError('The shipping archive changed during test installation')
    # Only the private host enables automation. The published descriptor and
    # archive remain untouched, and the product DLLs are the exact shipping bytes.
    shipping['Modules'].append(test_row)
    replacements[descriptor] = (json.dumps(shipping, indent=2) + '\n').encode('utf-8')
    binding = {archive: archive_hash, mapping: release.digest(mapping), test_dll: test_digest}
    for target, data in replacements.items():
        if target.is_symlink() or target.resolve() != target or not target.parent.is_dir():
            raise ValueError('The native test runtime target is redirected or absent')
    for target, data in replacements.items():
        digest = hashlib.sha256(data).hexdigest()
        if not target.is_file() or release.digest(target) != digest:
            temporary = target.with_name(target.name + '.shipping-test')
            created = False
            try:
                with temporary.open('xb') as output:
                    created = True
                    output.write(data)
                os.replace(temporary, target)
            finally:
                if created and temporary.exists():
                    temporary.unlink()
        binding[target] = digest
    verify(binding)
    return binding
