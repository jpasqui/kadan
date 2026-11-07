"""Bound canonical installed UAT builds with an owned child-process profile."""
import json
import os
from pathlib import Path
import re
import subprocess


PROBE = r'''$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false);
$taskAssembly=[Reflection.Assembly]::LoadFrom($env:P4_UAT_PROBE_UBT);
$taskFlags=[Reflection.BindingFlags]'Public,NonPublic,Static';
$taskTool=$taskAssembly.GetType('UnrealBuildTool.UnrealBuildTool', $true);
if (!$taskTool.GetMethod('IsEngineInstalled',$taskFlags).Invoke($null,@())) { throw 'The engine is not installed.' };
$taskXml=$taskAssembly.GetType('UnrealBuildTool.XmlConfig',$true);
$taskXml.GetMethod('ReadConfigFiles',$taskFlags).Invoke($null,[object[]]@($null));
$taskConfig=[Activator]::CreateInstance($taskAssembly.GetType('UnrealBuildTool.BuildConfiguration',$true));
$taskXml.GetMethod('ApplyTo',$taskFlags).Invoke($null,[object[]]@($taskConfig));
$taskInputs=@($taskXml.GetMethod('FindInputFiles',$taskFlags).Invoke($null,@()) | ForEach-Object { $_.Location.FullName });
@{
appdata=[Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData);
localappdata=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData);
personal=[Environment]::GetFolderPath([Environment+SpecialFolder]::Personal);
xml_inputs=$taskInputs; xml_cache=$taskXml.GetField('CacheFile',$taskFlags).GetValue($null).FullName;
actions=$taskConfig.MaxParallelActions;
local_executor=$taskConfig.bAllowParallelExecutor;
remote=@($taskConfig.bAllowHybridExecutor,$taskConfig.bAllowXGE,$taskConfig.bAllowFASTBuild,$taskConfig.bAllowSNDBS,$taskConfig.bAllowDistcc)
} | ConvertTo-Json -Compress'''


def require_paths(paths, profile):
    expected = dict(appdata=profile / 'AppData/Roaming',
                    localappdata=profile / 'AppData/Local', personal=profile / 'Documents')
    if not isinstance(paths, dict) or not set(expected).issubset(paths):
        raise ValueError('The actual .NET build profile search paths are incomplete')
    for name, path in expected.items():
        actual = paths[name]
        if not isinstance(actual, str) or not actual or Path(actual).resolve() != path.resolve():
            raise ValueError('The actual .NET build profile search path escapes its owner: ' + name)
    return expected


def require_xml_scope(observed, profile, actions):
    expected = require_paths(observed, profile)
    inputs = observed.get('xml_inputs')
    required = {directory / 'Unreal Engine/UnrealBuildTool/BuildConfiguration.xml'
                for directory in (expected['appdata'], expected['personal'])}
    if not isinstance(inputs, list) or len(inputs) != 2 or any(not isinstance(value, str) or not value for value in inputs) or \
            {Path(value).resolve() for value in inputs} != {path.resolve() for path in required}:
        raise ValueError('Installed UBT found an XML input outside the owned build profile')
    cache = observed.get('xml_cache')
    if not isinstance(cache, str) or not cache or not Path(cache).resolve().is_relative_to(expected['localappdata'].resolve()):
        raise ValueError('Installed UBT found an XML cache outside the owned build profile')
    remote = observed.get('remote')
    if observed.get('local_executor') is not True or type(observed.get('actions')) is not int or observed['actions'] != actions or not isinstance(remote, list) or \
            len(remote) != 5 or any(type(value) is not bool or value for value in remote):
        raise ValueError('Installed UBT did not apply the bounded local build settings')


