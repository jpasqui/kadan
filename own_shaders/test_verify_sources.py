"""Checks for authored shader manifests and the generated payload header."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import verify_sources  # noqa: E402


class ShaderProvenance(unittest.TestCase):
    def setUp(self):
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        self.root = Path(work.name)
        self.shaders = self.root / 'own_shaders'
        (self.shaders / 'source').mkdir(parents=True)
        (self.root / 'core').mkdir()
        self.source = 'own_shaders/source/fixture.vert'
        self.payload = 'own_shaders/fixture.payload'
        self.generator = 'own_shaders/make_payload_header.py'
        self.header = 'core/own_shader_payloads.h'
        (self.root / self.source).write_text('#version 450\nvoid main() {}\n', encoding='utf-8', newline='\n')
        (self.root / self.payload).write_bytes(b'authored fixture payload\x00\x01')
        (self.root / self.generator).write_bytes((HERE / 'make_payload_header.py').read_bytes())
        scope = {'__file__': str(self.root / self.generator), '__name__': 'fixture_payload_generator'}
        exec(compile((self.root / self.generator).read_text(encoding='utf-8'), self.generator, 'exec'), scope)
        (self.root / self.header).write_text(scope['text'](), encoding='utf-8', newline='\n')
        self.manifest = {
            'format': 1,
            'origin': 'project-authored',
            'generation': {
                'gpu_rebuild_equivalence': 'unverified',
                'external_prerequisites': ['NVIDIA OpenGL driver', 'GLSL-to-NV assembly frontend'],
            },
            'sources': {self.source: self.record(self.source)},
            'payloads': {self.payload: {
                **self.record(self.payload, 'raw'), 'source': self.source, 'defines': [],
            }},
            'generators': {self.generator: self.record(self.generator)},
            'generated': {self.header: self.record(self.header)},
        }
        self.write_manifest()

    def record(self, name, mode='utf8-lf'):
        data = (self.root / name).read_bytes()
        if mode == 'utf8-lf':
            data = data.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n').encode('utf-8')
        return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'hash_mode': mode}

    def write_manifest(self):
        (self.shaders / 'PROVENANCE.json').write_text(
            json.dumps(self.manifest, indent=2) + '\n', encoding='utf-8', newline='\n')

    def snapshot(self):
        return {path.relative_to(self.root).as_posix():
                (path.read_bytes(), path.stat().st_mtime_ns)
                for path in self.root.rglob('*') if path.is_file()}

    def test_valid_fixture_is_read_only(self):
        before = self.snapshot()
        report = verify_sources.verify(self.root)
        self.assertIsInstance(report, dict)
        self.assertEqual(self.snapshot(), before)

    def test_source_and_payload_tampering_are_refused(self):
        for name in (self.source, self.payload):
            with self.subTest(name=name):
                path = self.root / name
                original = path.read_bytes()
                path.write_bytes(original + b'changed')
                with self.assertRaises(ValueError):
                    verify_sources.verify(self.root)
                path.write_bytes(original)

    def test_missing_payload_and_unlisted_source_or_payload_are_refused(self):
        path = self.root / self.payload
        original = path.read_bytes()
        path.unlink()
        with self.assertRaises(ValueError):
            verify_sources.verify(self.root)
        path.write_bytes(original)
        for name in ('own_shaders/unlisted.payload', 'own_shaders/source/unlisted.geom'):
            with self.subTest(name=name):
                extra = self.root / name
                extra.write_bytes(b'unlisted authored fixture')
                with self.assertRaises(ValueError):
                    verify_sources.verify(self.root)
                extra.unlink()

    def test_updated_payload_hash_does_not_admit_stale_header(self):
        path = self.root / self.payload
        path.write_bytes(b'a newly compiled payload')
        self.manifest['payloads'][self.payload].update(self.record(self.payload, 'raw'))
        self.write_manifest()
        with self.assertRaises(ValueError):
            verify_sources.verify(self.root)

    def test_updated_header_hash_does_not_admit_stale_generation(self):
        path = self.root / self.header
        path.write_text('// stale header\n', encoding='utf-8', newline='\n')
        self.manifest['generated'][self.header] = self.record(self.header)
        self.write_manifest()
        with self.assertRaises(ValueError):
            verify_sources.verify(self.root)

    def test_checkout_line_endings_do_not_change_text_identity(self):
        for name in (self.source, self.generator, self.header):
            path = self.root / name
            text = path.read_text(encoding='utf-8')
            path.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))
        verify_sources.verify(self.root)

    def test_manifest_paths_and_hash_metadata_are_validated(self):
        original = dict(self.manifest['sources'][self.source])
        for field, value in (('bytes', True), ('sha256', 'x' * 64), ('hash_mode', 'utf16')):
            with self.subTest(field=field):
                self.manifest['sources'][self.source] = {**original, field: value}
                self.write_manifest()
                with self.assertRaises(ValueError):
                    verify_sources.verify(self.root)
        self.manifest['sources'] = {'../outside.vert': original}
        self.write_manifest()
        with self.assertRaises(ValueError):
            verify_sources.verify(self.root)

    def test_payload_cannot_name_an_unrecorded_source(self):
        self.manifest['payloads'][self.payload]['source'] = 'own_shaders/source/missing.vert'
        self.write_manifest()
        with self.assertRaises(ValueError):
            verify_sources.verify(self.root)


if __name__ == '__main__':
    unittest.main()
