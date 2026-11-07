"""Stage the one Blueprint editor source module into the authoring plugin for its native build.

No copied host/projects or binaries. A receipt permits updates only to our unchanged staged sources.
"""
import hashlib
import json
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'Kadan/Source/P4BlueprintImport'
MODULE = 'P4BlueprintImport'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def stage(plugin):
    plugin = Path(plugin)
    descriptor = plugin / 'Kadan.uplugin'
    data = json.loads(descriptor.read_text(encoding='utf-8'))
    installed = plugin.parent.rglob('*.uplugin') if plugin.parent.name.casefold() == 'plugins' else ()
    for other in installed:
        if other.resolve() == descriptor.resolve():
            continue
        other_data = json.loads(other.read_text(encoding='utf-8'))
        if any(row.get('Name') == MODULE for row in other_data.get('Modules', ())):
            raise ValueError('Blueprint editor module collision with installed plugin: ' + other.stem)
    if plugin.parent.name.casefold() == 'plugins':
        for binary in plugin.parent.rglob('UE4Editor-' + MODULE + '.dll'):
            if plugin.resolve() not in binary.resolve().parents:
                raise ValueError('Blueprint editor DLL basename collision with installed plugin: '
                                 + binary.relative_to(plugin.parent).as_posix())
    target = plugin / 'Source' / MODULE
    receipt_file = plugin / 'blueprint-module-sources.json'
    receipt = json.loads(receipt_file.read_text(encoding='utf-8')) if receipt_file.is_file() else {}
    if not isinstance(receipt, dict) or any(Path(key).is_absolute() or '..' in Path(key).parts
                                           or ':' in key or '\\' in key for key in receipt):
        raise ValueError('invalid Blueprint editor staging receipt')
    wanted = {p.relative_to(SOURCE).as_posix(): p.read_bytes() for p in SOURCE.rglob('*') if p.is_file()}
    # Validate the whole destination before replacing even one file. Unknown host edits survive.
    for path in target.rglob('*') if target.is_dir() else ():
        if not path.is_file():
            continue
        relative = path.relative_to(target).as_posix()
        current = path.read_bytes()
        if current != wanted.get(relative) and digest(current) != receipt.get(relative):
            raise ValueError('Blueprint editor source changed outside its staging owner: ' + relative)
    touched = []
    for relative, contents in wanted.items():
        path = target / relative
        if not path.is_file() or path.read_bytes() != contents:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
            touched.append('Source/' + MODULE + '/' + relative)
    for relative in receipt.keys() - wanted.keys():
        path = target / relative
        if path.is_file():
            path.unlink()
            touched.append('removed Source/' + MODULE + '/' + relative)
    modules = [m for m in data.get('Modules', ()) if m['Name'] != MODULE]
    data['Modules'] = modules + [dict(Name=MODULE, Type='Editor', LoadingPhase='Default')]
    text = json.dumps(data, indent=2) + '\n'
    if descriptor.read_text(encoding='utf-8') != text:
        descriptor.write_text(text, encoding='utf-8', newline='\n')
        touched.append(descriptor.name)
    receipt_file.write_text(json.dumps({k: digest(v) for k, v in sorted(wanted.items())}, indent=1) + '\n',
                            encoding='utf-8', newline='\n')
    return touched


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('plugin')
    args = parser.parse_args()
    print('\n'.join(stage(args.plugin)))
