import json
from pathlib import Path
import tempfile
import unittest
import kadan_sweep_gallery as gallery


class CaptureGate(unittest.TestCase):
    def test_legacy_declaration_refusals_without_asset_replies_are_not_lost_or_duplicated(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work);report=root/'import.json'
            report.write_text(json.dumps(dict(assets={'/Game/Existing':dict(valid=False,kind='DataTable',error='Own recorded refusal')},
                game_reference_reasons={'/game/existing':'Repeated reference diagnosis','/Game/Deferred':'its required class was not successfully declared with the native type: Class_C'})),encoding='utf-8')
            gallery.record(dict(map='/Game/Map',variant='day'),{},root,report)
            row=json.loads((root/'Map_day.row.json').read_text(encoding='utf-8'))
            self.assertEqual(len(row['refusals']),2)
            self.assertEqual(row['refusals'][0]['reason'],'Own recorded refusal')
            self.assertIn('required generated class was unavailable',(root/'index.html').read_text(encoding='utf-8'))

    def test_first_native_refusal_explains_a_later_bank_comparison_failure(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work);report=root/'import.json'
            report.write_text(json.dumps(dict(assets={'/Game/Bank':dict(valid=False,kind='AkAudioBank',
                error='asset-import-cook-differs-from-game',first_refusal='asset-import-native-data-not-brought-over-yet: /Script/AkAudio.AkAssetDataWithMedia')})),encoding='utf-8')
            gallery.record(dict(map='/Game/Map',variant='day'),{},root,report)
            row=json.loads((root/'Map_day.row.json').read_text(encoding='utf-8'))
            self.assertIn('AkAssetDataWithMedia',row['refusals'][0]['first_refusal'])
            self.assertIn('embedded Wwise bank/media data',(root/'index.html').read_text(encoding='utf-8'))

    def test_summary_counts_and_existing_game_comparison_link(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            audit = dict(component_audit=[{'actor': 'FixtureActor', 'class': 'Fixture<Bridge>', 'class_import': 'reference only'}],
                         component_audit_candidate_count=3, component_audit_truncated=True)
            gallery.record(dict(map='/Game/Partial',variant='day',imported=True,exit=0,uneditable_original_actors=2,
                                preserved_assets=17),audit,root)
            page = (root/'index.html').read_text(encoding='utf-8')
            self.assertIn('Construction and water component audit (3)', page)
            self.assertIn('Fixture&lt;Bridge&gt;', page)
            self.assertIn('candidate list is truncated', page)
            self.assertIn('17 existing assets reused in this project', page)
            self.assertIn('1 map rows; 0 verified four-image captures; 0 passed editor checks; 1 retain game-owned actors',
                          (root/'index.html').read_text(encoding='utf-8'))
            self.assertNotIn('href="game-vs-editor.html"',(root/'index.html').read_text(encoding='utf-8'))
            (root/'game-vs-editor.html').write_text('<p>Measured comparisons</p>',encoding='utf-8')
            gallery.render(root)
            self.assertIn('href="game-vs-editor.html"',(root/'index.html').read_text(encoding='utf-8'))

    def test_valid_projection_retains_omissions_and_is_labelled_partial(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            report=root/'import.json'
            report.write_text(json.dumps(dict(assets={'/Game/Layer':dict(valid=True,game_backed=True,
                game_backed_reason='Actor class refused <detail>',uneditable_original_actor_count=1,
                uneditable_original_actors=['Actor_2'],class_problems=['Missing class'])})),encoding='utf-8')
            gallery.record(dict(map='/Game/Partial',variant='day',uneditable_original_actors=1),{},root,report)
            row=json.loads((root/'Partial_day.row.json').read_text(encoding='utf-8'))
            self.assertEqual(row['projections'][0]['uneditable_original_actors'],['Actor_2'])
            self.assertEqual(row['projections'][0]['class_problems'],['Missing class'])
            self.assertIn('Actor class refused &lt;detail&gt;',(root/'index.html').read_text(encoding='utf-8'))

    def test_batch_errors_are_attributed_without_repeating_other_rows(self):
        error='/Game/A widgets: first failure\n/Game/B widgets: own failure\n/Game/C values: third failure'
        self.assertEqual(gallery.refusal_reason('/Game/B',error),'/Game/B widgets: own failure')
        self.assertEqual(gallery.refusal_reason('/Game/D',error),'Blueprint batch stopped: /Game/A widgets: first failure')
        self.assertEqual(gallery.refusal_reason('/Game/B','own error\nrequired detail'),'own error\nrequired detail')

    def test_missing_or_partial_captures_cannot_report_imported(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            native = dict(capture_verified=True, shader_probe_ready=True,
                          captures=[dict(image=view+'.jpg') for view in gallery.VIEWS])
            folder = root/'Area002_P_day'
            folder.mkdir()
            for name in gallery.VIEWS[:3]:
                (folder/(name+'.jpg')).write_bytes(b'x'*129)
            row = dict(map='/Game/Area002_P', variant='day', imported=True, exit=0)
            self.assertFalse(gallery.record(row,native,root))
            self.assertFalse(row['imported'])
            self.assertEqual(row['exit'],1)
            self.assertIn('not verified',(root/'index.html').read_text(encoding='utf-8'))
            (folder/'terrain-2.jpg').write_bytes(b'x'*129)
            row['imported']=True
            self.assertTrue(gallery.record(row,native,root))
            native['shader_probe_ready']=False
            self.assertFalse(gallery.record(row,native,root))

    def test_resume_requires_receipt_and_retained_pictures(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            row=dict(map='/Game/Area002_P',variant='day',capture_status='verified')
            folder=root/'Area002_P_day'; folder.mkdir()
            self.assertFalse(gallery.resume_verified(row,root))
            native=dict(map=row['map'],variant='day',capture_verified=True,shader_probe_ready=True,
                        captures=[dict(image=view+'.jpg') for view in gallery.VIEWS])
            (folder/'capture.json').write_text(json.dumps(native),encoding='utf-8')
            for name in gallery.VIEWS:
                (folder/(name+'.jpg')).write_bytes(b'x'*129)
            self.assertTrue(gallery.resume_verified(row,root))
            (folder/'overview.jpg').unlink()
            self.assertFalse(gallery.resume_verified(row,root))

    def test_refusals_and_html_escaping(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            report=root/'import.json'
            report.write_text(json.dumps(dict(assets={'/Game/Test':dict(valid=False,kind='Font',error='asset-import-properties-not-read <bad>')})),encoding='utf-8')
            gallery.record(dict(map='/Game/Empty',variant='day',error='<script>bad</script>'),{},root,report)
            page=(root/'index.html').read_text(encoding='utf-8')
            self.assertIn('asset-import-properties-not-read: 1 (Font 1)',page)
            self.assertIn('Some source properties are not yet decoded completely.',page)
            self.assertIn('&lt;script&gt;',page)
            self.assertNotIn('<script>',page)

    def test_widget_members_group_across_asset_and_member_names(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            report=root/'import.json'
            assets={name:dict(valid=False,kind='WidgetBlueprint',error='source widget member has no exact owned widget: '+name)
                    for name in ('Text_A','Image_B')}
            report.write_text(json.dumps(dict(assets=assets)),encoding='utf-8')
            gallery.record(dict(map='/Game/WidgetTest',variant='day'),{},root,report)
            result=json.loads((root/'WidgetTest_day.row.json').read_text(encoding='utf-8'))
            self.assertEqual(result['top_refusals'],[dict(reason='Widget source member has no owned widget',count=2,kinds={'WidgetBlueprint':2})])

    def test_all_refusals_and_import_time_survive_summary_grouping(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            assets={'/Game/Refused'+str(n):dict(valid=False,kind='Blueprint',error='Reason '+str(n)+' <source>') for n in range(8)}
            report=root/'import.json';report.write_text(json.dumps(dict(assets=assets)),encoding='utf-8')
            gallery.record(dict(map='/Game/Cave/Test',variant='day',seconds=777,import_seconds='total=650 blueprint_import=200',
                                editable_original_actors=90,uneditable_original_actors=3),{},root,report)
            result=json.loads((root/'Test_day.row.json').read_text(encoding='utf-8'))
            self.assertEqual(len(result['top_refusals']),6)
            self.assertEqual(len(result['refusals']),8)
            self.assertEqual(result['import_total_seconds'],650)
            page=(root/'index.html').read_text(encoding='utf-8')
            self.assertIn('Every refused asset (8)',page)
            self.assertIn('Reason 7 &lt;source&gt;',page)
            self.assertIn('Cave import exceeded ten minutes',page)
            self.assertIn('90 editable; 3 retained in the game',page)


if __name__ == '__main__':
    unittest.main()
