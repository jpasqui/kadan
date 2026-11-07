"""Build the generated stub plugin against the launcher's UE 4.26, one Unreal tool at a time.

    py -3.11 tools/stubs/build_plugin.py <host folder> [--engine <UE_4.26>] [--headers-only]

<host folder> holds Plugins/P4Stubs (from gen_headers.py). A minimal project file is written next to
it and UnrealBuildTool builds the editor target for that project, which compiles only the plugin's
modules (the engine is prebuilt). --headers-only stops after UnrealHeaderTool. The kit's machine lock
is held while Unreal tools run. The log goes to <host folder>/build.log.

The game's own Engine enums and terrain structs (EC4Stencil, C4TerrainTagContainer, ...) have to be
registered under /Script/Engine, as in the game, not under the stub module's package. The build does
that the way the 0.2.0 release build did: UnrealHeaderTool writes its sources and the action list
(-SkipBuild -ForceHeaderGeneration -WriteActions), the generated registrations of those types are
replaced by the plugin's own (fixed_engine_declarations_uht.py), and UnrealBuildTool then runs exactly
that action list (-Mode=Execute), which never runs UnrealHeaderTool again.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]

import fixed_engine_declarations as declarations
import fixed_engine_declarations_uht as registration

ENGINE = Path(os.environ.get('P4_UE_ROOT') or r'C:\Program Files\Epic Games\UE_4.26')
GENERATED = 'Intermediate/Build/Win64/UE4Editor/Inc/Carrot4'


def adapt_generated(plugin):
    """Replace the generated registrations of the game's Engine types; return what was replaced."""
    manual = (plugin / declarations.CPP).read_text(encoding='utf-8')
    terrain = [len(re.findall(r'\bUScriptStruct\s*\*\s*Z_Construct_UScriptStruct_F' + name + r'\s*\(\s*\)\s*\{',
                              registration.code_only(manual))) for name in ('C4TerrainTag', 'C4TerrainTagContainer')]
    if terrain not in ([0, 0], [1, 1]):
        raise ValueError('the plugin replaces only one of the two terrain structs')
    files = [('Carrot4StubEnums.gen.cpp', registration.adapt_enum_registration)]
    if terrain == [1, 1]:
        files.append(('P4FixedEngineDeclarations.gen.cpp', registration.adapt_struct_registration))
    replaced = {}
    for name, adapt in files:
        path = plugin / GENERATED / name
        text, blocks = adapt(path.read_text(encoding='utf-8'))
        path.write_text(text, encoding='utf-8', newline='')
        replaced[name] = sorted(blocks)
    return replaced


def run(command, log, timeout, engine):
    log.write(' '.join(command) + '\n')
    log.flush()
    return subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                          timeout=timeout, cwd=str(Path(engine) / 'Engine/Source')).returncode


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('host')
    parser.add_argument('--engine', default=str(ENGINE))
    parser.add_argument('--headers-only', action='store_true')
    parser.add_argument('--timeout', type=int, default=1500)
    parser.add_argument('--extra', nargs='*', default=[])
    args = parser.parse_args(argv)
    host = Path(args.host).resolve()
    plugin = host / 'Plugins/P4Stubs'
    project = host / 'StubHost.uproject'
    # The editor scripts (run_editor.py) need Python in the same project: a project file without these two
    # starts an editor that never runs the script and never ends.
    plugins = [dict(Name=name, Enabled=True) for name in ('P4Stubs', 'PythonScriptPlugin', 'EditorScriptingUtilities')]
    project.write_text(json.dumps(dict(FileVersion=3, EngineAssociation='4.26', Category='', Description='',
                                       Plugins=plugins), indent='\t'), encoding='utf-8')
    ubt = str(Path(args.engine) / 'Engine/Binaries/DotNET/UnrealBuildTool.exe')
    target = [ubt, 'UE4Editor', 'Win64', 'Development', f'-Project={project}', '-NoHotReloadFromIDE',
              '-NoUBTMakefiles', '-WaitMutex']
    actions = host / 'stub-actions.json'
    import ue_bake
    lock = ue_bake._lock()
    start = time.time()
    code = 0
    try:
        with open(host / 'build.log', 'w', encoding='utf-8', errors='replace') as log:
            if args.headers_only:
                code = run(target + ['-SkipBuild', *args.extra], log, args.timeout, args.engine)
            else:
                declarations.set_manual_registration_mode(plugin, True)
                code = run(target + ['-SkipBuild', '-ForceHeaderGeneration', f'-WriteActions={actions}',
                                     *['-Module=' + name for name in registration.MODULES], *args.extra],
                           log, args.timeout, args.engine)
                if not code:
                    replaced = adapt_generated(plugin)
                    log.write('replaced generated registrations: ' + json.dumps(replaced) + '\n')
                    print('registered under /Script/Engine:', ', '.join(n for names in replaced.values() for n in names))
                    code = run([ubt, '-Mode=Execute', f'-Actions={actions}'], log, args.timeout, args.engine)
    finally:
        lock.close()
    print(f'UnrealBuildTool exit code {code} after {time.time() - start:.0f} s, log {host / "build.log"}')
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
