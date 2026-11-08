import importlib.util
import sys
import unittest
from pathlib import Path

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
