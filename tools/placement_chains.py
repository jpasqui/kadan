"""Writes the native classes of the game's placeable classes, for tools/placement_census.cpp.

    python tools/placement_chains.py <reflection.json> <placement folder> <out.txt>

reflection.json is the game's reflection capture (private/stub-inputs/reflection.json). The placement folder holds
the game's placement lists (AP_*.uasset); every class a row of them names gets a line:

    <class path> TAB <actor's native classes> TAB <AI component's native classes> TAB <components>

Native classes are most derived first, comma separated. The AI component is the actor's component of the most
derived class that derives CarrotAIComponent. Components are the actor's components of its Blueprint classes,
most derived class first, then the components its native classes declare, as name=classes (classes colon
separated), semicolon separated; a native class's component has a * before its name. That one is its declared
class, perhaps a base of the one the game makes, and in the editor its property often holds nothing.

A fifth column holds the tags of the components the game's serializers find by tag (name=tag,tag; semicolon
separated). Reflection does not hold them: they come from tools/placement_component_tags.txt.
"""
import json
import re
import sys
from pathlib import Path


def main(argv):
    tags = {}
    for line in (Path(__file__).resolve().parent / 'placement_component_tags.txt').read_text(encoding='utf-8').splitlines():
        if line and not line.startswith('#'):
            path, _, held = line.partition(chr(9))
            tags[path] = held
    if len(argv) != 3:
        print(__doc__)
        return 2
    reflection, folder, out = Path(argv[0]), Path(argv[1]), Path(argv[2])
    structures = {entry['path']: entry for entry in json.loads(reflection.read_text(encoding='utf-8'))['structures']}

    def native_chain(path):
        chain = []
        while path:
            entry = structures.get(path)
            if path.startswith('/Script/'):
                chain.append(path.rsplit('.', 1)[-1])
            if not entry:
                break
            path = entry.get('super_path')
        return chain

    # The class paths rows name: SoftRefActorClass values are soft object paths in the lists' name tables.
    wanted = set()
    for asset in folder.rglob('AP_*.uasset'):
        for match in re.finditer(rb'/Game/[A-Za-z0-9_/]+\.[A-Za-z0-9_]+_C', asset.read_bytes()):
            wanted.add(match.group().decode())

    lines = []
    for path in sorted(wanted):
        if path not in structures:
            continue
        components = []
        level = path
        while level:
            entry = structures.get(level)
            if not entry:
                break
            for field in entry.get('fields', []):
                detail = field.get('type_detail') or {}
                if field.get('observed_type_name') != 'ObjectProperty' or not detail.get('class_path'):
                    continue
                chain = native_chain(detail['class_path'])
                if 'ActorComponent' in chain:
                    components.append((('*' if level.startswith('/Script/') else '') + field['name'], chain))
            level = entry.get('super_path')
        ai = [chain for _, chain in components if 'CarrotAIComponent' in chain]
        ai_chain = max(ai, key=len) if ai else []
        lines.append('\t'.join([
            path, ','.join(native_chain(path)), ','.join(ai_chain),
            ';'.join(name + '=' + ':'.join(chain) for name, chain in components), tags.get(path, '')]))
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} classes')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
