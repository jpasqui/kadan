"""The placement layouts header is what its JSON gives, and the generator refuses what it cannot write."""
import json
import unittest
import make_placement_layouts as layouts


class PlacementLayouts(unittest.TestCase):
    def test_header_is_current(self):
        made = layouts.generate(json.loads(layouts.SOURCE.read_text(encoding='utf-8')))
        self.assertEqual(layouts.HEADER.read_text(encoding='utf-8'), made,
                         'run python tools/make_placement_layouts.py after editing core/placement_layouts.json')

    def test_lists_are_expanded_and_counted(self):
        data = dict(
            types=[dict(name='Pair', kind='struct', members=[dict(name='A', type='i32'), dict(name='B', type='f32')])],
            lists=[dict(name='Tail', fields=[dict(name='T', type='bool', gate='>0x5')])],
            slots=[dict(row='AI', half='Static', id='AIStatic', chain='component', owner=dict(classes=['X', 'Y']))],
            classes=[dict(**{'class': 'X'}, slot='AI.Static', base='replace',
                          fields=[dict(name='F', type='Pair'), '@Tail'])])
        made = layouts.generate(data)
        self.assertIn('{"Pair", "struct", "", "", 0, 2},', made)
        self.assertIn('{"AI", true, Slot::AIStatic, \'c\', 2, 0, "", "X,Y", ""},', made)
        self.assertIn('{"X", Slot::AIStatic, true, 0, 2, 2},', made)
        self.assertIn('{"T", "bool", \'>\', UINT64_C(0x5),', made)
        marked = layouts.generate(dict(data, classes=[dict(**{'class': 'X'}, slot='AI.Static', base='replace',
                                                           fields=[dict(name='L', type='i32', derived=True, from_actor=True)])]))
        self.assertIn('"", "", false, false, "", "", true, "", "", "", ""},', marked)
        filled = layouts.generate(dict(data, classes=[dict(**{'class': 'X'}, slot='AI.Static', base='replace',
                                                           fields=[dict(name='P', type='vec', derived=True, fill='place',
                                                                        fill_class='AkComponent')])]))
        self.assertIn('false, "place", "AkComponent", "", ""},', filled)
        for bad in (dict(name='P', type='vec', fill='place'), dict(name='P', type='vec', derived=True, fill_tag='T')):
            with self.assertRaises(ValueError):
                layouts.generate(dict(data, classes=[dict(**{'class': 'X'}, slot='AI.Static', base='replace', fields=[bad])]))

    def test_complete_owners_are_listed_and_checked(self):
        base = dict(types=[], lists=[], classes=[],
                    slots=[dict(row='AI', half='Static', id='AIStatic', chain='component', owner=dict(classes=['X']))])
        self.assertIn('return {Rows, 0};', layouts.generate(base))
        made = layouts.generate(dict(base, complete=[dict(slot='AI.Static', classes=['XAIComponent', 'YAIComponent'])]))
        self.assertIn('{Slot::AIStatic, "XAIComponent"},', made)
        self.assertIn('return {Rows, 2};', made)
        for complete in ([dict(slot='Life.Dynamic', classes=['X'])], [dict(slot='AI.Static', classes=['X', 'X'])],
                         [dict(slot='AI.Static', classes=['X'], why='checked')],
                         [dict(slot='AI.Static', classes=['X']), dict(slot='AI.Static', classes=['X'])]):
            with self.assertRaises(ValueError):
                layouts.generate(dict(base, complete=complete))

    def test_refuses_unknown_keys_lists_and_slots(self):
        base = dict(types=[], lists=[], slots=[dict(row='AI', half='Static', id='AIStatic', owner={})])
        for classes in ([dict(**{'class': 'X'}, slot='AI.Static', base='replace', fields=['@Missing'])],
                        [dict(**{'class': 'X'}, slot='AI.Static', base='replace', fields=[dict(name='F', type='i32', typo=1)])],
                        [dict(**{'class': 'X'}, slot='Life.Dynamic', base='replace', fields=[])],
                        [dict(**{'class': 'X'}, slot='AI.Static', base='sideways', fields=[])]):
            with self.assertRaises(ValueError):
                layouts.generate(dict(base, classes=classes))


if __name__ == '__main__':
    unittest.main()