def prepare(folder, engine, actions, environment=None):
    """Keep every installed UBT XML input/cache inside this invocation's profile."""
    folder, engine = Path(folder).resolve(), Path(engine).resolve()
    if not 1 <= actions <= 64:
        raise ValueError('The compiler action limit must be between 1 and 64')
    if not (engine / 'Engine/Build/InstalledBuild.txt').is_file():
        raise ValueError('Scoped XML settings require the installed Unreal engine')
    profile = folder / 'profile'
    expected = dict(appdata=profile / 'AppData/Roaming',
                    localappdata=profile / 'AppData/Local', personal=profile / 'Documents')
    for path in (profile, *expected.values()):
        if path.resolve() != path or path.is_symlink():
            raise ValueError('The owned build profile is redirected')
        path.mkdir(parents=True, exist_ok=True)
    # Installed UBT skips engine XML files. It reads AppData first, then Personal;
    # both actual locations contain the same bounded settings. Its cache uses
    # LocalApplicationData and therefore also stays inside this profile.
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">\n'
           '  <BuildConfiguration>\n'
           '    <MaxParallelActions>{}</MaxParallelActions>\n'
           '    <bAllowParallelExecutor>true</bAllowParallelExecutor>\n'
           '    <bAllowHybridExecutor>false</bAllowHybridExecutor>\n'
           '    <bAllowXGE>false</bAllowXGE>\n'
           '    <bAllowFASTBuild>false</bAllowFASTBuild>\n'
           '    <bAllowSNDBS>false</bAllowSNDBS>\n'
           '    <bAllowDistcc>false</bAllowDistcc>\n'
           '    <bDisableDebugInfo>true</bDisableDebugInfo>\n'
           '  </BuildConfiguration>\n'
           '</Configuration>\n').format(actions)
    for directory in (expected['appdata'], expected['personal']):
        config = directory / 'Unreal Engine/UnrealBuildTool/BuildConfiguration.xml'
        if config.resolve() != config or config.is_symlink():
            raise ValueError('The owned build XML input is redirected')
        config.parent.mkdir(parents=True, exist_ok=True)
        if not config.is_file() or config.read_text(encoding='utf-8') != xml:
            config.write_text(xml, encoding='utf-8')
    env = dict(os.environ if environment is None else environment)
    for name in list(env):
        if name.upper() in ('USERPROFILE', 'APPDATA', 'LOCALAPPDATA'):
            del env[name]
    env.update(USERPROFILE=str(profile), APPDATA=str(expected['appdata']),
               LOCALAPPDATA=str(expected['localappdata']))
    system_root = next((value for name, value in env.items() if name.casefold() == 'systemroot'), None)
    if not system_root:
        raise ValueError('The inherited Windows system root is missing')
    powershell = Path(system_root) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    probe_env = dict(env, P4_UAT_PROBE_UBT=str(engine / 'Engine/Binaries/DotNET/UnrealBuildTool.exe'))
    result = subprocess.run([str(powershell), '-NoProfile', '-NonInteractive', '-Command', PROBE],
                            env=probe_env, capture_output=True, text=True, encoding='utf-8', timeout=30)
    if result.returncode:
        raise ValueError('The actual .NET build profile search paths could not be read: ' + result.stderr[-1200:])
    observed = json.loads(result.stdout)
    require_xml_scope(observed, profile, actions)
    return env, dict(profile=str(profile), max_parallel_actions=actions,
                     xml_inputs=[str(expected[name] / 'Unreal Engine/UnrealBuildTool/BuildConfiguration.xml')
                                 for name in ('appdata', 'personal')])


def canonical_command(engine, staged, packaged):
    return [str(Path(engine) / 'Engine/Build/BatchFiles/RunUAT.bat'), 'BuildPlugin',
            '-Plugin=' + str(Path(staged) / 'Kadan.uplugin'), '-Package=' + str(packaged),
            '-TargetPlatforms=Win64', '-NoDeleteHostProject', '-VS2019']


def release_commands(engine, staged, packaged, actions):
    """Only canonical UAT compiles; preliminary UBT exports its actions and toolchain."""
    engine, staged, packaged = Path(engine), Path(staged), Path(packaged)
    return [
        [str(engine / 'Engine/Build/BatchFiles/RunUAT.bat'), 'BuildPlugin',
         '-Plugin=' + str(staged / 'Kadan.uplugin'), '-Package=' + str(packaged),
         '-NoHostPlatform', '-NoTargetPlatforms', '-NoDeleteHostProject', '-VS2019'],
        [str(engine / 'Engine/Binaries/DotNET/UnrealBuildTool.exe'), 'UE4Editor', 'Win64', 'Development',
         '-Project=' + str(packaged / 'HostProject/HostProject.uproject'), '-NoHotReloadFromIDE',
         '-NoUBTMakefiles', '-WaitMutex', '-NoDebugInfo', '-Module=Kadan', '-Module=KadanShaderFormat',
         '-Module=P4BlueprintImport',
         '-MaxParallelActions=' + str(actions),
         '-WriteOutdatedActions=' + str(packaged / 'HostProject/kadan-preflight-actions.json')],
        canonical_command(engine, staged, packaged),
    ]


