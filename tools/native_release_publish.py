"""Publish an exact qualified native kit archive; failed admission preserves the old release."""
import argparse
import json
from pathlib import Path
import os
import shutil
import tempfile
import zipfile
if __package__:
    from . import native_quality_gate
else:
    import native_quality_gate

def audit_archive(path):
    """Native distribution never contains Python, raw custody or game-origin packages."""
    errors=[]
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            name=info.filename.replace('\\','/');parts=Path(name).parts
            lower=name.casefold()
            if any(part.casefold() == 'p4stubs' for part in parts) or Path(name).name.casefold() in {
                    'ue4editor-' + module.casefold() + '.dll' for module in (
                        'P4StubsCore', 'Carrot4', 'AkAudio', 'EasyParticle', 'UIParticle', 'SwitchRuntimeSettings')}:
                errors.append('Game-derived type declarations are private setup inputs: '+name)
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

def publish(artifact,destination,evidence,policy,root):
    report=native_quality_gate.check(root,policy,evidence,artifact)
    if artifact.is_file():report['errors']+=audit_archive(artifact)
    report['ready']=not report['errors']
    if not report['ready']:return report
    if artifact.resolve()==destination.resolve():raise ValueError('Candidate and published artifact must be separate')
    destination.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.native-kit-',dir=str(destination.parent))
    try:
        with os.fdopen(fd,'wb') as target,artifact.open('rb') as source:shutil.copyfileobj(source,target)
        # Recheck the staged bytes and current source after copy, before replacing.
        staged=Path(name)
        repeated=native_quality_gate.check(root,policy,evidence,staged)
        if not repeated['ready']:return repeated
        os.replace(name,destination)
    finally:
        if Path(name).exists():Path(name).unlink()
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifact',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args()
    evidence=json.loads(args.evidence.read_text(encoding='utf-8'))
    policy=json.loads(native_quality_gate.POLICY.read_text(encoding='utf-8'))
    report=publish(args.artifact,args.destination,evidence,policy,native_quality_gate.ROOT)
    print(json.dumps(dict(ready=report['ready'],errors=report['errors']),indent=2))
    return 0 if report['ready'] else 1

if __name__=='__main__':raise SystemExit(main())
