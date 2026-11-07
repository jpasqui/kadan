"""Host checks for the pinned stub inputs and the receipt comparison (no Unreal, no real inputs).

    py -3.11 tools/stubs/test_regenerate.py
"""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regenerate  # noqa: E402
import stub_build  # noqa: E402


class PinnedInputs(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.data = b'{"classes": {}}'
        manifest = dict(folder='inputs', inputs=dict(native_engine_evidence=dict(
            file='evidence.json', sha256=hashlib.sha256(self.data).hexdigest(), source='test')))
        (self.folder / 'inputs.json').write_text(json.dumps(manifest), encoding='utf-8')
        (self.folder / 'inputs').mkdir()
        self.saved = stub_build.MANIFEST
        stub_build.MANIFEST = self.folder / 'inputs.json'

    def tearDown(self):
        stub_build.MANIFEST = self.saved

    def test_absent_input_names_its_place(self):
        with self.assertRaisesRegex(ValueError, 'native_engine_evidence is absent'):
            stub_build.input_bytes('native_engine_evidence', self.folder)

    def test_different_bytes_are_refused(self):
        (self.folder / 'inputs/evidence.json').write_bytes(b'{"classes": {"x": 1}}')
        with self.assertRaisesRegex(ValueError, 'not the pinned one'):
            stub_build.input_bytes('native_engine_evidence', self.folder)

    def test_pinned_bytes_are_read(self):
        (self.folder / 'inputs/evidence.json').write_bytes(self.data)
        self.assertEqual(stub_build.input_bytes('native_engine_evidence', self.folder), self.data)


class Compare(unittest.TestCase):
    def test_lists_changed_added_removed_and_inputs(self):
        mine = dict(files=dict(a='1', b='2', c='3'), inputs=dict(defaults='x', reflection='y'))
        theirs = dict(files=dict(a='1', b='9', d='4'), inputs=dict(defaults='x', reflection='z'))
        self.assertEqual(regenerate.compare(mine, theirs),
                         dict(changed=['b'], added=['c'], removed=['d'], inputs_changed=['reflection']))


if __name__ == '__main__':
    unittest.main()
