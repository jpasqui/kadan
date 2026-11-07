"""Host checks of kadan_fixture.py and test_tiers.py: no editor."""
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kadan_fixture
import test_tiers


class Args:
    def __init__(self, **values):
        self.__dict__.update(values)


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.store = mock.patch.object(kadan_fixture, 'STORE', self.root / 'store')
        self.store.start()
        project = self.root / 'imported'
        (project / 'Content/Maps').mkdir(parents=True)
        (project / 'Config').mkdir()
        (project / 'Saved').mkdir()
        (project / 'Host.uproject').write_text('{}', encoding='utf-8')
        (project / 'Config/DefaultEngine.ini').write_text('[x]\n', encoding='utf-8')
        (project / 'Saved/log.txt').write_text('left out', encoding='utf-8')
        self.asset = project / 'Content/Maps/Cave.umap'
        self.asset.write_bytes(b'map bytes')
        os.chmod(self.asset, stat.S_IREAD)             # Kadan marks the game's packages read-only
        (project / 'Content/Mine.uasset').write_bytes(b'creator bytes')
        kadan_fixture.keep(Args(name='cave', project=str(project), note='test'))

    def tearDown(self):
        self.store.stop()
        for path in self.root.rglob('*'):
            if path.is_file():
                os.chmod(path, stat.S_IWRITE)
        self.temp.cleanup()

    def test_copy_shares_no_file_with_the_fixture(self):
        project = kadan_fixture.make_copy('cave', self.root / 'work/a')
        self.assertEqual(project.name, 'Host.uproject')
        kept = self.root / 'store/cave/Content/Maps/Cave.umap'
        copied = project.parent / 'Content/Maps/Cave.umap'
        self.assertFalse(os.path.samefile(kept, copied))
        self.assertEqual(kept.read_bytes(), copied.read_bytes())
        self.assertFalse(os.path.samefile(self.root / 'store/cave/Config/DefaultEngine.ini',
                                          project.parent / 'Config/DefaultEngine.ini'))
        self.assertFalse((project.parent / 'Saved/log.txt').exists())

    def test_a_save_in_a_copy_does_not_reach_the_fixture(self):
        project = kadan_fixture.make_copy('cave', self.root / 'work/a')
        saved = project.parent / 'Content/Mine.uasset'
        temporary = saved.with_suffix('.tmp')
        temporary.write_bytes(b'saved by the editor')
        os.replace(temporary, saved)                  # how Unreal saves a package
        self.assertEqual((self.root / 'store/cave/Content/Mine.uasset').read_bytes(), b'creator bytes')
        self.assertEqual(kadan_fixture.check(Args(name='cave')), 0)

    def test_a_write_in_place_does_not_reach_the_fixture(self):
        kadan_fixture.make_copy('cave', self.root / 'work/a')
        with open(self.root / 'work/a/Content/Mine.uasset', 'ab') as stream:
            stream.write(b' written in place')
        self.assertEqual(kadan_fixture.check(Args(name='cave')), 0)
        kadan_fixture.make_copy('cave', self.root / 'work/b')

    def test_a_save_survives_another_lane_remaking_its_copy(self):
        # Kadan marks game packages read-only. An editor clears the mark on its copy to save the package,
        # and meanwhile another lane makes its own copy again, which puts the fixture's marks back. A copy
        # that shared the physical file (a hard link) got the mark back too, and the save's delete failed.
        project = kadan_fixture.make_copy('cave', self.root / 'work/b')
        saved = project.parent / 'Content/Maps/Cave.umap'
        os.chmod(saved, stat.S_IWRITE)
        kadan_fixture.make_copy('cave', self.root / 'work/a')
        kadan_fixture.make_copy('cave', self.root / 'work/a')
        self.assertFalse(kadan_fixture.read_only(saved))
        temporary = saved.with_suffix('.tmp')
        temporary.write_bytes(b'saved by the editor')
        os.unlink(saved)                              # Unreal's save deletes the old file, not read-only ones
        os.rename(temporary, saved)
        self.assertTrue(kadan_fixture.read_only(self.root / 'store/cave/Content/Maps/Cave.umap'))

    def test_replacing_a_copy_keeps_the_fixture_marks(self):
        kadan_fixture.make_copy('cave', self.root / 'work/a')
        kadan_fixture.make_copy('cave', self.root / 'work/a')
        self.assertTrue(kadan_fixture.read_only(self.root / 'store/cave/Content/Maps/Cave.umap'))
        self.assertEqual(kadan_fixture.check(Args(name='cave')), 0)

    def test_a_folder_that_is_not_a_copy_is_never_replaced(self):
        other = self.root / 'work/mine'
        other.mkdir(parents=True)
        (other / 'keep.txt').write_text('mine', encoding='utf-8')
        with self.assertRaises(SystemExit):
            kadan_fixture.make_copy('cave', other)
        self.assertTrue((other / 'keep.txt').is_file())

    def test_package_files(self):
        project = kadan_fixture.make_copy('cave', self.root / 'work/a')
        self.assertEqual([p.name for p in kadan_fixture.package_files(project.parent, '/Game/Maps/Cave')], ['Cave.umap'])
        with self.assertRaises(SystemExit):
            kadan_fixture.package_files(project.parent, '/Game/../Config/DefaultEngine')


