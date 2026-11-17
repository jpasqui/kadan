import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import material_coverage
import material_gallery


class MaterialCoverageTest(unittest.TestCase):
    def test_page_is_current(self):
        table = material_coverage.load()
        self.assertEqual(material_coverage.PAGE.read_text(encoding='utf-8'), material_coverage.render(table) + '\n',
                         'run tools/material_coverage.py render')

    def test_every_row_has_a_known_status(self):
        for row in material_coverage.load()['rows']:
            self.assertIn(row.get('status'), material_coverage.STATUS, row['id'])
            self.assertIn(row['family'], [f for f, _ in material_coverage.FAMILIES], row['id'])
            if row['status'] == 'game':
                self.assertTrue(row.get('seen'), row['id'] + ' is marked seen without saying what was seen')

    def test_merge_keeps_what_was_seen_and_takes_a_node_attempt_ue4_compiles(self):
        table = {'rows': [{'family': 'translucency', 'id': 'FogOnLit', 'feature': 'f', 'status': 'game',
                           'seen': 'drawn'}]}
        report = [
            {'family': 'translucency', 'id': 'FogOnLit', 'feature': 'f', 'result': 'compiles'},
            {'family': 'nodes', 'id': 'Add', 'feature': 'Add node (inputs: unconnected)', 'result': 'ue4-refuses',
             'editor_errors': 'Missing input'},
            {'family': 'nodes', 'id': 'Add', 'feature': 'Add node (inputs: scalar)', 'result': 'game-refuses',
             'game_error': 'no'},
            {'family': 'blend', 'id': 'X', 'feature': 'x', 'result': 'not-built'},
        ]
        rows = {r['id']: r for r in material_coverage.merge(table, report)['rows']}
        self.assertEqual(rows['FogOnLit']['status'], 'game')
        self.assertEqual(rows['FogOnLit']['seen'], 'drawn')
        self.assertEqual(rows['Add']['status'], 'refused')
        self.assertEqual(rows['Add']['feature'], 'Add node')
        self.assertNotIn('X', rows)

    def test_merge_prunes_only_when_asked_and_only_the_reported_families(self):
        table = {'rows': [{'family': 'blend', 'id': 'Gone', 'feature': 'g', 'status': 'compiles'},
                          {'family': 'blend', 'id': 'Kept', 'feature': 'k', 'status': 'game', 'seen': 'drawn'},
                          {'family': 'nodes', 'id': 'Other', 'feature': 'o', 'status': 'compiles'}]}
        report = [{'family': 'blend', 'id': 'Kept', 'feature': 'k', 'result': 'compiles'}]
        kept = {r['id'] for r in material_coverage.merge(json.loads(json.dumps(table)), report)['rows']}
        self.assertEqual(kept, {'Gone', 'Kept', 'Other'})
        pruned = {r['id']: r for r in material_coverage.merge(json.loads(json.dumps(table)), report, prune=True)['rows']}
        self.assertEqual(set(pruned), {'Kept', 'Other'})
        self.assertEqual(pruned['Kept']['status'], 'game')

    def test_merge_carries_a_marks_provenance_and_drops_it_with_the_mark(self):
        table = {'rows': [{'family': 'blend', 'id': 'Seen', 'feature': 's', 'status': 'game', 'seen': 'drawn',
                           'commit': 'abc1234', 'shader_format': 29},
                          {'family': 'blend', 'id': 'Lost', 'feature': 'l', 'status': 'game', 'seen': 'drawn',
                           'commit': 'abc1234', 'shader_format': 29}]}
        report = [{'family': 'blend', 'id': 'Seen', 'feature': 's', 'result': 'compiles'},
                  {'family': 'blend', 'id': 'Lost', 'feature': 'l', 'result': 'game-refuses', 'game_error': 'no'}]
        rows = {r['id']: r for r in material_coverage.merge(table, report)['rows']}
        self.assertEqual((rows['Seen']['commit'], rows['Seen']['shader_format']), ('abc1234', 29))
        for key in ('seen', 'commit', 'shader_format'):
            self.assertNotIn(key, rows['Lost'])

    def test_the_shader_format_version_is_read_from_its_source(self):
        self.assertGreater(material_coverage.shader_format_version(), 0)
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'KadanShaderFormat.cpp'
            source.write_text('constexpr uint32 FormatVersion = 41; // why\n', encoding='utf-8')
            self.assertEqual(material_coverage.shader_format_version(source), 41)
            source.write_text('constexpr uint32 OtherVersion = 41;\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                material_coverage.shader_format_version(source)
            with self.assertRaises(ValueError):
                material_coverage.shader_format_version(Path(folder) / 'missing.cpp')

    def run_seen(self, folder, commit, *args):
        table = Path(folder) / 'table.json'
        if not table.exists():
            table.write_text(json.dumps({'rows': [
                {'family': 'blend', 'id': 'Old', 'feature': 'o', 'status': 'game', 'seen': 'drawn (2026-01-01)'},
                {'family': 'blend', 'id': 'New', 'feature': 'n', 'status': 'compiles'}]}), encoding='utf-8')
        with mock.patch.object(material_coverage, 'TABLE', table), \
                mock.patch.object(material_coverage, 'PAGE', Path(folder) / 'page.md'), \
                mock.patch.object(material_coverage, 'git_commit', return_value=commit):
            code = material_coverage.main(['seen', *args])
        return code, {r['id']: r for r in json.loads(table.read_text(encoding='utf-8'))['rows']}

    def test_a_mark_records_the_commit_and_shader_format_and_old_rows_stay_valid(self):
        version = material_coverage.shader_format_version()
        with tempfile.TemporaryDirectory() as folder:
            code, rows = self.run_seen(folder, 'abc1234', 'New', '--note', 'drawn', '--date', '2026-11-17')
            self.assertEqual(code, 0)
            self.assertEqual(rows['New']['status'], 'game')
            self.assertEqual(rows['New']['seen'], 'drawn (2026-11-17)')
            self.assertEqual((rows['New']['commit'], rows['New']['shader_format']), ('abc1234', version))
            self.assertEqual(rows['Old'], {'family': 'blend', 'id': 'Old', 'feature': 'o', 'status': 'game',
                                           'seen': 'drawn (2026-01-01)'})
            page = (Path(folder) / 'page.md').read_text(encoding='utf-8')
            self.assertIn('drawn (2026-11-17) [commit abc1234, shader format %d]' % version, page)
            self.assertIn('| drawn (2026-01-01) |', page)
            # Without git the mark has no commit, and a mark made again replaces the old one's.
            code, rows = self.run_seen(folder, None, 'New', '--draws', '--note', 'again')
            self.assertEqual(code, 0)
            self.assertNotIn('commit', rows['New'])
            self.assertEqual(rows['New']['shader_format'], version)

    def test_a_mark_fails_without_a_shader_format_version(self):
        with tempfile.TemporaryDirectory() as folder:
            with mock.patch.object(material_coverage, 'SHADER_FORMAT', Path(folder) / 'missing.cpp'):
                code, rows = self.run_seen(folder, 'abc1234', 'New', '--note', 'drawn')
            self.assertEqual(code, 1)
            self.assertEqual(rows['New']['status'], 'compiles')


class MaterialGalleryTest(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec('lupa'), 'needs lupa (pip install lupa) to run the Lua')
    def test_lua_parses_and_lists_every_material(self):
        gallery = {'cube': '/Game/Mods/m_1/SM_Cube', 'materials': [
            {'material': 'M_Cov000_A', 'runtime': '/Game/Mods/m_1/_KadanRuntimeMaterials/M_Cov000_A'},
            {'material': 'M_Cov001_B"', 'runtime': '/Game/Mods/m_1/_KadanRuntimeMaterials/M_Cov001_B.M_Cov001_B'}]}
        source = material_gallery.build(gallery)
        self.assertIn('"/Game/Mods/m_1/SM_Cube.SM_Cube"', source)
        self.assertIn('M_Cov000_A.M_Cov000_A', source)
        self.assertEqual(material_gallery.build(gallery, only=['B']).count('_KadanRuntimeMaterials'), 1)
        import lupa
        lua = lupa.LuaRuntime()
        lua.execute('return function() ' + source + ' end')


if __name__ == '__main__':
    unittest.main()
