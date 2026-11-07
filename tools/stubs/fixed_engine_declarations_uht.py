"""Bounded pre-compile UHT enum registration replacement and source binding.

Use UBT -SkipBuild -ForceHeaderGeneration -WriteActions=<owned.json>, prepare,
validate, then UBT -Mode=Execute -Actions=<same.json>. Execute mode never invokes
UHT. Receipts contain logical names/hashes; process-local paths are attributes.
"""
import hashlib
import json
import re
from pathlib import Path

import fixed_engine_declarations as declarations

MODULES = ('P4StubsCore', 'Carrot4', 'AkAudio', 'EasyParticle', 'UIParticle', 'SwitchRuntimeSettings')
MAX_TEXT = 32 * 1024 * 1024
EVIDENCE_MANIFEST = 'registration-evidence.json'


class RegistrationReceipt(dict):
    """JSON-safe public fields plus private same-process validation bindings."""
    def __init__(self, data, bindings, evidence_directory=None, evidence_names=()):
        super().__init__(data)
        self._bindings = bindings
        self._evidence_directory = evidence_directory
        self._evidence_names = frozenset(evidence_names)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path, bound=MAX_TEXT):
    path = canonical(path)
    if not path.is_file() or path.stat().st_size > bound:
        raise ValueError('required bounded build input absent or too large: ' + path.name)
    return path.read_bytes()


def canonical(path):
    """Reject symlink/junction aliases rather than resolving them into writes."""
    lexical = Path(path).absolute()
    if lexical != lexical.resolve():
        raise ValueError('native build path alias refused: ' + lexical.name)
    for ancestor in (lexical, *lexical.parents):
        try:
            info = ancestor.lstat()
        except FileNotFoundError:
            continue
        if ancestor.is_symlink() or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('native build reparse ancestry refused: ' + lexical.name)
    return lexical


def source_files(root):
    """Check each directory before descent; never traverse a reparse subtree."""
    pending, count = [canonical(root)], 0
    while pending:
        directory = pending.pop()
        for entry in sorted(directory.iterdir()):
            entry = canonical(entry)
            count += 1
            if count > 10000:
                raise ValueError('bounded canonical module source inventory exceeded')
            if entry.is_dir():
                pending.append(entry)
            elif entry.is_file():
                yield entry
            else:
                raise ValueError('ordinary native module source file required')