class TierTests(unittest.TestCase):
    def test_selector_uses_prefixes_only_where_a_group_is_whole(self):
        everything = ['A.X.One', 'A.X.Two', 'A.Y.Fast', 'A.Y.Slow', 'B.Only']
        fast = ['A.X.One', 'A.X.Two', 'A.Y.Fast', 'B.Only']
        self.assertEqual(test_tiers.selector(fast, everything), ['A.X', 'A.Y.Fast', 'B'])
        self.assertEqual(test_tiers.selector(['A.Y.Slow'], everything), ['A.Y.Slow'])

    def test_selector_shortens_names_inside_a_group_that_is_not_whole(self):
        everything = ['A.X.Alpha', 'A.X.Apple', 'A.X.Beta', 'A.X.Slow', 'C.One']
        self.assertEqual(test_tiers.selector(['A.X.Alpha', 'A.X.Apple', 'A.X.Beta'], everything), ['A.X.A', 'A.X.Beta'])

    def test_a_test_that_stopped_the_editor_leaves_its_group_to_run(self):
        everything = ['A.Game.Crash', 'A.Game.Start', 'A.GameLink.Wire', 'A.Other']
        remaining = ['A.Game.Start', 'A.GameLink.Wire', 'A.Other']
        choice = test_tiers.selector(remaining, everything, avoid=['A.Game.Crash'])
        self.assertEqual(choice, ['A.Game.Start', 'A.GameLink.Wire', 'A.Other'])
        for name in remaining:
            self.assertTrue(any(name.startswith(item) for item in choice))
        self.assertFalse(any('A.Game.Crash'.startswith(item) for item in choice))

    def test_selector_refuses_a_name_that_starts_another_tier_name(self):
        with self.assertRaises(ValueError):
            test_tiers.selector(['A.Load'], ['A.Load', 'A.LoadAll'])

    def test_a_shared_start_does_not_widen_past_what_was_checked(self):
        # A.Game is what A.Game.Crash and A.Game.Start share without its dot, and it would pick A.GameLink.Wire.
        everything = ['A.Game.Crash', 'A.Game.Start', 'A.GameLink.Wire']
        choice = test_tiers.selector(['A.Game.Crash', 'A.Game.Start'], everything)
        self.assertFalse(any(test_tiers.picks(item, 'A.GameLink.Wire') for item in choice), choice)
        self.assertTrue(all(any(test_tiers.picks(item, name) for item in choice) for name in ['A.Game.Crash', 'A.Game.Start']))

    def test_filters_pick_by_case_insensitive_substring(self):
        self.assertTrue(test_tiers.picks('kit.native', 'P4MapKit.NativeImport.StrictMapRoundTrip'))
        # A filter that another tier's test contains anywhere, in any case, is not used.
        everything = ['P.Map.Load', 'P.Map.Save', 'Q.X.p.map.L']
        choice = test_tiers.selector(['P.Map.Load', 'P.Map.Save'], everything)
        self.assertFalse(any(test_tiers.picks(item, 'Q.X.p.map.L') for item in choice), choice)
        self.assertTrue(all(any(test_tiers.picks(item, name) for item in choice) for name in ['P.Map.Load', 'P.Map.Save']))
        with self.assertRaises(ValueError):
            test_tiers.selector(['P.Map.Load'], ['P.Map.Load', 'Q.Other.p.map.load.Slow'])

    def test_no_fast_filter_picks_a_test_of_another_tier(self):
        fast, heavy, fresh = test_tiers.tiers(with_fresh=True)
        choice = test_tiers.selector(fast, fast + heavy + fresh)
        for item in choice:
            self.assertEqual([name for name in heavy + fresh if test_tiers.picks(item, name)], [], item)
        self.assertTrue(all(any(test_tiers.picks(item, name) for item in choice) for name in fast))

    def test_the_plugin_tests_split_and_fit_the_command_line(self):
        fast, heavy = test_tiers.tiers()
        self.assertGreater(len(fast), 300)
        self.assertIn('P4MapKit.Kadan.ImportBench', heavy)
        self.assertIn('P4MapKit.Export.AddedActorCave000', heavy)
        self.assertLess(len('+'.join(test_tiers.selector(fast, fast + heavy))), 12000)

    def test_durations_from_a_log(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'tests.log'
            log.write_text(
                '[2026.10.16-12.00.00:000][  0]LogAutomationController: Display: Test Started. Name={One}\n'
                '[2026.10.16-12.00.05:500][  0]LogAutomationController: Display: Test Completed. Result={Passed} Name={One} Path={A.One}\n',
                encoding='utf-8')
            self.assertEqual(test_tiers.durations([log]), {'A.One': 5.5})


if __name__ == '__main__':
    unittest.main()
