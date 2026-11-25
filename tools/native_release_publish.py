"""Audit a release archive's members before it is published.

The archive may carry the P4Stubs plugin's descriptor, module list and DLLs, which declare the game's
types. It never carries game containers or assets, sources (P4Stubs' generated headers included),
Python, PDBs, private data or the compiled automation tests.
"""
from pathlib import Path
import zipfile

# The modules of P4Stubs, the plugin that declares the game's own classes, structs and enums.
STUB_MODULES = ('P4StubsCore', 'Carrot4', 'AkAudio', 'EasyParticle', 'UIParticle', 'SwitchRuntimeSettings')
STUB_DLLS = {'ue4editor-' + module.casefold() + '.dll' for module in STUB_MODULES}
# The only P4Stubs files a release ships, at the archive's root.
STUB_MEMBERS = {'P4Stubs/P4Stubs.uplugin', 'P4Stubs/Binaries/Win64/UE4Editor.modules',
                *('P4Stubs/Binaries/Win64/UE4Editor-' + module + '.dll' for module in STUB_MODULES)}

def audit_archive(path):
    """Native distribution never contains Python, raw custody or game-origin packages."""
    errors=[]
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            name=info.filename.replace('\\','/');parts=Path(name).parts
            lower=name.casefold()
            if any(part.casefold() == 'p4stubs' for part in parts) or Path(name).name.casefold() in STUB_DLLS:
                if name not in STUB_MEMBERS:
                    errors.append('Only the P4Stubs descriptor, module list and DLLs ship, in P4Stubs/: '+name)
            if name.startswith('/') or '..' in parts:errors.append('Unsafe archive member: '+name)
            if lower.endswith(('.py','.pyc','.pyo','.h','.hpp','.inl','.c','.cc','.cpp','.cs','.pdb','.profraw','.profdata')):errors.append('Development/runtime source artifact: '+name)
            if any(part.casefold() in ('private','source-custody','saved','tests') for part in parts):errors.append('Private import/test data: '+name)
            if lower.endswith('.dll'):
                data=archive.read(info)
                # Every test name of the project starts with one of these, whatever its category.
                automation_prefixes=('P4MapKit.', 'P4Blueprints.')
                if any(prefix.encode(encoding) in data for prefix in automation_prefixes
                       for encoding in ('utf-8', 'utf-16-le')):
                    errors.append('Private native automation compiled into distribution: '+name)
            if '/content/carrot4/' in lower:errors.append('Game-origin Content: '+name)
            if lower.endswith(('.uasset','.uexp','.ubulk','.uptnl','.pak','.utoc','.ucas')):
                errors.append('Native plugin distribution must not embed game/content containers: '+name)
    return errors