def within(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def evidence_inventory(directory):
    """Inspect one fixed private directory without following foreign subtrees."""
    directory = canonical(directory)
    if not directory.exists():
        return set()
    if not directory.is_dir():
        raise ValueError('private registration evidence directory collision')
    names = set()
    for entry in directory.iterdir():
        if len(names) >= 5:
            raise ValueError('foreign private registration evidence inventory')
        entry = canonical(entry)
        if not entry.is_file():
            raise ValueError('foreign private registration evidence entry')
        names.add(entry.name)
    return names


def prepare_evidence(actions_path, snapshots, actions_sha256):
    """Plan retained full sources; refuse edited/foreign prior evidence first.

    The manifest grants reuse only to its exact bounded, hash-matching files.
    This is build review evidence, never an archive member or runtime proof.
    """
    directory = canonical(actions_path.parent / 'registration-evidence')
    expected = set(snapshots) | {EVIDENCE_MANIFEST}
    existing = evidence_inventory(directory)
    prior = {}
    if existing:
        if existing != expected:
            raise ValueError('foreign private registration evidence inventory')
        manifest_path = directory / EVIDENCE_MANIFEST
        raw = read(manifest_path, 64 * 1024)
        try:
            manifest = json.loads(raw.decode('utf-8'))
        except (ValueError, UnicodeError) as exc:
            raise ValueError('invalid private registration evidence manifest') from exc
        if (not isinstance(manifest, dict)
                or set(manifest) != {'format', 'version', 'actions_sha256', 'files'}
                or manifest['format'] != 'p4-fixed-engine-private-registration-evidence'
                or manifest['version'] != 1
                or not isinstance(manifest['actions_sha256'], str)
                or not re.fullmatch('[0-9a-f]{64}', manifest['actions_sha256'])
                or not isinstance(manifest['files'], dict)
                or set(manifest['files']) != set(snapshots)):
            raise ValueError('invalid private registration evidence manifest')
        prior[manifest_path] = sha(raw)
        for name, expected_hash in manifest['files'].items():
            if not isinstance(expected_hash, str) or not re.fullmatch('[0-9a-f]{64}', expected_hash):
                raise ValueError('invalid private registration evidence hash')
            path = directory / name
            if sha(read(path)) != expected_hash:
                raise ValueError('owned private registration evidence changed: ' + name)
            prior[path] = expected_hash
    writes = []
    hashes = {}
    for name, data in snapshots.items():
        path = canonical(directory / name)
        if path.exists() and not path.is_file():
            raise ValueError('private registration evidence destination collision')
        if len(data) > MAX_TEXT:
            raise ValueError('private registration evidence source bound exceeded')
        writes.append((path, data))
        hashes[name] = sha(data)
    manifest = dict(format='p4-fixed-engine-private-registration-evidence', version=1,
                    actions_sha256=actions_sha256, files=hashes)
    raw = (json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode('utf-8')
    manifest_path = canonical(directory / EVIDENCE_MANIFEST)
    writes.append((manifest_path, raw))
    return directory, expected, existing, prior, writes, hashes


def code_only(text):
    """Retain positions/braces while removing comments and quoted literals."""
    out = list(text)
    pattern = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    for match in pattern.finditer(text):
        for i in range(match.start(), match.end()):
            if out[i] not in '\r\n':
                out[i] = ' '
    return ''.join(out)


def definition(code, pattern):
    matches = list(re.finditer(pattern + r'\s*\{', code))
    if len(matches) != 1:
        raise ValueError('required unique generated definition absent: ' + pattern)
    start, brace = matches[0].start(), matches[0].end() - 1
    depth = 0
    for i in range(brace, len(code)):
        if code[i] == '{':
            depth += 1
        elif code[i] == '}':
            depth -= 1
            if depth == 0:
                return start, i + 1
    raise ValueError('unbalanced generated definition')


def adapt_enum_registration(text, names=tuple(declarations.ENUMS)):
    """Remove whole selected singleton blocks; retain all other source bytes."""
    if not text.startswith('// Copyright') or 'Generated code exported from UnrealHeaderTool.' not in text:
        raise ValueError('stock UHT generated-source marker absent')
    code = code_only(text)
    spans, blocks = [], {}
    for name in names:
        if name not in declarations.ENUMS:
            raise ValueError('generated enum identity outside fixed scope')
        singleton = 'Z_Construct_UEnum_Carrot4_' + name
        start, unused = definition(code, r'\bstatic\s+UEnum\s*\*\s*' + name + r'_StaticEnum\s*\(\s*\)')
        body_start, end = definition(code, r'\bUEnum\s*\*\s*' + singleton + r'\s*\(\s*\)')
        if body_start <= start:
            raise ValueError('generated enum definition order changed')
        block = text[start:end]
        stripped = code[start:end]
        checks = [r'\b' + name + r'_StaticEnum\s*\(\s*\)\s*\{',
                  r'\bStaticEnum\s*<\s*' + name + r'(?:::\s*Type)?\s*>\s*\(\s*\)\s*\{',
                  r'\bFCompiledInDeferEnum\s+Z_CompiledInDeferEnum_UEnum_' + name + r'\s*\(',
                  r'\buint32\s+Get_' + singleton + r'_Hash\s*\(\s*\)\s*\{',
                  r'\bUEnum\s*\*\s*' + singleton + r'\s*\(\s*\)\s*\{']
        if any(len(re.findall(check, stripped)) != 1 for check in checks):
            raise ValueError('generated enum singleton inventory changed: ' + name)
        definitions = re.findall(r'\b(?:U[A-Za-z_0-9]*\s*\*|(?:uint|int)\d+|bool|void)\s+([A-Za-z_][A-Za-z_0-9:]*)\s*\([^;{}]*\)\s*\{', stripped)
        if set(definitions) != {name + '_StaticEnum', singleton, 'Get_' + singleton + '_Hash'}:
            raise ValueError('unexpected function within generated enum block: ' + name)
        if 'Z_Construct_UPackage__Script_Carrot4' not in stripped or 'FindOrConstructDynamicTypePackage' in stripped:
            raise ValueError('generated enum outer/mode changed: ' + name)
        if len(re.findall(r'\bFCompiledInDefer(?:Enum|Struct)?\b', stripped)) != 1:
            raise ValueError('unexpected generated deferred registration: ' + name)
        # No unselected enum/struct/class registration may be swallowed by a span.
        other = re.findall(r'\bZ_Construct_(?:UEnum|UScriptStruct|UClass)[A-Za-z_0-9]*\s*\(\s*\)\s*\{', stripped)
        if len(other) != 1:
            raise ValueError('unexpected generated type registration: ' + name)
        spans.append((start, end, name))
        blocks[name] = sha(block.encode('utf-8'))
    spans.sort()
    if any(left[1] > right[0] for left, right in zip(spans, spans[1:])):
        raise ValueError('selected generated registration blocks overlap')
    output = text
    for start, end, name in reversed(spans):
        output = output[:start] + '// P4 original Engine registration supplied by P4FixedEngineDeclarations.cpp: ' + name + '\n' + output[end:]
    remaining = code_only(output)
    for name in names:
        if re.search(r'\bUEnum\s*\*\s*Z_Construct_UEnum_Carrot4_' + name + r'\s*\(\s*\)\s*\{', remaining):
            raise ValueError('generated enum singleton remains')
    return output, blocks


def adapt_struct_registration(text):
    """Suppress two whole native UHT singleton blocks, never a package literal."""
    if not text.startswith('// Copyright') or 'Generated code exported from UnrealHeaderTool.' not in text:
        raise ValueError('stock UHT generated-source marker absent')
    code, spans, blocks = code_only(text), [], {}
    for name in ('FC4TerrainTag', 'FC4TerrainTagContainer'):
        single = 'Z_Construct_UScriptStruct_' + name
        start, unused = definition(code, r'\b(?:class\s+)?UScriptStruct\s*\*\s*' + name + r'::StaticStruct\s*\(\s*\)')
        unused, end = definition(code, r'\buint32\s+Get_' + single + r'_Hash\s*\(\s*\)')
        if end <= start:
            raise ValueError('generated native struct definition order changed')
        block, stripped = text[start:end], code[start:end]
        checks = [r'\bStaticStruct\s*<\s*' + name + r'\s*>\s*\(\s*\)\s*\{',
                  r'\bFCompiledInDeferStruct\s+Z_CompiledInDeferStruct_UScriptStruct_' + name + r'\s*\(',
                  r'\bUScriptStruct::DeferCppStructOps\s*\(',
                  r'\bstruct\s+' + single + r'_Statics\s*\{',
                  r'\bUScriptStruct\s*\*\s*' + single + r'\s*\(\s*\)\s*\{']
        if any(len(re.findall(check, stripped)) != 1 for check in checks):
            raise ValueError('generated native struct singleton inventory changed: ' + name)
        definitions = re.findall(r'\b(?:U[A-Za-z_0-9]*\s*\*|(?:uint|int)\d+|bool|void\s*\*?)\s+([A-Za-z_][A-Za-z_0-9:]*)\s*\([^;{}]*\)\s*\{', stripped)
        expected = {name + '::StaticStruct', single, single + '_Statics::NewStructOps', 'Get_' + single + '_Hash'}
        if set(definitions) != expected or len(definitions) != 4:
            raise ValueError('unexpected function within generated native struct block: ' + name)
        if len(re.findall(r'\bFCompiledInDefer(?:Enum|Struct)?\b', stripped)) != 1 or 'Z_Construct_UPackage__Script_Carrot4' not in stripped or 'FindOrConstructDynamicTypePackage' in stripped:
            raise ValueError('generated native struct outer/mode/defer changed: ' + name)
        other = re.findall(r'\bZ_Construct_(?:UEnum|UScriptStruct|UClass)[A-Za-z_0-9]*\s*\(\s*\)\s*\{', stripped)
        if len(other) != 1:
            raise ValueError('unexpected generated native type registration: ' + name)
        spans.append((start, end, name))
        blocks[name] = sha(block.encode('utf-8'))
    spans.sort()
    if any(left[1] > right[0] for left, right in zip(spans, spans[1:])):
        raise ValueError('selected generated native struct blocks overlap')
    output = text
    for start, end, name in reversed(spans):
        output = output[:start] + '// P4 original Engine native struct supplied by P4FixedEngineDeclarations.cpp: ' + name + '\n' + output[end:]
    return output, blocks


def graph_inputs(plugin, actions_path, generated_names=('Carrot4StubEnums.gen.cpp',)):
    """Bind all6 links, scoped outputs, and mapped/no-PCH compiler responses."""
    host = plugin.parent.parent
    raw = read(actions_path)
    graph = json.loads(raw.decode('utf-8'))
    actions = graph.get('Actions')
    if not isinstance(actions, list) or not 0 < len(actions) <= 20000:
        raise ValueError('bounded native UBT action graph absent')
    links, compiles, bindings = {}, 0, {actions_path: sha(raw)}
    generated = {name: set() for name in generated_names}
    for action in actions:
        for output in list(action.get('ProducedItems', ())) + list(action.get('DeleteItems', ())):
            path = canonical(output)
            if not within(path, host):
                raise ValueError('action output outside isolated HostProject')
            if action['Type'] == 'Link' and path.suffix.lower() == '.dll':
                for module in MODULES:
                    if path.name == 'UE4Editor-' + module + '.dll':
                        if path != plugin / 'Binaries/Win64' / path.name or module in links:
                            raise ValueError('canonical module DLL output collision')
                        links[module] = sha(json.dumps(action, sort_keys=True).encode('utf-8'))
        dependency = action.get('DependencyListFile')
        if dependency and not within(canonical(dependency), host):
            raise ValueError('compiler dependency output outside isolated HostProject')
        for prerequisite in action.get('PrerequisiteItems', ()):
            path = Path(prerequisite).absolute()
            if path.name in generated:
                path = canonical(path)
                if not within(path, host) or '/Inc/Carrot4/' not in path.as_posix():
                    raise ValueError('generated enum source outside isolated Carrot4 scope')
                generated[path.name].add(path)
        if action['Type'] != 'Compile':
            continue
        # UBT also labels the stock Windows .rc2 resource action Compile.
        # It has no C++ response or __FILE__ strings; qualify its exact stock
        # inputs and single owned module resource output before distinguishing it.
        command = Path(action.get('CommandPath', '')).as_posix().replace('\\', '/')
        if command.lower().endswith('/rc.exe'):
            work = canonical(action.get('WorkingDirectory', ''))
            resource = work.parent / 'Build/Windows/Resources'
            prerequisites = {canonical(value) for value in action.get('PrerequisiteItems', ())}
            outputs = [canonical(value) for value in action.get('ProducedItems', ())]
            arguments = action.get('CommandArguments', '')
            source_args = re.findall(r'"([^"]+\.rc2?)"', arguments, re.I)
            output_arg = re.search(r'(?:^|\s)/fo\s+"([^"]+)"', arguments, re.I)
            expected_outputs = {plugin / 'Intermediate/Build/Win64/UE4Editor/Development' / module / 'Default.rc2.res'
                                for module in MODULES}
            if (not re.search(r'/Windows Kits/10/bin/[^/]+/x64/rc\.exe$', command, re.I)
                    or not work.as_posix().lower().endswith('/engine/source')
                    or prerequisites != {resource / 'Default.rc2', resource / 'Default.ico'}
                    or len(outputs) != 1 or outputs[0] not in expected_outputs
                    or len(source_args) != 1 or canonical(source_args[0]) != resource / 'Default.rc2'
                    or not output_arg or canonical(output_arg.group(1)) != outputs[0]
                    or action.get('DeleteItems') or action.get('CommandDescription') != 'Resource'
                    or action.get('StatusDescription') != 'Default.rc2'):
                raise ValueError('noncanonical Windows resource compiler action refused')
            for path in prerequisites:
                bindings[path] = sha(read(path, 1024 * 1024))
            continue
        compiles += 1
        arguments = action.get('CommandArguments', '')
        responses = re.findall(r'@(?:"([^"]+)"|([^\s]+))', arguments)
        if not responses:
            raise ValueError('compile response file absent')
        for quoted, bare in responses:
            path = canonical(quoted or bare)
            if not within(path, host):
                raise ValueError('compile response outside isolated HostProject')
            contents = read(path, 1024 * 1024)
            bindings[path] = sha(contents)
            arguments += '\n' + contents.decode('utf-8-sig')
        if re.search(r'(?:^|\s)/(?:Yu|Yc)', arguments, re.I):
            raise ValueError('precompiled header compile action refused')
        # UBT uses list2cmdline: paths containing spaces quote the entire
        # argument, rather than only the value after the colon.
        prefixes = re.findall(r'(?:^|\s)(?:"/d1trimfile:([^"]+)"|/d1trimfile:"([^"]+)"|/d1trimfile:([^\s"]+))', arguments, re.I)
        prefixes = [(a or b or c).replace('\\', '/').rstrip('/').lower() for a, b, c in prefixes]
        host_name = host.as_posix().lower()
        if not any(host_name == prefix or host_name.startswith(prefix + '/') for prefix in prefixes):
            raise ValueError('isolated host compiler source-label mapping absent')
        if not any(prefix.endswith('/engine/source') for prefix in prefixes):
            raise ValueError('engine Source compiler source-label mapping absent')
    if set(links) != set(MODULES) or compiles == 0:
        raise ValueError('action graph does not rebuild all six canonical DLLs')
    for name, paths in generated.items():
        if not paths:
            # Exact locations only; a unity wrapper may hide direct prerequisites.
            for root in (plugin, host):
                path = root / 'Intermediate/Build/Win64/UE4Editor/Inc/Carrot4' / name
                if path.is_file():
                    paths.add(canonical(path))
        if len(paths) != 1:
            raise ValueError('unique isolated generated registration source absent: ' + name)
    return {name: paths.pop() for name, paths in generated.items()}, bindings, links, compiles


def prepare_generated_registration(host_plugin, actions_path):
    """Retain review sources, then replace only selected generated registrations."""
    plugin, actions_path = canonical(host_plugin), canonical(actions_path)
    if plugin.parent.name != 'Plugins' or plugin.name != 'P4Stubs':
        raise ValueError('exact isolated HostProject P4Stubs plugin required')
    if not within(actions_path, plugin.parent.parent):
        raise ValueError('action graph outside isolated HostProject')
    manual = read(plugin / declarations.CPP).decode('utf-8')
    terrain_counts = [len(re.findall(r'\bUScriptStruct\s*\*\s*Z_Construct_UScriptStruct_F' + name + r'\s*\(\s*\)\s*\{', code_only(manual)))
                      for name in ('C4TerrainTag', 'C4TerrainTagContainer')]
    if terrain_counts not in ([0, 0], [1, 1]):
        raise ValueError('complete sole first-party native terrain replacements required')
    names = ('Carrot4StubEnums.gen.cpp',) + (('P4FixedEngineDeclarations.gen.cpp',) if terrain_counts == [1, 1] else ())
    generated, bindings, links, compiles = graph_inputs(plugin, actions_path, names)
    mode = plugin / declarations.MODE
    if not declarations.registration_mode(read(mode, 4096).decode('utf-8'))[1]:
        raise ValueError('final manual enum mode must precede UHT/action generation')
    source_hashes = {}
    for module in MODULES:
        source = plugin / 'Source' / module
        if not source.is_dir():
            raise ValueError('canonical module source absent: ' + module)
        for path in source_files(source):
            data = read(path)
            bindings[path] = sha(data)
            source_hashes[path.relative_to(plugin).as_posix()] = sha(data)
    for name in declarations.ENUMS:
        if len(re.findall(r'\bUEnum\s*\*\s*Z_Construct_UEnum_Carrot4_' + name + r'\s*\(\s*\)\s*\{', code_only(manual))) != 1:
            raise ValueError('sole first-party native enum replacement absent: ' + name)
    changes, inventories, snapshots, originals = [], [], {}, {}
    for name in names:
        path = generated[name]
        before = read(path)
        adapt = adapt_enum_registration if name == names[0] else adapt_struct_registration
        adapted, blocks = adapt(before.decode('utf-8'))
        after = adapted.encode('utf-8')
        changes.append((path, after))
        originals[path] = sha(before)
        snapshots[name + '.before.cpp'] = before
        snapshots[name + '.after.cpp'] = after
        inventories.append(dict(name=name, before_sha256=sha(before), after_sha256=sha(after), removed_block_sha256=blocks))
    directory, evidence_names, existing, prior, evidence_writes, evidence_hashes = prepare_evidence(
        actions_path, snapshots, bindings[actions_path])
    if any(within(path, directory) for path in (*bindings, *originals)):
        raise ValueError('private registration evidence overlaps a bound build input')
    # Recheck every input and prior evidence after planning both transformations
    # and all destinations. Edited evidence is never repaired or overwritten.
    if evidence_inventory(directory) != existing:
        raise ValueError('private registration evidence inventory changed')
    for path, expected in {**bindings, **originals, **prior}.items():
        if sha(read(path)) != expected:
            raise ValueError('bound native build input changed before adaptation: ' + path.name)
    # All guards above complete before the first write. No DLL or source inputs
    # are modified; no PDB/binary rewriting or stock Engine mutation occurs.
    directory.mkdir(exist_ok=True)
    for path, contents in evidence_writes:
        canonical(path).write_bytes(contents)
        bindings[path] = sha(contents)
    for path, after in changes:
        canonical(path).write_bytes(after)
        bindings[path] = sha(after)
    public = dict(format='p4-fixed-engine-generated-registration', version=1,
                  actions_sha256=bindings[actions_path], source_files=source_hashes,
                  generated=inventories[0], additional_generated=inventories[1:],
                  private_evidence=dict(files=evidence_hashes,
                                        manifest_sha256=bindings[directory / EVIDENCE_MANIFEST]),
                  canonical_module_link_actions=links, mapped_no_pch_compiles=compiles,
                  runtime_qualified=False)
    receipt = RegistrationReceipt(public, bindings, directory, evidence_names)
    validate_generated_registration(receipt)
    return receipt


def validate_generated_registration(receipt):
    """Read-only before/after Execute gate; serialized public JSON is insufficient."""
    if not isinstance(receipt, RegistrationReceipt):
        raise ValueError('same-process private generated-registration bindings required')
    if receipt._evidence_directory is not None and evidence_inventory(receipt._evidence_directory) != receipt._evidence_names:
        raise ValueError('private registration evidence inventory changed')
    for path, expected in receipt._bindings.items():
        if sha(read(path)) != expected:
            raise ValueError('bound native build input changed: ' + path.name)
    return dict(valid=True, actions_sha256=receipt['actions_sha256'],
                private_evidence_manifest_sha256=receipt['private_evidence']['manifest_sha256'],
                generated_sha256=receipt['generated']['after_sha256'],
                additional_generated_sha256={item['name']: item['after_sha256'] for item in receipt['additional_generated']})
