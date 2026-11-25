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
import build_plugin  # noqa: E402
import gen_headers  # noqa: E402
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


class Descriptor(unittest.TestCase):
    """The shipped descriptor is the 0.2.0 one with EnabledByDefault added, and nothing else changed."""
    # SHA-256 of P4Stubs.uplugin as regenerate.py now writes it.
    ENABLED = 'd4c4dbab6a216bfcb1294763df9e9bce40cf00b6eed606883b4fd728c6a212cc'

    def generated(self):
        # The descriptor gen_headers.py writes for the six modules, in its order.
        modules = ['P4StubsCore', 'AkAudio', 'Carrot4', 'EasyParticle', 'SwitchRuntimeSettings', 'UIParticle']
        return json.dumps(dict(
            FileVersion=3, Version=1, VersionName='0.1', FriendlyName='Pikmin 4 stub classes', Description='stub',
            Category='Pikmin 4', CanContainContent=False, Installed=False,
            Modules=[dict(Name=m, Type='Runtime', LoadingPhase='PreDefault') for m in modules]), indent='\t') + '\n'

    def test_enabled_by_default_and_otherwise_the_shipped_descriptor(self):
        text = stub_build.kadan_descriptor(self.generated(), 'PublicDependencyModuleNames.AddRange(new string[] { '
                                                             '"Paper2D", "ProceduralMeshComponent" });')
        self.assertEqual(hashlib.sha256(text.encode('utf-8')).hexdigest(), self.ENABLED)
        descriptor = json.loads(text)
        self.assertIs(descriptor.pop('EnabledByDefault'), True)
        shipped = json.loads((Path(regenerate.__file__).with_name('shipped-0.2.0.json')).read_text(encoding='utf-8'))
        before = json.dumps(descriptor, sort_keys=True, indent=1) + '\n'
        self.assertEqual(hashlib.sha256(before.encode('utf-8')).hexdigest(), shipped['files']['P4Stubs.uplugin'])

    def test_same_category_as_kadan_and_enabled_by_default(self):
        text = stub_build.kadan_descriptor(self.generated().replace('"Pikmin 4"', '"Other"'), '')
        descriptor = json.loads(text)
        kadan = json.loads((stub_build.ROOT / 'Kadan/Kadan.uplugin').read_text(encoding='utf-8-sig'))
        self.assertEqual(descriptor['Category'], kadan['Category'])
        self.assertIs(descriptor['EnabledByDefault'], True)


class DetailsPanel(unittest.TestCase):
    """The stubs show in the Details panel what the game's editor showed."""

    def specifiers(self, flags, expose='all'):
        generator = gen_headers.Generator.__new__(gen_headers.Generator)
        generator.expose = expose
        field = dict(flags=hex(flags), type_detail=dict(kind='IntProperty'), array_dim=1)
        return generator.specifiers(field, False, False)

    def test_a_property_the_game_edits_is_shown(self):
        made = self.specifiers(gen_headers.F_EDIT)
        self.assertIn('EditAnywhere', made)
        self.assertNotIn('EditCondition', made)
        self.assertNotIn('Category', made)

    def test_a_property_only_kadan_edits_is_hidden(self):
        made = self.specifiers(0)
        self.assertIn('EditAnywhere', made)
        self.assertIn('meta=(EditCondition="false", EditConditionHides)', made)
        self.assertEqual(self.specifiers(0, expose='game'), '')
        self.assertNotIn('Edit', self.specifiers(gen_headers.F_TRANSIENT))


class ActionList(unittest.TestCase):
    def test_every_produced_file_gets_its_folder(self):
        folder = Path(tempfile.mkdtemp())
        actions = folder / 'actions.json'
        dll = folder / 'Plugins/P4Stubs/Binaries/Win64/UE4Editor-Carrot4.dll'
        obj = folder / 'Intermediate/Module.Carrot4.cpp.obj'
        actions.write_text(json.dumps(dict(Actions=[dict(ProducedItems=[str(dll)], PrerequisiteItems=[str(folder / 'x.h')]),
                                                    dict(ProducedItems=[str(obj)])])), encoding='utf-8')
        self.assertEqual(build_plugin.output_folders(actions), sorted({dll.parent, obj.parent}))


class Compare(unittest.TestCase):
    def test_lists_changed_added_removed_and_inputs(self):
        mine = dict(files=dict(a='1', b='2', c='3'), inputs=dict(defaults='x', reflection='y'))
        theirs = dict(files=dict(a='1', b='9', d='4'), inputs=dict(defaults='x', reflection='z'))
        self.assertEqual(regenerate.compare(mine, theirs),
                         dict(changed=['b'], added=['c'], removed=['d'], inputs_changed=['reflection']))


if __name__ == '__main__':
    unittest.main()
