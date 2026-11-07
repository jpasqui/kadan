"""Add the hand-written editor helper module (editor_module/) to a generated stub plugin.

    py -3.11 tools/stubs/add_editor_module.py <plugin folder>     then build_plugin.py as usual

Copies the sources to <plugin>/Source/P4StubsEditor and lists the module in the .uplugin. The module
gives Python what UE 4.26's Python lacks (unreal.P4StubTools): a component on a placed actor, and a
Blueprint with components. Experimental, for the step 2 game experiments.
"""
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE = 'P4StubsEditor'


def main(argv):
    plugin = Path(argv[0]).resolve()
    descriptor = next(plugin.glob('*.uplugin'))
    target = plugin / 'Source' / MODULE
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(HERE / 'editor_module', target)
    data = json.loads(descriptor.read_text(encoding='utf-8'))
    data['Modules'] = [m for m in data['Modules'] if m['Name'] != MODULE] + \
        [dict(Name=MODULE, Type='Editor', LoadingPhase='Default')]
    descriptor.write_text(json.dumps(data, indent='\t') + '\n', encoding='utf-8', newline='\n')
    print('{} added to {}'.format(MODULE, descriptor))


if __name__ == '__main__':
    main(sys.argv[1:])