def require_parallel_build(log, actions):
    """Require the canonical compile to report its actual bounded worker count."""
    rows = [(int(count), int(workers)) for count, workers in re.findall(
        r'Building\s+(\d+)\s+actions?\s+with\s+(\d+)\s+process(?:es)?\.\.\.', log)]
    if not rows or not any(count > 0 for count, workers in rows) or any(not 1 <= workers <= actions for count, workers in rows):
        raise ValueError('Canonical BuildPlugin did not report a bounded local compile')
    return [dict(actions=count, workers=workers) for count, workers in rows]


def audit_pch(plugin, source_pch):
    """Read the canonical Kadan compile responses and exact private PCH source."""
    plugin, source_pch = Path(plugin).resolve(), Path(source_pch)
    private = plugin / 'Source/Kadan/Private/P4MapKitPCH.h'
    if not private.is_file() or private.read_bytes() != source_pch.read_bytes():
        raise ValueError('Canonical Kadan private PCH differs from its source')
    header = private.read_text(encoding='utf-8')
    include = header.find('#include "UnrealEdSharedPCH.h"')
    if include < 0 or any(not 0 <= header.find(pragma) < include for pragma in (
            '#pragma float_control(precise, on)', '#pragma fp_contract(off)')):
        raise ValueError('Canonical Kadan private PCH lacks its numeric settings before engine headers')
    folder = plugin / 'Intermediate/Build/Win64/UE4Editor/Development/Kadan'
    # The PCH is created by its own action, whose response is PCH.<Module>.h.pch.response.
    responses = list(folder.glob('*.obj.response')) + list(folder.glob('*.pch.response'))
    if not 2 <= len(responses) <= 1024:
        raise ValueError('Canonical Kadan object response files are absent or incomplete')
    wrappers, compiled_headers, creation, uses = set(), set(), 0, 0
    for response in responses:
        if response.resolve() != response or response.stat().st_size > 1 << 20:
            raise ValueError('Canonical Kadan object response path is redirected or oversized')
        text = response.read_text(encoding='utf-8-sig')
        flags = re.findall(r'/Y([cu])(?:"([^"]+)"|([^\s"]+))', text)
        if len(flags) != 1:
            raise ValueError('Canonical Kadan translation unit does not use its private PCH: ' + response.name)
        kind, quoted, plain = flags[0]
        wrapper = Path(quoted or plain)
        if not wrapper.is_absolute() or wrapper.resolve() != wrapper or not wrapper.is_relative_to(plugin) or \
                not wrapper.is_file() or wrapper.stat().st_size > 1 << 20:
            raise ValueError('Canonical Kadan PCH wrapper escapes its compiled host')
        wrappers.add(wrapper)
        pch_flags = re.findall(r'/Fp(?:"([^"]+)"|([^\s"]+))', text)
        if len(pch_flags) != 1:
            raise ValueError('Canonical Kadan translation unit lacks its compiled private PCH')
        pch = Path(pch_flags[0][0] or pch_flags[0][1])
        if not pch.is_absolute() or pch.resolve() != pch or not pch.is_relative_to(plugin) or not pch.is_file():
            raise ValueError('Canonical Kadan compiled PCH escapes its host or is absent')
        compiled_headers.add(pch)
        if kind == 'u':
            force = re.findall(r'/FI(?:"([^"]+)"|([^\s"]+))', text)
            if wrapper not in {Path(quoted or plain).resolve() for quoted, plain in force}:
                raise ValueError('Canonical Kadan translation unit does not force its private PCH')
        creation += kind == 'c'
        uses += kind == 'u'
    if creation != 1 or uses != len(responses) - 1 or len(wrappers) != 1 or len(compiled_headers) != 1:
        raise ValueError('Canonical Kadan translation units disagree on their private PCH')
    wrapper = next(iter(wrappers))
    includes = re.findall(r'^\s*#include\s+"([^"]+)"', wrapper.read_text(encoding='utf-8-sig'), re.MULTILINE)
    if len(includes) != 1 or Path(includes[0]).resolve() != private:
        raise ValueError('Canonical Kadan PCH wrapper does not include its exact private header')
    return dict(objects=len(responses), creations=creation, uses=uses, numeric_settings_before_engine=True)
