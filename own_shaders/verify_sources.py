"""Check the recorded shader sources, payloads, and embedded header without building shaders.

    python own_shaders/verify_sources.py --check
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = 'own_shaders/PROVENANCE.json'
GENERATOR = 'own_shaders/make_payload_header.py'
HEADER = 'core/own_shader_payloads.h'
MAX_FILE = 8 * 1024 * 1024


def file_path(root, relative):
    """One regular repository file with no linked directory in its path."""
    if not isinstance(relative, str):
        raise ValueError('Manifest paths must be text')
    path = PurePosixPath(relative)
    if path.is_absolute() or path.as_posix() != relative or '..' in path.parts or '\\' in relative or ':' in relative:
        raise ValueError('Manifest path escapes the repository')
    full = root / relative
    for current in (full, *full.parents):
        if current == root:
            break
        if current.is_symlink() or (current.exists() and bool(getattr(current.lstat(), 'st_file_attributes', 0) & 0x400)):
            raise ValueError('Linked shader input: ' + relative)
    if not full.is_file() or not stat.S_ISREG(full.stat().st_mode) or full.stat().st_size > MAX_FILE:
        raise ValueError('Missing, non-regular, or oversized shader input: ' + relative)
    return full


def file_bytes(path, mode):
    raw = path.read_bytes()
    if len(raw) > MAX_FILE:
        raise ValueError('Shader input exceeds its size bound')
    if mode == 'utf8-lf':
        return raw.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n').encode('utf-8')
    return raw


def check_entries(root, entries, mode):
    if not isinstance(entries, dict) or not entries:
        raise ValueError('Shader manifest sections must be nonempty mappings')
    for name, entry in entries.items():
        path = file_path(root, name)
        if not isinstance(entry, dict) or type(entry.get('bytes')) is not int or entry['bytes'] < 0 or \
                not isinstance(entry.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', entry['sha256']) or \
                entry.get('hash_mode') != mode:
            raise ValueError('Invalid hash record: ' + name)
        data = file_bytes(path, mode)
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Shader input hash or size differs: ' + name)


def verify(root=ROOT):
    root = Path(root).resolve()
    manifest = json.loads(file_path(root, MANIFEST).read_text(encoding='utf-8'))
    if not isinstance(manifest, dict) or type(manifest.get('format')) is not int or manifest['format'] != 1 or \
            manifest.get('origin') != 'project-authored':
        raise ValueError('Unsupported shader provenance format')
    generation = manifest.get('generation')
    if not isinstance(generation, dict) or generation.get('gpu_rebuild_equivalence') != 'unverified' or \
            not isinstance(generation.get('external_prerequisites'), list) or \
            not generation['external_prerequisites'] or \
            any(not isinstance(item, str) or not item for item in generation['external_prerequisites']):
        raise ValueError('Shader generation prerequisites and verification limits must be recorded')
    sources, payloads = manifest.get('sources'), manifest.get('payloads')
    generators, generated = manifest.get('generators'), manifest.get('generated')
    check_entries(root, sources, 'utf8-lf')
    check_entries(root, payloads, 'raw')
    check_entries(root, generators, 'utf8-lf')
    check_entries(root, generated, 'utf8-lf')
    if set(generators) != {GENERATOR} or set(generated) != {HEADER}:
        raise ValueError('Shader generator and embedded-header inventory differs')
    for name in sources:
        path = PurePosixPath(name)
        if not name.startswith('own_shaders/source/') or path.suffix not in ('.vert', '.geom'):
            raise ValueError('Unexpected authored shader source: ' + name)
    for name, entry in payloads.items():
        path = PurePosixPath(name)
        if path.parent.as_posix() != 'own_shaders' or path.suffix != '.payload':
            raise ValueError('Unexpected shader payload path: ' + name)
        if entry.get('source') not in sources or not isinstance(entry.get('defines'), list) or \
                any(not isinstance(item, str) or not item for item in entry['defines']):
            raise ValueError('Payload source and defines are missing: ' + name)
    source_folder = root / 'own_shaders/source'
    source_paths = list(source_folder.rglob('*'))
    if any(path.is_symlink() or bool(getattr(path.lstat(), 'st_file_attributes', 0) & 0x400) for path in source_paths):
        raise ValueError('Linked directory or file in the authored shader sources')
    actual_sources = {path.relative_to(root).as_posix() for path in source_paths if path.is_file()}
    actual_payloads = {path.relative_to(root).as_posix() for path in (root / 'own_shaders').glob('*.payload')}
    if actual_sources != set(sources) or actual_payloads != set(payloads):
        raise ValueError('Shader files differ from the recorded source or payload inventory')
    # Execute the existing owned generator in memory so this check writes no header or bytecode cache.
    generator = root / GENERATOR
    scope = {'__file__': str(generator), '__name__': '_kadan_payload_header_check'}
    exec(compile(generator.read_text(encoding='utf-8'), str(generator), 'exec'), scope)
    wanted = scope['text']()
    have = (root / HEADER).read_text(encoding='utf-8')
    if have != wanted:
        raise ValueError('Embedded shader header differs from its recorded payloads')
    return {'sources': len(sources), 'payloads': len(payloads), 'embedded_header': 'matched',
            'gpu_rebuild_equivalence': generation['gpu_rebuild_equivalence']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='verify inputs without writing generated files')
    parser.parse_args()
    try:
        print(json.dumps(verify(), sort_keys=True))
    except (ValueError, OSError, UnicodeError, KeyError) as error:
        parser.exit(1, 'Shader source verification failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
