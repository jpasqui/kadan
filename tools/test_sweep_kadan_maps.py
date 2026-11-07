"""Host checks of actual-container inventory coverage and honest crash/report classification."""
import csv
import json
import os
import stat
import sys
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

from sweep_kadan_maps import (aggregate_variant, automation_errors, cache_original_sources, failure_cause, frames, kept_closure,
                              content_before_invocation, created_in_invocation, map_inventory, owned_sweep_path,
                              run_bounded, variant_cases, write_rows)


class SweepTests(unittest.TestCase):
    def test_stopped_import_uses_elapsed_log_time_instead_of_the_planned_checkpoint(self):
        from sweep_kadan_maps import unfinished_elapsed
        log='[2026.10.25-09.00.00:000][325]LogAutomationController: Display: Test Started. Name={128_Area003_P}\n'
        log+='[2026.10.25-09.18.01:500][325]LogD3D11RHI: Error: Device allocation failed\n'
        self.assertEqual(unfinished_elapsed(log,'Area003_P'),1081.5)
        self.assertIsNone(unfinished_elapsed(log,'Area004_P'))

    def test_each_editor_launch_uses_the_guard_without_repeated_wait_notices(self):
        from sweep_kadan_maps import wait_editor_headroom,REPO
        with tempfile.TemporaryDirectory() as work:
            report=Path(work)/'maps.csv'
            def gate(callback):
                callback('waiting_for_memory',{'available_commit_bytes':1})
                callback('waiting_for_memory',{'available_commit_bytes':2})
                callback('memory_available',{'available_commit_bytes':8*1024**3})
            with mock.patch.object(sys,'path',[str(REPO),*sys.path]):
                with mock.patch('capture_memory_guard.wait_for_capture_memory',side_effect=gate) as guarded, mock.patch('builtins.print') as notices:
                    wait_editor_headroom(report)
            guarded.assert_called_once()
            self.assertEqual(notices.call_count,2)
            state=json.loads(report.with_suffix('.memory.json').read_text(encoding='utf-8'))
            self.assertEqual(state['event'],'memory_available')

    def test_editor_batches_bound_resident_assets_and_isolate_large_maps(self):
        from sweep_kadan_maps import editor_batches
        cases=[dict(map=str(i),category='cave') for i in range(7)]
        cases.insert(3,dict(map='surface',category='area'))
        cases.append(dict(map='ending',category='ending'))
        batches=list(editor_batches(cases,3))
        self.assertEqual([[r['map'] for r in batch] for batch in batches],
                         [['0','1','2'],['surface'],['3','4','5'],['6'],['ending']])
        self.assertEqual([id(r) for batch in batches for r in batch],[id(r) for r in cases])
        self.assertEqual(list(editor_batches(cases,0)),[cases])
        self.assertEqual(list(editor_batches([],32)),[])
        bounded = list(editor_batches([dict(category='cave') for _ in range(49)]))
        self.assertEqual([len(batch) for batch in bounded], [24, 24, 1])
        with self.assertRaises(ValueError):list(editor_batches(cases,-1))

    def test_actual_d3d11_allocation_exit_is_retained_without_a_fatal_stack(self):
        log='LogD3D11RHI: Error: Device->CreateBuffer failed\n at D3D11UniformBuffer.cpp:150\n with error E_OUTOFMEMORY\nLogWindows: FPlatformMisc::RequestExit(1)\n'
        self.assertTrue(any('E_OUTOFMEMORY' in line for line in frames(log)))
        self.assertEqual(frames(log.replace('RequestExit(1)','RequestExit(0)')),[])
        self.assertEqual(frames('A diagnostic mentions E_OUTOFMEMORY and RequestExit(1)'),[])
        # The error code on the fourth line of the D3D11 error is kept too.
        fourth='LogD3D11RHI: Error: Device->CreateBuffer failed\n at D3D11UniformBuffer.cpp:150\n while creating\n with error E_OUTOFMEMORY\nLogWindows: FPlatformMisc::RequestExit(1)\n'
        self.assertTrue(any('E_OUTOFMEMORY' in line for line in frames(fourth)))

    def test_session_forwards_reimport_and_clears_stale_single_map_mode(self):
        from sweep_kadan_maps import session_environment
        args=SimpleNamespace(containers=Path('paks'),retoc=Path('retoc.exe'),import_only=True,reimport=True)
        cases=[dict(map='/Game/One_P',variant='day'),dict(map='/Game/Two_P',variant='day')]
        with mock.patch.dict(os.environ,{'P4_NATIVE_SWEEP_MAP':'/Game/Stale','P4_NATIVE_SWEEP_REIMPORT':'0'}):
            env=session_environment(args,cases,Path('game'),Path('gallery'))
            self.assertNotIn('P4_NATIVE_SWEEP_MAP',env)
            self.assertEqual(env['P4_NATIVE_SWEEP_MAPS'],'/Game/One_P;/Game/Two_P')
            self.assertEqual(env['P4_NATIVE_SWEEP_KEEP'],'1')
            self.assertEqual(env['P4_NATIVE_SWEEP_REIMPORT'],'1')
            self.assertEqual(env['P4_NATIVE_SWEEP_IMPORT_ONLY'],'1')
            args.reimport=False;args.import_only=False
            resumed=session_environment(args,cases,Path('game'),Path('gallery'))
            self.assertNotIn('P4_NATIVE_SWEEP_REIMPORT',resumed)
            self.assertNotIn('P4_NATIVE_SWEEP_IMPORT_ONLY',resumed)

    def test_complete_root_inventory_and_area_variants(self):
        def line(path, kind='ExportBundleData'):
            return 'Carrot4-Switch 1234567800000002 ' + kind + ' ../../../Carrot4/Content/' + path
        text = '\n'.join([
            line('Carrot4/Maps/Main/Area/Area001/Area001_P.umap'),
            line('Carrot4/Maps/Main/Area/Area001/Area001_P.umap'),
            line('Carrot4/Maps/Main/Area/Area001/Sublevels/Area001_Objects_Night.umap'),
            line('Carrot4/Maps/Main/Area/Area001/Sublevels/Area001_Hero_Objects.umap'),
            line('Carrot4/Maps/Madori/Cave/Cave035/Cave035_F09/Cave035_F09_P.umap'),
            line('Carrot4/Maps/Madori/Ddb/DDB_AI006/DDB_AI006_P.umap'),
            line('Carrot4/Maps/Ending/Ending_Area500/Ending_Area500_P.umap'),
            line('Carrot4/Maps/Boot_P.umap'),
            line('Carrot4/Maps/Main/Area/Area001/Area001_P_BuiltData.uasset'),
            line('Carrot4/Maps/Main/Area/Area001/Area001_P.umap', 'BulkData'),
            'Engine 1234567800000002 ExportBundleData ../../../Engine/Content/Maps/Entry.umap'])
        rows = map_inventory(text)
        self.assertEqual(len(rows), 5)
        self.assertEqual({row['category'] for row in rows}, {'area', 'cave', 'battle', 'ending', 'system'})
        area = next(row for row in rows if row['category'] == 'area')
        self.assertTrue(area['night_layers'])
        self.assertTrue(area['hero_layers'])
        self.assertTrue(any(row['map'].endswith('Cave035_F09_P') for row in rows))

    def test_only_actual_fatal_calls_are_crash_frames(self):
        self.assertEqual(frames('Expected map to be imported\n[Callstack] 0x1234 diagnostic'), [])
        output = 'Assertion failed: MeshData\nFile renderer.cpp\n[Callstack] 0x1234 UE4Editor-RenderCore.dll\n'
        self.assertTrue(any('MeshData' in line for line in frames(output)))
        self.assertTrue(any('UE4Editor-RenderCore.dll' in line for line in frames(output)))
        self.assertLessEqual(len(frames(output * 30)), 16)

    def test_editor_assertion_cause_survives_helper_summary_truncation(self):
        detail = "Expected 'The imported original level matches' to be true. " + 'x' * 400
        text = ('[2026.10.13][123]LogAutomationController: Error: Test Completed. Result={Failed} Name={Map}\n' +
                '[2026.10.13][123]LogAutomationController: Error: ' + detail + '\n' +
                '[2026.10.13][123]LogAutomationTest: Error: E_MISSING_PARENT /Game/Actual/Parent\n')
        self.assertEqual(automation_errors(text), [detail, 'E_MISSING_PARENT /Game/Actual/Parent'])

    def test_every_inventory_row_remains_untested_until_actual_reply(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'maps.csv'
            write_rows(path, [dict(map='/Game/Carrot4/Maps/One_P', reached='inventory', game_seen=False),
                              dict(map='/Game/Carrot4/Maps/Two_P', reached='import_complete', imported=False,
                                   editor_exit=0, exit=1, error='Missing parent', game_seen=False)])
            with path.open(encoding='utf-8', newline='') as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]['edited_exported'], '')
            self.assertEqual(rows[1]['editor_exit'], '0')
            self.assertEqual(rows[1]['exit'], '1')
            self.assertTrue(all(row['game_seen'] == 'False' for row in rows))

    def test_two_maps_same_crash_stops_actual_runner_and_preserves_raw_evidence(self):
        rows = [dict(map='/Game/Carrot4/Maps/' + name, reached='inventory', game_seen=False)
                for name in ('One_P', 'Two_P', 'Three_P')]
        visited = []
        def run(row):
            visited.append(row['map'])
            number = len(visited)
            row.update(reached='importing', exit=1, edited_exported=False,
                       crash_frames='Assertion failed: MeshData | C:/Engine/Source/Renderer.cpp Line: ' + str(number * 10) +
                       ' | [Callstack] 0x' + str(number * 1111) + ' UE4Editor-Renderer.dll!BuildMesh() + ' + str(number * 20) + ' bytes',
                       error=row['map'] + ' failed')
        run_bounded(rows, run)
        self.assertEqual(len(visited), 2)
        self.assertEqual(failure_cause(rows[0]), failure_cause(rows[1]))
        self.assertNotEqual(rows[0]['crash_frames'], rows[1]['crash_frames'])
        self.assertEqual(rows[2]['reached'], 'inventory')
        self.assertNotIn('exit', rows[2])
        self.assertNotIn('edited_exported', rows[2])
        different = dict(rows[1], crash_frames=rows[1]['crash_frames'].replace('BuildMesh()', 'BuildTexture()'))
        self.assertNotEqual(failure_cause(rows[0]), failure_cause(different))

    def test_package_specific_paths_share_error_cause_but_codes_do_not(self):
        first = dict(map='/Game/Carrot4/Maps/One_P', error='One_P: E_MISSING_PARENT /Game/Characters/ParentA.ParentA')
        second = dict(map='/Game/Carrot4/Maps/Two_P', error='Two_P: E_MISSING_PARENT /Game/Other/ParentB.ParentB')
        self.assertEqual(failure_cause(first), failure_cause(second))
        self.assertNotEqual(failure_cause(first), failure_cause(dict(second, error='Two_P: E_BAD_SHADER /Game/Other/ParentB')))
        access = dict(first, crash_frames='Unhandled Exception: code: 0xc0000005 | [Callstack] 0x1234 UE4Editor.dll!BuildMesh()')
        illegal = dict(access, crash_frames=access['crash_frames'].replace('0xc0000005', '0xc000001d').replace('0x1234', '0x5678'))
        self.assertNotEqual(failure_cause(access), failure_cause(illegal))

    def test_day_success_does_not_claim_night_or_hero_activation_or_export(self):
        rows = [dict(map='/Game/Maps/Area_P', night_layers=True, hero_layers=True, reached='inventory', game_seen=False),
                dict(map='/Game/Maps/Cave_P', night_layers=False, hero_layers=False, reached='inventory', game_seen=False)]
        cases = variant_cases(rows)
        self.assertEqual([(case['map'], case['variant']) for case in cases],
                         [(rows[0]['map'], 'day'), (rows[0]['map'], 'night'), (rows[0]['map'], 'hero'), (rows[1]['map'], 'day')])
        day = cases[0]
        day.update(reached='editor_verified', exit=0, unchanged_exported=True, edited_exported=True, variant_activated=True)
        aggregate_variant(day)
        self.assertTrue(rows[0]['day_edited_exported'])
        self.assertFalse(rows[0]['edited_exported'])
        self.assertNotIn('night_edited_exported', rows[0])
        self.assertNotIn('hero_activation', rows[0])
        night = cases[1]
        night.update(reached='variant_activation', exit=1, unchanged_exported=True, edited_exported=False, variant_activated=False)
        aggregate_variant(night)
        self.assertTrue(rows[0]['day_edited_exported'])
        self.assertFalse(rows[0]['night_activation'])
        self.assertFalse(rows[0]['edited_exported'])
        self.assertEqual(rows[1]['reached'], 'inventory')
        self.assertTrue(all(not row['game_seen'] for row in rows))

    def test_requested_variant_uses_actual_inventory_and_keeps_one_row_per_root(self):
        rows = [dict(map='/Game/Maps/Area_P', night_layers=True, hero_layers=True),
                dict(map='/Game/Maps/Cave_P', night_layers=False, hero_layers=False)]
        cases = variant_cases(rows, {'Area_P'}, 'hero')
        self.assertEqual(len(cases), 1)
        self.assertEqual(cases[0]['variant'], 'hero')
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]['variants_requested'], 'hero')
        self.assertEqual(variant_cases(rows, {'Cave_P'}, 'night'), [])

    def test_noop_export_receipts_remain_per_variant_and_aggregate_without_claiming_artifacts(self):
        for outcomes, expected in ((('exported', 'verified_no_changes'), 'mixed'),
                                   (('verified_no_changes', 'exported'), 'mixed'),
                                   (('verified_no_changes', 'verified_no_changes'), 'verified_no_changes'),
                                   (('exported', 'exported'), 'exported'),
                                   (('failed', 'verified_no_changes'), 'failed')):
            for order in ((0, 1), (1, 0)):
                with self.subTest(outcomes=outcomes, order=order):
                    row = dict(map='/Game/Maps/Area_P', night_layers=True, hero_layers=False)
                    cases = variant_cases([row])
                    for index in order:
                        case, outcome = cases[index], outcomes[index]
                        case.update(unchanged_outcome=outcome, unchanged_exported=outcome == 'exported',
                                    unchanged_verified_noop=outcome == 'verified_no_changes')
                        aggregate_variant(case)
                    self.assertEqual(row['unchanged_outcome'], expected)
                    self.assertEqual(row['unchanged_exported'], expected == 'exported')
                    self.assertEqual(row['unchanged_verified_noop'], expected == 'verified_no_changes')
                    self.assertEqual(row['day_unchanged_outcome'], outcomes[0])
                    self.assertEqual(row['night_unchanged_outcome'], outcomes[1])
                    self.assertEqual(row['day_unchanged_verified_noop'], outcomes[0] == 'verified_no_changes')
                    self.assertEqual(row['night_unchanged_verified_noop'], outcomes[1] == 'verified_no_changes')
        row = dict(map='/Game/Maps/Area_P', night_layers=True, hero_layers=False)
        day = variant_cases([row])[0]
        day.update(unchanged_outcome='verified_no_changes', unchanged_exported=False, unchanged_verified_noop=True)
        aggregate_variant(day)
        self.assertEqual(row['unchanged_outcome'], 'pending')
        self.assertFalse(row['unchanged_verified_noop'])
        self.assertFalse(row['unchanged_exported'])

    def test_a_missing_asset_fails_a_verified_map(self):
        from sweep_kadan_maps import fail_missing_assets
        row = dict(reached='editor_verified', exit=0, missing_assets=1, error='')
        fail_missing_assets(row)
        self.assertEqual((row['reached'], row['exit']), ('assets_missing', 1))
        self.assertIn('1 assets did not import', row['error'])
        for row in (dict(reached='editor_verified', exit=0, missing_assets=0), dict(reached='captured', exit=1, missing_assets=3)):
            before = dict(row)
            fail_missing_assets(row)
            self.assertEqual(row, before)

    def test_a_black_landscape_fails_a_verified_map(self):
        from sweep_kadan_maps import fail_unpainted_landscapes, import_warnings
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'Cave018_F01_P_day.import.json'
            report.write_text(json.dumps({'warnings': [
                'A has a value this editor cannot hold (x).',
                '/Game/Maps/Cave018_F01_Landscape landscape color preview: Landscape_0.MIC_36: inline landscape parent source is unavailable']}),
                encoding='utf-8')
            row = dict(reached='import_verified', exit=0, error='')
            fail_unpainted_landscapes(row, import_warnings(report))
            self.assertEqual((row['reached'], row['exit']), ('landscape_unpainted', 1))
            self.assertIn('1 landscape materials draw black: Landscape_0.MIC_36: inline landscape parent', row['error'])
            self.assertEqual(import_warnings(Path(temporary) / 'absent.json'), [])
        for row, warnings in ((dict(reached='editor_verified', exit=0), ['A has a value this editor cannot hold (x).']),
                              (dict(reached='captured', exit=1), ['L landscape color preview: why'])):
            before = dict(row)
            fail_unpainted_landscapes(row, warnings)
            self.assertEqual(row, before)

    def test_kept_project_counts_the_whole_closure_and_retries_what_failed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / 'Kept.uproject'
            project.write_text('{}', encoding='utf-8')
            (root / 'Saved/Kadan').mkdir(parents=True)
            before = content_before_invocation(project)
            failed = root / 'Content/Carrot4/Bad/BP_Bad.uasset'
            failed.parent.mkdir(parents=True)
            failed.write_bytes(b'shell')
            ledger = {'/Game/Carrot4/Old/SM_Old': dict(kind='StaticMesh', reference_only=True, objects=0),
                      '/Game/Carrot4/Maps/Shared_Level': dict(kind='World', reference_only=False, objects=7)}
            report = dict(reference_only=['/Game/Carrot4/New/M_New'], assets={
                '/Game/Carrot4/Old/SM_Old': dict(valid=True, state='existing asset preserved'),
                '/Game/Carrot4/Maps/Shared_Level': dict(valid=True, state='existing asset preserved'),
                '/Game/Carrot4/Maps/Own_P': dict(valid=True, kind='World', objects=5, state='made'),
                '/Game/Carrot4/New/M_New': dict(valid=True, kind='Material', reference_only=True),
                '/Game/Carrot4/Bad/BP_Bad': dict(valid=False, kind='Blueprint', state='failed')})
            (root / 'Saved/Kadan/import-result.json').write_text(json.dumps(report), encoding='utf-8')
            row = {}
            kept_closure(project, row, ledger, before=before)
            self.assertEqual((row['assets'], row['new_assets'], row['preserved_assets']), (4, 2, 2))
            self.assertEqual((row['levels'], row['objects'], row['reference_only_assets']), (2, 12, 2))
            self.assertEqual(row['purged_leftovers'], 1)
            self.assertFalse(failed.exists())
            self.assertNotIn('/Game/Carrot4/Bad/BP_Bad', ledger)
            self.assertEqual(ledger['/Game/Carrot4/Maps/Own_P']['objects'], 5)

    def test_legacy_refusal_keeps_preexisting_package_and_sidecars_without_promotion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            project = root / 'Kept.uproject'
            report_file = root / 'Saved/Kadan/import-result.json'
            report_file.parent.mkdir(parents=True)
            legacy = '/Game/Carrot4/Player/ABP_Legacy'
            invalid_preserved = '/Game/Carrot4/Player/BP_InvalidPreserved'
            originals = {}
            for suffix in ('.uasset', '.umap', '.uexp', '.ubulk', '.uptnl'):
                path = root / ('Content/' + legacy[6:] + suffix)
                path.parent.mkdir(parents=True, exist_ok=True)
                originals[path] = b'legacy source' + suffix.encode('ascii')
                path.write_bytes(originals[path])
            other = root / ('Content/' + invalid_preserved[6:] + '.uasset')
            originals[other] = b'preexisting refused asset'
            other.write_bytes(originals[other])
            before = content_before_invocation(project)
            created = root / 'Content/Carrot4/New/BP_Bad.uasset'
            created.parent.mkdir(parents=True)
            created.write_bytes(b'new failed shell')
            ledger = {key: dict(kind='AnimBlueprint', reference_only=False) for key in (legacy, invalid_preserved)}
            report_file.write_text(json.dumps(dict(ok=False, problems=['legacy-animation-unmarked'], assets={
                legacy: dict(valid=False, kind='AnimBlueprint', state='original game reference'),
                invalid_preserved: dict(valid=False, kind='Blueprint', state='existing asset preserved'),
                '/Game/Carrot4/New/BP_Bad': dict(valid=False, kind='Blueprint', state='failed'),
                '/Game/Carrot4/New/DT_Good': dict(valid=True, kind='DataTable', saved=True)})), encoding='utf-8')
            row = dict(imported=False, unchanged_exported=False, edited_exported=False, error='legacy-animation-unmarked')
            kept_closure(project, row, ledger, before=before)
            self.assertEqual({path: path.read_bytes() for path in originals}, originals)
            self.assertFalse(created.exists())
            self.assertEqual((row['assets'], row['preserved_assets'], row['new_assets']), (1, 0, 1))
            self.assertEqual((row['failed_reply_assets'], row['retained_failed_files'], row['purged_leftovers']), (3, 6, 1))
            self.assertEqual(set(ledger), {'/Game/Carrot4/New/DT_Good'})
            self.assertFalse(row['imported'] or row['unchanged_exported'] or row['edited_exported'])
            self.assertEqual(row['error'], 'legacy-animation-unmarked')

    def test_failed_cleanup_without_preinvocation_custody_keeps_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            project = root / 'Kept.uproject'
            failed = root / 'Content/Carrot4/Bad.uasset'
            failed.parent.mkdir(parents=True)
            failed.write_bytes(b'cannot prove when created')
            report = root / 'import.json'
            report.write_text(json.dumps(dict(assets={'/Game/Carrot4/Bad': dict(valid=False)})), encoding='utf-8')
            row = {}
            kept_closure(project, row, {}, report)
            self.assertEqual(failed.read_bytes(), b'cannot prove when created')
            self.assertEqual((row['failed_reply_assets'], row['retained_failed_files'], row['purged_leftovers']), (1, 1, 0))

    def test_session_success_created_after_start_survives_later_refusal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            project = root / 'Kept.uproject'
            before = content_before_invocation(project)
            path = root / 'Content/Carrot4/Shared.uasset'
            path.parent.mkdir(parents=True)
            path.write_bytes(b'successful earlier map source')
            package = '/Game/Carrot4/Shared'
            report = root / 'import.json'
            ledger = {}
            report.write_text(json.dumps(dict(assets={package: dict(valid=True, kind='Blueprint')})), encoding='utf-8')
            kept_closure(project, {}, ledger, report, before=before)
            self.assertIn(package, ledger)
            retained = frozenset(ledger)
            report.write_text(json.dumps(dict(assets={package: dict(valid=False, state='original game reference')})), encoding='utf-8')
            for refusal in range(2):
                with self.subTest(refusal=refusal):
                    row = {}
                    kept_closure(project, row, ledger, report, before=before, retained_packages=retained)
                    self.assertEqual(path.read_bytes(), b'successful earlier map source')
                    self.assertEqual((row['failed_reply_assets'], row['retained_failed_files'], row['purged_leftovers']), (1, 1, 0))
                    self.assertNotIn(package, ledger)

    def test_cleanup_requires_containment_no_alias_and_complete_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            project = root / 'Kept.uproject'
            content = root / 'Content'
            content.mkdir()
            before = content_before_invocation(project)
            outside = root / 'outside.uasset'
            outside.write_bytes(b'foreign')
            self.assertFalse(created_in_invocation(outside, content, before))
            aliased = content / 'Alias'
            aliased.mkdir()
            candidate = aliased / 'new.uasset'
            candidate.write_bytes(b'aliased source')
            with mock.patch('sweep_kadan_maps.reparse_path', side_effect=lambda path: path == aliased):
                self.assertFalse(created_in_invocation(candidate, content, before))
            self.assertEqual(outside.read_bytes(), b'foreign')
            self.assertEqual(candidate.read_bytes(), b'aliased source')
            with mock.patch('sweep_kadan_maps.os.walk', side_effect=PermissionError('inventory inaccessible')):
                with self.assertRaises(PermissionError):
                    content_before_invocation(project)

    def test_new_hardlinked_failure_keeps_source_bytes_and_attributes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            project = root / 'Kept.uproject'
            source = root / 'source.uasset'
            source.write_bytes(b'retained hardlinked source')
            os.chmod(source, stat.S_IREAD)
            before = content_before_invocation(project)
            linked = root / 'Content/Carrot4/NewFailed.uasset'
            linked.parent.mkdir(parents=True)
            os.link(source, linked)
            source_before = source.stat()
            report = root / 'import.json'
            report.write_text(json.dumps(dict(assets={'/Game/Carrot4/NewFailed': dict(valid=False)})), encoding='utf-8')
            row = {}
            kept_closure(project, row, {}, report, before=before)
            source_after = source.stat()
            self.assertTrue(linked.exists())
            self.assertEqual(source.read_bytes(), b'retained hardlinked source')
            self.assertEqual(source_before.st_mode, source_after.st_mode)
            self.assertEqual(getattr(source_before, 'st_file_attributes', None), getattr(source_after, 'st_file_attributes', None))
            self.assertEqual((row['retained_failed_files'], row['purged_leftovers']), (1, 0))

    def test_extracted_originals_resolve_from_the_editor_folder_and_link_body_first(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            binaries = root / 'Engine/Binaries/Win64'
            binaries.mkdir(parents=True)
            project = root / 'Sweep/Sweep.uproject'
            extracted = root / 'Sweep/Saved/Kadan/OriginalPackages/Carrot4/Content/Carrot4/Obj/SM_A.uasset'
            extracted.parent.mkdir(parents=True)
            extracted.write_bytes(b'head')
            extracted.with_suffix('.uexp').write_bytes(b'body')
            relative = os.path.relpath(extracted, binaries)
            (root / 'Sweep/Saved/Kadan/import-result.json').write_text(json.dumps(
                dict(assets={'/Game/Carrot4/Obj/SM_A': dict(header=relative)})), encoding='utf-8')
            game = root / 'Sweep/game'
            game.mkdir()
            self.assertEqual(cache_original_sources(project, game, binaries), 2)
            self.assertEqual((game / 'Carrot4/Content/Carrot4/Obj/SM_A.uexp').read_bytes(), b'body')
            self.assertEqual(cache_original_sources(project, game, binaries), 0)

    def test_only_the_dedicated_short_root_can_be_mutable_outside_this_worktree(self):
        with tempfile.TemporaryDirectory() as temporary:
            main = Path(temporary).resolve()
            repository = main / 'private/worktrees/kadan-placements2'
            self.assertTrue(owned_sweep_path(repository / 'private/project/Host.uproject', repository))
            self.assertTrue(owned_sweep_path(main / 'private/kadan-pl2/SweepTests.uproject', repository))
            self.assertTrue(owned_sweep_path(main / 'private/kadan-pl2/game', repository))
            self.assertFalse(owned_sweep_path(main / 'private/another-lane/Content', repository))
            self.assertFalse(owned_sweep_path(main / 'private/kadan-pl2-other/Content', repository))
            self.assertFalse(owned_sweep_path(main / 'Content', repository))
            self.assertFalse(owned_sweep_path(main / 'private/kadan-pl2/../../Content', repository))


if __name__ == '__main__':
    unittest.main()
