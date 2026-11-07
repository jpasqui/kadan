"""Make the P4Stubs plugin again from its pinned inputs, and compare or build it.

    py -3.11 tools/stubs/regenerate.py [--host private/stubs] [--compare tools/stubs/shipped-0.2.0.json]
                                       [--build] [--engine C:/UE_4.26]

Writes the plugin's source to <host>/Plugins/P4Stubs (only the files that changed, so a build compiles
only those) with its receipt, unified-stubs.json: the inputs' SHA-256 and every generated file's. The
inputs are the ones inputs.json pins; an absent or different input stops the run before anything is
written. --compare lists the generated files that differ from another receipt (the shipped plugin's
is shipped-0.2.0.json). --build then builds the plugin with UnrealBuildTool (build_plugin.py); on the
desktop, run it through remote_unreal.py after pushing private/stub-inputs once.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import stub_build  # noqa: E402

# The original native layout facts for the six engine types the game's engine adds, and the terrain
# storage receipt they carry. Bounded, as fixed_declaration_build.py read them for the shipped plugin.
NATIVE_LIMIT, TERRAIN_LIMIT = 2 * 1024 * 1024, 256 * 1024


def native_evidence():
    data = stub_build.input_bytes('native_engine_evidence')
    if len(data) > NATIVE_LIMIT:
        raise ValueError('native engine evidence is larger than {} bytes'.format(NATIVE_LIMIT))
    evidence = json.loads(data.decode('utf-8'))
    if not isinstance(evidence, dict) or not isinstance(evidence.get('classes'), dict):
        raise ValueError('native engine evidence must be a named class object')
    terrain = stub_build.input_bytes('terrain_native_receipt')
    if len(terrain) > TERRAIN_LIMIT:
        raise ValueError('terrain receipt is larger than {} bytes'.format(TERRAIN_LIMIT))
    evidence['terrain_receipt_bytes'] = terrain
    return evidence


def compare(receipt, reference):
    """The generated files that differ from the reference receipt's, by name."""
    mine, theirs = receipt['files'], reference['files']
    return dict(changed=sorted(n for n in mine.keys() & theirs.keys() if mine[n] != theirs[n]),
                added=sorted(mine.keys() - theirs.keys()), removed=sorted(theirs.keys() - mine.keys()),
                inputs_changed=sorted(k for k in receipt['inputs'] if receipt['inputs'][k] != reference['inputs'].get(k)))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', type=Path, default=ROOT / 'private/stubs',
                        help='folder that gets Plugins/P4Stubs and the build project')
    parser.add_argument('--compare', type=Path, help='another unified-stubs.json receipt to compare with')
    parser.add_argument('--build', action='store_true', help='build the plugin with UnrealBuildTool afterwards')
    parser.add_argument('--engine', default=None, help='UE 4.26 folder for --build')
    args = parser.parse_args(argv)
    plugin = args.host.resolve() / 'Plugins/P4Stubs'
    receipt = stub_build.generate(plugin, require_exact_identities=True, native_engine_evidence=native_evidence())
    print('{} classes in {} modules: {}'.format(receipt['classes'], len(receipt['modules']), plugin))
    code = 0
    if args.compare:
        found = compare(receipt, json.loads(args.compare.read_text(encoding='utf-8')))
        for what, names in found.items():
            print('{}: {}'.format(what, ', '.join(names) if names else 'none'))
    if args.build:
        import build_plugin
        code = build_plugin.main([str(args.host.resolve())] + (['--engine', args.engine] if args.engine else []))
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
