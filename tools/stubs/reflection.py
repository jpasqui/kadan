"""The captured reflection data (schema version 2) as lookups for the stub generator.

The capture has, per class and struct: path, parent path, size, and each property's name, offset,
flags and type detail. Per enum: names and values. It does not have class flags, implemented
interfaces, metadata, default values or default subobjects.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / 'private/decomp/reflection/rich-merged-v2.json'

# Script modules of the game that the launcher's UE 4.26 does not have. Everything else under
# /Script/ is taken from the editor.
GAME_MODULES = {'Carrot4', 'AkAudio', 'EasyParticle', 'UIParticle', 'SwitchRuntimeSettings'}
NESTED = ('inner', 'key', 'value', 'underlying')
OBJECT = '/Script/CoreUObject.Object'


def module_of(path):
    return path.split('.', 1)[0][len('/Script/'):] if path.startswith('/Script/') else None


def in_engine(path):
    """True for a /Script/ type the editor already has."""
    return path.startswith('/Script/') and module_of(path) not in GAME_MODULES


def walk(detail):
    """A type detail and every nested one (array inner, map key and value, enum underlying)."""
    yield detail
    for key in NESTED:
        if isinstance(detail.get(key), dict):
            yield from walk(detail[key])


def references(detail):
    """(role, path) for each type a property names: struct, enum, class, interface, signature."""
    for node in walk(detail):
        for role, key in (('struct', 'struct_path'), ('enum', 'enum_path'), ('class', 'class_path'),
                          ('class', 'meta_class_path'), ('class', 'interface_path'),
                          ('signature', 'signature_function')):
            if node.get(key):
                yield role, node[key]


class Database:
    def __init__(self, data):
        if data.get('schema_version') != 2:
            raise ValueError('The stub generator needs the schema version 2 capture')
        self.structures = {s['path']: s for s in data['structures']}
        self.enums = {e['path']: e for e in data['enums']}
        self.functions = {f['path']: f for f in data.get('functions', [])}

    @classmethod
    def load(cls, path=CAPTURE):
        return cls(json.loads(Path(path).read_text(encoding='utf-8')))

    def chain(self, path):
        """The path and its captured parents, nearest first. Stops at the first uncaptured one."""
        out = []
        while path and path in self.structures and path not in out:
            out.append(path)
            path = self.structures[path]['super_path']
        return out

    def is_a(self, path, base):
        return base in self.chain(path)

    def missing_types(self, path):
        """Structs, enums and signatures a type's own properties name that the capture lacks
        (classes named by object properties can be degraded to a parent, so they do not count)."""
        out = set()
        for field in self.structures[path]['fields']:
            for role, ref in references(field['type_detail']):
                if role == 'struct' and ref not in self.structures:
                    out.add(ref)
                elif role == 'enum' and ref not in self.enums:
                    out.add(ref)
                elif role == 'signature' and ref not in self.functions:
                    out.add(ref)
        return out
