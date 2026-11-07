"""Relative diagnostic labels for an isolated release source stage only.

Never changes canonical sources, engine files, assertions or compiled binaries.
The actual final DLL audit remains authoritative.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

CANONICAL = Path(__file__).resolve().parents[1] / 'Kadan'
STUB_CANONICAL = Path(__file__).resolve().parents[1] / 'private/blueprints/host/Plugins/P4Stubs'
EXTENSIONS = {'.cpp', '.h', '.hpp', '.inl'}


def label_first_party_sources(plugin, plugin_name='Kadan'):
    plugin = Path(plugin).resolve()
    canonical = {'Kadan': CANONICAL, 'P4Stubs': STUB_CANONICAL}
    if (plugin_name not in canonical or plugin == canonical[plugin_name].resolve()
            or not (plugin / (plugin_name + '.uplugin')).is_file()):
        raise ValueError('Relative labels require an isolated plugin release stage')
    source = plugin / 'Source'
    if (source.is_symlink() or getattr(source, 'is_junction', lambda: False)()
            or not source.is_dir() or not source.resolve().is_relative_to(plugin)):
        raise ValueError('Source stage is missing or linked')
    try:
        from .native_core_sources import prepare_source_labels, finish_source_labels
    except ImportError:
        from native_core_sources import prepare_source_labels, finish_source_labels
    core_receipt = prepare_source_labels(plugin)
    records = {}
    for path in sorted(source.rglob('*')):
        if not path.is_file() or path.suffix.lower() not in EXTENSIONS:
            continue
        if path.is_symlink() or path.stat().st_nlink != 1 or not path.resolve().is_relative_to(source.resolve()):
            raise ValueError('Source label target escapes stage')
        relative = path.relative_to(plugin).as_posix()
        label = plugin_name + '/' + relative
        if not re.fullmatch(r'[A-Za-z0-9_./-]+', label):
            raise ValueError('Unsafe diagnostic source label')
        original = path.read_bytes()
        direct = ('#line 1 "' + label + '"\n').encode('utf-8')
        # Headers must retain their physical lines: UHT uses those lines for
        # GENERATED_BODY identifiers. MSVC rejects #line 0, and positive guarded
        # labels would change __LINE__. The qualified compiler trim handles
        # header filenames during the final rebuild instead.
        header = path.suffix.lower() != '.cpp'
        legacy_guard = ('#ifdef __cplusplus\n#line 0 "' + label + '"\n#endif\n').encode('utf-8')
        prefix = b'' if header else direct
        # Keep an optional UTF-8 signature at the beginning of the translation
        # unit rather than accidentally inserting it after the new directive.
        bom = b'\xef\xbb\xbf' if original.startswith(b'\xef\xbb\xbf') else b''
        body = original[len(bom):]
        body.decode('utf-8')
        # Upgrade only the exact earlier stage prefix, preserving source text.
        if header:
            if body.startswith(legacy_guard):
                body = body[len(legacy_guard):]
            elif body.startswith(direct):
                body = body[len(direct):]
        content = bom + body if body.startswith(prefix) else bom + prefix + body
        if content != original:
            path.write_bytes(content)
        records[relative] = dict(label=None if header else label,
                                label_policy='compiler-trim' if header else 'line-directive',
                                sha256=hashlib.sha256(content).hexdigest())
    # The native Maxwell stage records its adapted bytes. Keep that first-party
    # provenance accurate after adding a release-only diagnostic directive.
    transformed = {name: row['sha256'] for name, row in records.items()}
    for manifest in source.rglob('SOURCE_PROVENANCE.json'):
        if (manifest.is_symlink() or manifest.stat().st_nlink != 1
                or not manifest.resolve().is_relative_to(source.resolve())):
            raise ValueError('Provenance manifest escapes stage')
        data = json.loads(manifest.read_text(encoding='utf-8'))
        for name, record in data.get('files', {}).items():
            if not isinstance(name, str) or Path(name).name != name:
                raise ValueError('Unsafe compiler provenance filename')
            path = manifest.parent / name
            relative = path.relative_to(plugin).as_posix()
            if relative in records:
                record['staged_sha256'] = records[relative]['sha256']
                record['diagnostic_label'] = records[relative]['label']
                record['diagnostic_label_policy'] = records[relative]['label_policy']
        content = (json.dumps(data, indent=2, sort_keys=True) + '\n').encode('utf-8')
        if manifest.read_bytes() != content:
            manifest.write_bytes(content)
        transformed[manifest.relative_to(plugin).as_posix()] = hashlib.sha256(content).hexdigest()
    finish_source_labels(plugin, core_receipt, transformed)
    return dict(format=1, files=records)


def trim_arguments(prefixes):
    """Installed MSVC capability must be probed before using this internal flag."""
    return ['/d1trimfile:' + str(Path(value).resolve()).rstrip('\\/') + '\\' for value in prefixes]


def decode_preprocessor_lines(stdout):
    """Decode emitted ASCII C string literals before interpreting path slashes."""
    emitted = []
    for line in stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        if len(line) > 8192:
            raise ValueError('Preprocessor literal exceeds bound')
        if line.startswith('"'):
            try:
                value = json.loads(line)
            except (json.JSONDecodeError, UnicodeError) as error:
                raise ValueError('Malformed preprocessor C string literal') from error
            if not isinstance(value, str) or any(not 32 <= ord(character) <= 126 for character in value):
                raise ValueError('Preprocessor literal is not printable ASCII')
            emitted.append(value)
        elif re.fullmatch(r'[0-9]+', line):
            emitted.append(line)
        else:
            raise ValueError('Unexpected preprocessor literal or line number')
        if len(emitted) > 128:
            raise ValueError('Preprocessor output exceeds line bound')
    return emitted


def probe_trim_arguments(compiler, folder, prefixes, environment=None):
    """Preprocess source-label constants using the exact release compiler.

    Returns only sanitized booleans. Generated probe source stays in the private
    release output, and never enters a product or the active development host.
    """
    folder = Path(folder).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    prefixes = [Path(value).resolve() for value in prefixes]
    if not prefixes:
        raise ValueError('At least one source prefix is required')
    labels = [str(value / ('SourceLabelProbe' + str(i) + '.h')).replace('\\', '/')
              for i, value in enumerate(prefixes)]
    source = folder / 'source-label-probe.cpp'
    text = ''.join('#line 1 "' + value + '"\n__FILE__\n' for value in labels)
    text += '#line 1 "Kadan/SourceLabelProbe.cpp"\n__FILE__\n'
    text += ('#define P4_BODY_INNER(line) P4_BODY_##line\n'
             '#define P4_BODY_AT(line) P4_BODY_INNER(line)\n'
             '#define P4_BODY() P4_BODY_AT(__LINE__)\n'
             '#define P4_BODY_3 "physical-body-3"\n')
    for extension in ('.h', '.hpp', '.inl'):
        name = 'source-label-header' + extension
        (folder / name).write_text('__FILE__\n__LINE__\nP4_BODY()\n', encoding='utf-8')
        text += '#include "' + name + '"\n'
    source.write_text(text, encoding='utf-8')
    arguments = trim_arguments(prefixes)
    command = [str(compiler), '/nologo', '/EP', *arguments, str(source)]
    if environment is not None:
        # The canonical UAT compile receives these through MSVC's inherited
        # appended-option environment, so prove that exact delivery path.
        environment = dict(environment)
        environment['_CL_'] = (environment.get('_CL_', '') + ' ' + subprocess.list2cmdline(arguments)).strip()
        command = [str(compiler), '/nologo', '/EP', str(source)]
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
    malformed = False
    try:
        emitted = decode_preprocessor_lines(result.stdout)
    except ValueError:
        emitted, malformed = [], True
    relative = len(emitted) == len(labels) + 10 and all(
        not re.search(r'(?i)(?:[a-z]:[\\/]|\\\\|/Users/|\\Users\\)', line) for line in emitted)
    headers = emitted[len(labels) + 1:]
    original_lines = len(headers) == 9 and all(headers[i] == '2' for i in (1, 4, 7))
    macro_lines = len(headers) == 9 and all(headers[i] == 'physical-body-3' for i in (2, 5, 8))
    ignored = 'D9002' in result.stderr or 'D8021' in result.stderr
    return dict(ok=result.returncode == 0 and relative and original_lines and macro_lines and not ignored,
                exit=result.returncode, prefixes=len(prefixes), relative_labels=relative,
                header_original_lines=original_lines, generated_macro_lines=macro_lines,
                ignored_option=ignored, malformed_literals=malformed)


def _original_default_literal(proof, module_name):
    """Only the two source-qualified Carrot4 FString defaults may be exempted."""
    if module_name != 'UE4Editor-Carrot4.dll':
        raise ValueError('Original default evidence belongs to Carrot4 only')
    proof = Path(proof)
    if not proof.is_file() or proof.stat().st_size > 1 << 20:
        raise ValueError('Missing or oversized original default evidence')
    data = json.loads(proof.read_text(encoding='utf-8'))
    if not isinstance(data, dict):
        raise ValueError('Original default evidence must be an object')
    literal = 'C:/home/Project/carrrot4u/Saved/Carrot4/Saved'
    expected = {
        ('/Script/Carrot4.GameConfig', '', 'SaveLoadConfig.HostMountPath', literal, 'Text'),
        ('/Script/Carrot4.GameConfigActor', 'GameConfig', 'SaveLoadConfig.HostMountPath', literal, 'Text'),
    }
    rows = data.get('allowed_original_default_rows', [])
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError('Original default rows must be objects')
    fields = ('class', 'subobject', 'property', 'value', 'kind')
    original_property=data.get('original_property')
    if (data.get('format') != 'p4-fixed-declarations-original-defaults-proof'
            or type(data.get('version')) is not int or data['version'] != 1
            or not isinstance(original_property,dict) or type(original_property.get('array_dim')) is not int
            or data.get('source_executable_sha256') != 'fe616565ecfd35868fdd9a43525931a3bd46c383151e2a244cdf8e199a974190'
            or data.get('original_property') != dict(struct='/Script/Carrot4.SaveLoadConfig',
                name='HostMountPath', kind='StrProperty', status='observed', array_dim=1,
                flags='0x18000000000205')
            or len(rows) != 2 or {tuple(row.get(key) for key in fields) for row in rows} != expected):
        raise ValueError('Original default evidence does not match the qualified rows')
    return literal


def audit_dll_source_labels(path, prefixes=(), original_defaults_proof=None):
    """Scan final module strings; return counts/errors, never matched paths.

    Prefixes should cover the entire owned release/project root and engine Source
    root, thereby including generated UHT sources and the retained HostProject.
    Detects generic absolute drive/UNC paths as well as both supplied prefixes.
    """
    path = Path(path)
    if not path.is_file() or not 0 < path.stat().st_size <= 512 * 1024 * 1024:
        return dict(ok=False, errors=['missing-or-oversized-module'])
    data = path.read_bytes()
    if data[:2] != b'MZ':
        return dict(ok=False, errors=['module-is-not-PE'])
    allowed_literal = None
    if original_defaults_proof is not None:
        try:
            allowed_literal = _original_default_literal(original_defaults_proof, path.name)
        except (ValueError, OSError, TypeError, KeyError):
            return dict(ok=False, errors=['invalid-original-default-evidence'])
    normalized_prefixes = [str(Path(value).resolve()).replace('\\', '/').casefold().rstrip('/')
                           for value in prefixes]
    values = [('utf8', value.decode('utf-8', errors='replace'))
              for value in re.findall(rb'[\x20-\x7e]{5,}', data)]
    values += [('utf16', value.decode('utf-16-le'))
               for value in re.findall(rb'(?:[\x20-\x7e]\x00){5,}', data)]
    counts = dict(absolute_path_strings=0, user_path_strings=0,
                  source_path_strings=0, prefix_matches=0, utf8=0, utf16=0,
                  qualified_original_default_strings=0)
    # A drive path starts its string, after at most one neighboring binary byte that joined
    # the extracted sequence, or follows a space, a quote, '=', '(' or '['. A type tag such as
    # struct:/Script/Engine.Guid, or code bytes that read as ,f:\fh6\, is not one. URI ://
    # syntax is rejected explicitly so URLs never masquerade as a local drive path.
    lead = r'(?:^.?|(?<=[\s"\'=(\[]))'
    absolute = re.compile(r'(?:' + lead + r'[A-Za-z]:(?:\\|/(?!/))|\\\\[A-Za-z0-9_.-]+[\\/])')
    # Narrow strings are the module's own literals, where the wide rule also matches a
    # virtual path such as save:/tsuchi/... and code bytes that read as Z:\T^. There a
    # drive path is a drive letter then a named folder.
    narrow = re.compile(r'(?:' + lead + r'[A-Za-z]:[\\/][A-Za-z0-9_$][A-Za-z0-9_. $~()-]*[\\/]|\\\\[A-Za-z0-9_.-]+[\\/])')
    for encoding, value in values:
        # Exact whole-string equality: a source/assertion path containing this
        # original game value still fails, as does every other C:/home path.
        if allowed_literal is not None and value == allowed_literal:
            counts['qualified_original_default_strings'] += 1
            continue
        normalized = value.replace('\\\\', '\\').replace('\\', '/').casefold()
        is_absolute = bool((narrow if encoding == 'utf8' else absolute).search(value))
        matches_prefix = any(prefix in normalized for prefix in normalized_prefixes)
        if not is_absolute and not matches_prefix:
            continue
        counts['absolute_path_strings'] += int(is_absolute)
        counts['user_path_strings'] += int(bool(re.search(r'[a-z]:/users/', normalized)))
        counts['source_path_strings'] += int(bool(re.search(r'\.(?:cpp|h|hpp|inl|cs|pdb)\b', normalized)))
        counts['prefix_matches'] += int(matches_prefix)
        counts[encoding] += 1
    errors = ['module-retains-absolute-source-or-private-paths'] if counts['utf8'] or counts['utf16'] else []
    return dict(ok=not errors, errors=errors, counts=counts, sha256=hashlib.sha256(data).hexdigest())
