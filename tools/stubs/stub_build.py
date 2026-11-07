"""One complete stub definition for the kit and Blueprint recovery.

All inputs are required. Module names preserve the game's /Script package identities; conflicting
installed modules are refused instead of silently loading a real plugin at those identities.
Generation writes only changed text files. The receipt contains logical inputs and hashes, no paths.
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUBS = Path(__file__).resolve().parent
# The generator's inputs, pinned by SHA-256 in inputs.json. The files are research captures, too large for
# the repository, so they live in the manifest's folder (private/stub-inputs) on each computer that builds.
MANIFEST = STUBS / 'inputs.json'
SOURCES = ('reflection', 'engine_index', 'defaults', 'engine_declared', 'zero_constructor')


def manifest():
    return json.loads(MANIFEST.read_text(encoding='utf-8'))


def input_bytes(key, root=ROOT):
    """The pinned input's bytes. Refuses an absent input or one whose bytes are not the pinned ones."""
    pinned = manifest()
    entry = pinned['inputs'][key]
    path = root / pinned['folder'] / entry['file']
    if not path.is_file():
        raise ValueError('stub input {} is absent: put {} ({}) at {}'.format(key, entry['file'], entry['source'], path))
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('stub input {} at {} is not the pinned one (SHA-256 {})'.format(key, path, entry['sha256']))
    return data


ENGINE_DEFAULTS = (
    ('/Script/Engine.PointLightComponent', 'CastShadows'),
    ('/Script/Engine.PointLightComponent', 'CastDynamicShadows'),
)


def inputs(root=ROOT):
    values, hashes = {}, {}
    for key in SOURCES:
        data = input_bytes(key, root)
        values[key] = json.loads(data.decode('utf-8'))
        hashes[key] = hashlib.sha256(data).hexdigest()
    return values, hashes


def installed_descriptor(descriptor):
    """Read Unreal's BOM/array-trailing-comma descriptors without changing bytes.

    Only punctuation outside quoted JSON strings is adapted. All other JSON
    syntax still passes through the strict decoder and failures name the file.
    """
    text = descriptor.read_text(encoding='utf-8-sig')
    characters, quoted, escaped = [], False, False
    for index, character in enumerate(text):
        if quoted:
            characters.append(character)
            if escaped:
                escaped = False
            elif character == '\\':
                escaped = True
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
            characters.append(character)
        elif character == ',':
            previous = index - 1
            while previous >= 0 and text[previous] in ' \t\r\n':
                previous -= 1
            following = index + 1
            while following < len(text) and text[following] in ' \t\r\n':
                following += 1
            if (following >= len(text) or text[following] != ']'
                    or previous < 0 or text[previous] in '[{,:'):
                characters.append(character)
        else:
            characters.append(character)
    try:
        data = json.loads(''.join(characters))
        if not isinstance(data, dict) or not isinstance(data.get('Modules', []), list):
            raise ValueError('descriptor object/modules array is malformed')
        for module in data.get('Modules', []):
            if not isinstance(module, dict) or not isinstance(module.get('Name'), str) or not module['Name']:
                raise ValueError('descriptor module name is malformed')
        return data
    except (ValueError, TypeError) as error:
        raise ValueError('installed plugin descriptor could not be read: ' + str(descriptor) + ': ' + str(error)) from error


def collisions(plugin, modules, roots):
    own = plugin.resolve()
    for root in roots:
        if not root.exists():
            continue
        for descriptor in root.rglob('*.uplugin'):
            if descriptor.parent.resolve() == own:
                continue
            data = installed_descriptor(descriptor)
            found = modules & {m['Name'] for m in data.get('Modules', ())}
            if found:
                raise ValueError('stub module identity collision: ' + ', '.join(sorted(found)))


def generate(plugin, plugin_roots=(), root=ROOT, require_exact_identities=False, native_engine_evidence=None):
    values, hashes = inputs(root)
    sys.path.insert(0, str(STUBS))
    import gen_headers
    import fixed_declarations
    import reflection
    native_options = {} if native_engine_evidence is None else dict(
        native_engine_evidence=native_engine_evidence,
        original_source_sha256=values['reflection'].get('source_sha256'))
    generator = fixed_declarations.Generator(
        reflection.Database(values['reflection']), values['engine_index'], expose='all',
        defaults=values['defaults'], functions=True, engine_declared=values['engine_declared'],
        zero_constructor=values['zero_constructor'], engine_defaults=ENGINE_DEFAULTS, **native_options)
    generator.select([], True)
    files = generator.files()
    identity_refusals = generator.identity_refusals()
    if require_exact_identities and identity_refusals:
        raise ValueError('; '.join(identity_refusals))
    descriptor = json.loads(files['P4Stubs.uplugin'])
    descriptor['FriendlyName'] = 'Kadan game types'
    descriptor['Description'] = 'Native type declarations used by Kadan to import your own game packages.'
    # These engine plugin modules are direct dependencies of the fixed Carrot4
    # declarations. Declare their owning plugins as well as the Build.cs modules
    # so UAT and clean projects enable the same supported engine definitions.
    carrot_build = files.get('Source/Carrot4/Carrot4.Build.cs', '')
    required_plugins = [name for name in ('Paper2D', 'ProceduralMeshComponent')
                        if '"' + name + '"' in carrot_build]
    if required_plugins:
        descriptor['Plugins'] = [dict(Name=name, Enabled=True) for name in required_plugins]
    files['P4Stubs.uplugin'] = json.dumps(descriptor, sort_keys=True, indent=1) + '\n'
    modules = {m['Name'] for m in descriptor['Modules']}
    collisions(Path(plugin), modules, [Path(p) for p in plugin_roots])
    receipt = dict(format='p4-unified-stubs', version=1, inputs=hashes,
                   modules=sorted(modules), classes=len(generator.classes), functions=True,
                   identity_refusals=identity_refusals,
                   files={key: hashlib.sha256(value.encode('utf-8')).hexdigest()
                          for key, value in sorted(files.items())})
    if native_engine_evidence is not None:
        receipt['native_engine_registration'] = dict(types=generator.native_engine['types'],
                                                   refusals=generator.native_engine['refusals'],
                                                   runtime_qualified=False)
        if generator.native_engine.get('terrain_evidence'):
            receipt['native_engine_registration']['terrain_evidence'] = generator.native_engine['terrain_evidence']
        if generator.native_engine.get('class_flag_plan'):
            receipt['native_engine_registration']['class_flag_plan'] = generator.native_engine['class_flag_plan']
    files['unified-stubs.json'] = json.dumps(receipt, sort_keys=True, indent=1) + '\n'
    gen_headers.write(files, plugin)
    notes = '\n'.join(generator.notes) + '\n'
    gen_headers.write({'generator-notes.txt': notes}, plugin)
    return receipt


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    parser.add_argument('--plugin-root', action='append', default=[])
    parser.add_argument('--require-exact-identities', action='store_true',
                        help='refuse unqualified original Script identities before writing any output')
    args = parser.parse_args()
    result = generate(args.out, args.plugin_root, require_exact_identities=args.require_exact_identities)
    print(json.dumps({key: result[key] for key in ('format', 'version', 'modules', 'classes')}, indent=1))
