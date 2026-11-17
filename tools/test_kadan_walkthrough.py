"""Host checks of kadan_walkthrough.py: its inputs, its editor starts, the page and the exit codes. No editor."""
import base64
import contextlib
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kadan_walkthrough as walk
import test_tiers

PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==')


def step(number, name, result='passed', **more):
    return dict(dict(number=number, id=name, title='Step ' + name, result=result, seconds=1.5 if result == 'passed' else 0,
                     picture='{:02d}-{}.png'.format(number, name) if result != 'not reached' else '', error='', notes=[],
                     log_errors=[]), **more)


def steps_document(state, rows, **more):
    return dict(dict(walkthrough='first-hour', state=state, kadan='0.3.1', map='/Game/Maps/Small_P', mod_id='first.hour',
                     steps=rows), **more)


class Arguments:
    game = retoc = repak = earlier_project = map = earlier_map = None


class InputsTest(unittest.TestCase):
    def test_an_option_wins_over_its_environment_variable_and_a_missing_input_is_empty(self):
        arguments = Arguments()
        arguments.game = 'D:/dump/romfs'
        values = walk.inputs(arguments, dict(P4_KADAN_WALK_GAME='E:/other', P4_KADAN_WALK_RETOC=' C:/tools/retoc.exe '))
        self.assertEqual(values['game'], 'D:/dump/romfs')
        self.assertEqual(values['retoc'], 'C:/tools/retoc.exe')
        self.assertEqual(values['repak'], '')
        self.assertEqual(values['earlier_project'], '')

    def test_each_part_names_the_input_it_lacks(self):
        values = dict(game='', retoc='', repak='', earlier_project='', map='')
        self.assertIn('--game', walk.missing_inputs(values, 'first-hour'))
        self.assertIn('--earlier-project', walk.missing_inputs(dict(values, game='G'), 'earlier-project'))
        self.assertEqual(walk.missing_inputs(dict(values, game='G'), 'first-hour'), '')
        self.assertEqual(walk.missing_inputs(dict(values, game='G', earlier_project='P'), 'earlier-project'), '')

    def test_without_the_game_folder_nothing_is_sent_and_the_run_says_not_run_and_fails(self):
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            code = walk.main([], {})
        self.assertEqual(code, 2)
        self.assertEqual(said.getvalue().count(walk.NOT_RUN), 2)
        self.assertIn('P4_KADAN_WALK_GAME', said.getvalue())

    def test_the_desktop_gets_the_same_inputs_and_parts(self):
        arguments = walk.argparse.Namespace(name='walk', built=walk.BUILT, stubs=walk.STUBS, timeout=600.0,
                                            parts=['first-hour'])
        words = walk.desktop_words(arguments, dict(game='private/game paks', retoc='r.exe', repak='', earlier_project='',
                                                   map='/Game/Maps/Small_P'))
        self.assertEqual(words[:3], ['py', 'tools/kadan_walkthrough.py', '--desktop'])
        self.assertEqual(words[words.index('--game') + 1], 'private/game paks')
        self.assertEqual(words[words.index('--map') + 1], '/Game/Maps/Small_P')
        self.assertNotIn('--repak', words)
        self.assertNotIn('--earlier-project', words)

    def test_the_run_goes_through_the_desktop_runner_in_the_logged_in_session(self):
        calls = []
        with tempfile.TemporaryDirectory() as folder:
            results = Path(folder) / 'private/desktop-results' / walk.WORK / 'walk/results'
            (results / 'first-hour').mkdir(parents=True)
            (results / 'first-hour/01-open.png').write_bytes(PNG)
            summary = dict(name='walk', inputs=dict(game='G'), recording=dict(recorded=False, reason='no ffmpeg'), parts=[
                dict(name='first-hour', title='First', test='T', state='passed', sessions=[dict(exit=0)],
                     steps=[step(1, 'open'), step(2, 'save', picture='', notes=['No picture: its window was minimized.'])])])
            (results / 'walkthrough.json').write_text(json.dumps(summary), encoding='utf-8')
            arguments = walk.argparse.Namespace(name='walk', built=walk.BUILT, stubs=walk.STUBS, timeout=600.0,
                                                parts=['first-hour'], mirror=2)
            repo, walk.REPO = walk.REPO, Path(folder)
            said = io.StringIO()
            try:
                with contextlib.redirect_stdout(said):
                    code = walk.laptop(arguments, dict(game='G', retoc='', repak='', earlier_project='', map=''),
                                       call=lambda words, mirror=None: calls.append((words, mirror)) or 0)
            finally:
                walk.REPO = repo
            self.assertEqual(code, 0)
            self.assertIn('02 save: passed without a picture - No picture: its window was minimized.', said.getvalue())
            self.assertEqual([call[0][0] for call in calls], ['run', 'wait', 'fetch'])
            self.assertEqual(calls[0][0][1:5], ['--background', 'walk', '--session', '--'])
            self.assertTrue(all(mirror == 2 for _, mirror in calls))
            self.assertIn('data:image/png;base64,', (results / 'walkthrough.html').read_text(encoding='utf-8'))


class ProjectsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.stubs = self.root / 'stubs/P4Stubs'
        (self.stubs / 'Binaries/Win64').mkdir(parents=True)
        (self.stubs / 'Intermediate').mkdir()
        (self.stubs / 'P4Stubs.uplugin').write_text('{}', encoding='utf-8')
        (self.stubs / 'Binaries/Win64/UE4Editor-P4Stubs.dll').write_bytes(b'dll')
        (self.stubs / 'Intermediate/made.obj').write_bytes(b'obj')

    def tearDown(self):
        walk.remove_tree(self.root)
        self.temporary.cleanup()

    def test_the_fresh_project_has_only_the_blank_project_and_the_stubs_plugin(self):
        project = walk.fresh_project(self.root / 'fresh', self.stubs)
        self.assertEqual(json.loads(project.read_text(encoding='utf-8')),
                         dict(FileVersion=3, EngineAssociation='4.26', Category='', Description=''))
        files = sorted(path.relative_to(project.parent).as_posix() for path in project.parent.rglob('*') if path.is_file())
        self.assertEqual(files, ['Config/DefaultEngine.ini', 'KadanFirstHour.uproject',
                                 'Plugins/P4Stubs/Binaries/Win64/UE4Editor-P4Stubs.dll', 'Plugins/P4Stubs/P4Stubs.uplugin'])
        # A second run starts from a new project, not from what the first left.
        (project.parent / 'Content/Left.umap').write_bytes(b'map')
        walk.fresh_project(self.root / 'fresh', self.stubs)
        self.assertFalse((project.parent / 'Content/Left.umap').exists())

    def test_a_missing_stubs_plugin_stops_the_run_before_any_editor_start(self):
        with self.assertRaisesRegex(ValueError, 'No built Nothing plugin'):
            walk.fresh_project(self.root / 'fresh', self.root / 'Nothing')

    def test_the_earlier_project_is_copied_with_its_settings_and_with_the_new_plugins(self):
        source = self.root / 'kept'
        for name in ('Content/Maps', 'Saved/Logs', 'Saved/Config', 'Saved/Kadan', 'Saved/KadanWalkthrough', 'Intermediate',
                     'Plugins/Kadan/Tools', 'Plugins/P4Stubs', 'Config'):
            (source / name).mkdir(parents=True)
        (source / 'Kept.uproject').write_text('{}', encoding='utf-8')
        game_map = source / 'Content/Maps/Small_P.umap'
        game_map.write_bytes(b'map')
        os.chmod(game_map, stat.S_IREAD)            # as Kadan marks the game's packages
        (source / 'Content/Maps/Saved').mkdir()     # a content folder that only shares a made folder's name
        (source / 'Content/Maps/Saved/Kept.uasset').write_bytes(b'asset')
        (source / 'Saved/Logs/old.log').write_text('old', encoding='utf-8')
        (source / 'Saved/Config/EditorPerProjectUserSettings.ini').write_text('[Kadan]', encoding='utf-8')
        (source / 'Saved/Kadan/kept.json').write_text('{}', encoding='utf-8')
        (source / 'Saved/KadanWalkthrough/steps.json').write_text('{}', encoding='utf-8')
        (source / 'Plugins/Kadan/Tools/retoc.exe').write_bytes(b'old tool')
        (source / 'Plugins/P4Stubs/old.txt').write_text('old', encoding='utf-8')
        before = sorted(path.relative_to(source).as_posix() for path in source.rglob('*'))
        project = walk.earlier_copy(source, self.root / 'earlier', self.stubs)
        self.assertEqual(project, self.root / 'earlier/Kept.uproject')
        copy = project.parent
        self.assertEqual((copy / 'Content/Maps/Small_P.umap').read_bytes(), b'map')
        self.assertTrue((copy / 'Content/Maps/Saved/Kept.uasset').is_file())
        self.assertTrue((copy / 'Saved/Config/EditorPerProjectUserSettings.ini').is_file())
        self.assertTrue((copy / 'Saved/Kadan/kept.json').is_file())
        self.assertFalse((copy / 'Saved/Logs').exists())
        self.assertFalse((copy / 'Saved/KadanWalkthrough').exists())
        self.assertFalse((copy / 'Intermediate').exists())
        self.assertFalse((copy / 'Plugins/Kadan').exists())
        self.assertFalse((copy / 'Plugins/P4Stubs/old.txt').exists())
        self.assertTrue((copy / 'Plugins/P4Stubs/P4Stubs.uplugin').is_file())
        self.assertEqual(sorted(path.relative_to(source).as_posix() for path in source.rglob('*')), before)
        # A copy an earlier run left, read-only files and all, is replaced.
        walk.earlier_copy(source, self.root / 'earlier', self.stubs)
        os.chmod(game_map, stat.S_IWRITE)

    def test_an_earlier_project_folder_without_one_project_file_is_refused(self):
        (self.root / 'empty').mkdir()
        with self.assertRaisesRegex(ValueError, 'Expected one .uproject'):
            walk.earlier_copy(self.root / 'empty', self.root / 'earlier', self.stubs)


class EditorTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.project = self.root / 'fresh/KadanFirstHour.uproject'
        self.project.parent.mkdir()
        self.project.write_text('{}', encoding='utf-8')
        self.saved = self.project.parent / 'Saved/KadanWalkthrough'
        self.values = dict(game='C:/dump/Paks', retoc='C:/tools/retoc.exe', repak='', earlier_project='',
                           map='/Game/Maps/Small_P', earlier_map='')

    def tearDown(self):
        self.temporary.cleanup()

    def session(self, state, rows, log='', code=0):
        def start(command, environment, saved, timeout, log_line, told):
            self.commands.append(command)
            self.environments.append(environment)
            Path(saved).mkdir(parents=True, exist_ok=True)
            if state:
                (Path(saved) / 'steps.json').write_text(json.dumps(steps_document(state, rows)), encoding='utf-8')
                for row in rows:
                    if row['picture']:
                        (Path(saved) / row['picture']).write_bytes(PNG)
            Path(command[-1].split('=', 1)[1]).write_text(log, encoding='utf-8')
            return code
        return start

    def run_part(self, sessions, name='first-hour'):
        self.commands, self.environments = [], []
        remaining = list(sessions)
        return walk.run_part(name, self.project, self.values, self.root / 'results', 60, 'UE4Editor-Cmd.exe',
                             start=lambda *arguments: remaining.pop(0)(*arguments), log=lambda line: None,
                             stop=lambda project: None)

    def test_the_editor_start_runs_the_one_test_and_keeps_its_exit_and_log_options(self):
        command = walk.editor_command('UE4Editor-Cmd.exe', self.project, 'P4MapKit.Kadan.WalkthroughFirstHour',
                                      self.root / 'a.log', 600)
        self.assertEqual(command[1], str(self.project))
        self.assertIn('-unattended', command)
        self.assertIn('-ExecCmds=Automation RunTests P4MapKit.Kadan.WalkthroughFirstHour,' + walk.THROTTLE_READBACK, command)
        self.assertIn('-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False',
                      command)
        self.assertIn('-TestExit=Automation Test Queue Empty', command)
        self.assertEqual(command[-1], '-AbsLog=' + str(self.root / 'a.log'))
        self.assertLess(len(subprocess.list2cmdline(command)), 16384)

    def test_the_test_gets_its_inputs_and_its_own_project_and_no_other_part_s(self):
        os.environ['P4_KADAN_WALK_REPAK'] = 'left over from the calling shell'
        try:
            row = self.run_part([self.session('passed', [step(1, 'open')])])
        finally:
            del os.environ['P4_KADAN_WALK_REPAK']
        environment = self.environments[0]
        self.assertEqual(environment['P4_KADAN_WALK_PROJECT'], str(self.project))
        self.assertEqual(Path(environment['P4_KADAN_WALK_GAME']), Path('C:/dump/Paks'))
        self.assertEqual(environment['P4_KADAN_WALK_MAP'], '/Game/Maps/Small_P')
        self.assertNotIn('P4_KADAN_WALK_REPAK', environment)
        self.assertNotIn('P4_KADAN_WALK_EARLIER_PROJECT', environment)
        self.assertEqual(row['state'], 'passed')
        self.assertTrue((self.root / 'results/first-hour/01-open.png').is_file())
        self.assertTrue((self.root / 'results/first-hour/steps.json').is_file())
        self.assertTrue((self.root / 'results/first-hour/walkthrough-first-hour-1.log').is_file())

    def test_the_earlier_project_s_own_map_is_the_map_its_test_reads(self):
        self.values.update(earlier_project='C:/kept', earlier_map='/Game/Maps/Kept_P')
        self.run_part([self.session('passed', [step(1, 'open')])], name='earlier-project')
        environment = self.environments[0]
        self.assertEqual(environment['P4_KADAN_WALK_MAP'], '/Game/Maps/Kept_P')
        self.assertEqual(environment['P4_KADAN_WALK_EARLIER_PROJECT'], str(self.project))
        self.assertNotIn('P4_KADAN_WALK_EARLIER_MAP', environment)
        self.run_part([self.session('passed', [step(1, 'open')])])
        self.assertEqual(self.environments[0]['P4_KADAN_WALK_MAP'], '/Game/Maps/Small_P')

    def test_a_restart_for_project_setup_goes_on_in_another_editor_start(self):
        row = self.run_part([self.session('restarting', [step(1, 'setup'), step(2, 'import', 'not reached')]),
                             self.session('passed', [step(1, 'setup'), step(2, 'import')])])
        self.assertEqual(len(self.commands), 2)
        self.assertNotEqual(self.commands[0][-1], self.commands[1][-1])
        self.assertEqual(row['state'], 'passed')
        self.assertEqual([session['exit'] for session in row['sessions']], [0, 0])

    def test_a_run_that_never_stops_restarting_ends_as_failed(self):
        restarting = self.session('restarting', [step(1, 'setup'), step(2, 'import', 'not reached')])
        row = self.run_part([restarting] * walk.SESSIONS)
        self.assertEqual(len(self.commands), walk.SESSIONS)
        self.assertEqual(row['state'], 'failed')

    def test_a_failed_step_fails_the_part_and_keeps_the_steps_after_it_as_not_reached(self):
        row = self.run_part([self.session('failed', [step(1, 'open'), step(2, 'tools', 'failed', error='No such file'),
                                                     step(3, 'import', 'not reached')])])
        self.assertEqual(row['state'], 'failed')
        self.assertEqual([item['result'] for item in row['steps']], ['passed', 'failed', 'not reached'])

    def test_an_editor_that_ends_during_a_step_is_reported_as_stopped_there(self):
        row = self.run_part([self.session('running', [step(1, 'open'), step(2, 'import', 'running')], code=3)])
        self.assertEqual(row['state'], 'failed')
        self.assertEqual(row['steps'][1]['result'], 'stopped')
        self.assertIn('ended during this step', row['steps'][1]['error'])
        late = self.run_part([self.session('running', [step(1, 'import', 'running')], code=-1)])
        self.assertIn('time limit', late['steps'][0]['error'])

    def test_a_test_that_says_it_did_not_run_is_not_run_with_its_reason(self):
        log = 'LogAutomationController: Warning: {} The walkthrough needs P4_KADAN_WALK_RETOC.\n'.format(walk.NOT_RUN)
        row = self.run_part([self.session(None, [], log=log)])
        self.assertEqual(row['state'], 'not run')
        self.assertEqual(row['reason'], 'The walkthrough needs P4_KADAN_WALK_RETOC.')
        self.assertEqual(walk.exit_code(dict(parts=[row])), 2)

    def test_an_editor_that_wrote_nothing_fails_the_part(self):
        row = self.run_part([self.session(None, [], log='LogInit: started\n', code=1)])
        self.assertEqual(row['state'], 'failed')
        self.assertIn('no steps.json', row['reason'])

    def test_what_an_earlier_run_left_in_the_project_is_not_taken_for_this_run_s(self):
        self.saved.mkdir(parents=True)
        (self.saved / 'steps.json').write_text(json.dumps(steps_document('passed', [step(1, 'open')])), encoding='utf-8')
        row = self.run_part([self.session(None, [], log='LogInit: started\n', code=1)])
        self.assertEqual(row['state'], 'failed')

    def test_steps_are_told_once_as_their_results_come(self):
        told = {}
        document = steps_document('running', [step(1, 'open'), step(2, 'import', 'running'), step(3, 'export', 'not reached')])
        self.assertEqual(walk.step_lines(document, told), ['  01 open: passed (2 s)', '  02 import: running'])
        self.assertEqual(walk.step_lines(document, told), [])
        document['steps'][1].update(result='failed', error='The import stopped.')
        self.assertEqual(walk.step_lines(document, told), ['  02 import: failed - The import stopped.'])

    def test_the_background_throttle_is_read_back_from_the_editor_s_own_settings(self):
        line = 'Cmd: GetAll EditorPerformanceSettings bThrottleCPUWhenNotForeground SHOWDEFAULTS\n'
        self.assertIsNone(walk.unthrottled(line))
        off = line + ('0) EditorPerformanceSettings /Script/UnrealEd.Default__EditorPerformanceSettings.'
                      'bThrottleCPUWhenNotForeground = False\n')
        self.assertTrue(walk.unthrottled(off))
        self.assertFalse(walk.unthrottled(off.replace('= False', '= True')))
        row = self.run_part([self.session('passed', [step(1, 'open')], log=off)])
        self.assertTrue(row['sessions'][0]['unthrottled'])

    def test_the_tested_plugin_files_are_named_by_their_bytes(self):
        binaries = self.project.parent / 'Plugins/Kadan/Binaries/Win64'
        binaries.mkdir(parents=True)
        (binaries / 'UE4Editor-Kadan.dll').write_bytes(b'kadan')
        (binaries / 'UE4Editor-Kadan.pdb').write_bytes(b'symbols')
        identity = walk.plugin_identity(self.project)
        self.assertEqual(list(identity), ['UE4Editor-Kadan.dll'])
        self.assertEqual(identity['UE4Editor-Kadan.dll'], walk.hashlib.sha256(b'kadan').hexdigest()[:16])
        (binaries / 'UE4Editor-Kadan.dll').write_bytes(b'another build')
        self.assertNotEqual(walk.plugin_identity(self.project), identity)

    def test_the_stray_editor_search_names_the_project_safely(self):
        script = walk.stray_editor_script(Path("C:/it's here/Fresh.uproject"))
        self.assertIn("-like '*C:", script)
        self.assertIn("it''s here", script)


class RecordingTest(unittest.TestCase):
    def test_without_ffmpeg_nothing_is_started_and_the_reason_is_kept(self):
        process, record = walk.start_recording('out.mp4', which=lambda name: None, popen=self.fail)
        self.assertIsNone(process)
        self.assertFalse(record['recorded'])
        self.assertIn('ffmpeg is not on the desktop\'s PATH', record['reason'])
        self.assertIn('Nothing was installed', record['reason'])
        self.assertEqual(walk.stop_recording(None, record, 'out.mp4'), record)

    def test_with_ffmpeg_the_desktop_is_recorded_into_the_results(self):
        started = []
        process, record = walk.start_recording(Path('results/walkthrough.mp4'), which=lambda name: 'C:/bin/ffmpeg.exe',
                                               popen=lambda command, **options: started.append(command) or 'process')
        self.assertEqual(process, 'process')
        self.assertEqual(record, dict(recorded=True, file='walkthrough.mp4', reason=''))
        self.assertEqual(started[0][0], 'C:/bin/ffmpeg.exe')
        self.assertIn('gdigrab', started[0])
        self.assertEqual(started[0][-1], str(Path('results/walkthrough.mp4')))

    def test_a_recorder_that_wrote_nothing_is_not_called_a_recording(self):
        class Ended:
            def communicate(self, data, timeout):
                return b'', b''
        with tempfile.TemporaryDirectory() as folder:
            record = walk.stop_recording(Ended(), dict(recorded=True, file='walkthrough.mp4', reason=''),
                                         Path(folder) / 'walkthrough.mp4')
        self.assertFalse(record['recorded'])


class PageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        (self.folder / 'first-hour').mkdir()
        for name in ('01-open.png', '02-tools.png'):
            (self.folder / 'first-hour' / name).write_bytes(PNG)
        self.summary = dict(
            name='walk', started='2026-11-17T18:00:00+00:00', finished='2026-11-17T18:30:00+00:00', built='dev-host',
            inputs=dict(game='C:/dump/Paks', retoc='C:/nope/retoc.exe', repak='', earlier_project='', map=''),
            recording=dict(recorded=False, reason='ffmpeg is not on the desktop\'s PATH, so the run was not recorded. '
                                                  'Nothing was installed.'),
            parts=[
                dict(name='first-hour', title='A new creator\'s first hour', test='P4MapKit.Kadan.WalkthroughFirstHour',
                     state='failed', kadan='0.3.1', sessions=[dict(exit=0, timed_out=False)], reason='', steps=[
                         step(1, 'open', notes=['Opened <Kadan> & more']),
                         step(2, 'tools', 'failed', error='The file dialog did not take C:\\nope\\retoc.exe.',
                              log_errors=['LogKadan: one', 'LogKadan: two']),
                         step(3, 'import', 'not reached')]),
                dict(name='earlier-project', title='An earlier project', test='P4MapKit.Kadan.WalkthroughEarlierProject',
                     state='not run', reason='The walkthrough needs a project an earlier release imported.', steps=[],
                     sessions=[])])

    def tearDown(self):
        self.temporary.cleanup()

    def test_every_step_is_on_the_page_in_order_with_its_result(self):
        text = walk.page(self.summary, self.folder)
        places = [text.index('Step open'), text.index('Step tools'), text.index('Step import')]
        self.assertEqual(places, sorted(places))
        self.assertIn('<section class="step passed">', text)
        self.assertIn('<section class="step failed">', text)
        self.assertIn('<section class="step not-reached">', text)
        self.assertIn('The file dialog did not take C:\\nope\\retoc.exe.', text)
        self.assertIn('2 errors in the Output Log during this step', text)
        self.assertIn('Kadan walkthrough <span class="badge failed">failed</span>', text)

    def test_the_page_holds_its_pictures_and_asks_for_nothing_else(self):
        text = walk.page(self.summary, self.folder)
        self.assertEqual(text.count('src="data:image/png;base64,' + base64.b64encode(PNG).decode('ascii')), 2)
        for outside in ('http://', 'https://', '<script', '<link', 'url('):
            self.assertNotIn(outside, text)

    def test_the_page_names_the_source_and_the_plugin_files_that_were_tested(self):
        self.summary['source'] = 'commit 0123abc with uncommitted changes'
        self.summary['parts'][0]['plugin'] = {'UE4Editor-Kadan.dll': '00aa11bb22cc33dd'}
        text = walk.page(self.summary, self.folder)
        self.assertIn('commit 0123abc with uncommitted changes', text)
        self.assertIn('UE4Editor-Kadan.dll 00aa11bb22cc33dd', text)

    def test_the_source_is_the_commit_and_whether_the_checkout_differs_from_it(self):
        class Done:
            def __init__(self, stdout, returncode=0):
                self.stdout, self.returncode = stdout, returncode

        def clean(command, **options):
            return Done('0123abc' if 'rev-parse' in command else '')

        def changed(command, **options):
            return Done('0123abc' if 'rev-parse' in command else ' M tools/kadan_walkthrough.py')

        self.assertEqual(walk.source_state(run=clean), 'commit 0123abc')
        self.assertEqual(walk.source_state(run=changed), 'commit 0123abc with uncommitted changes')
        self.assertEqual(walk.source_state(run=lambda command, **options: Done('', 128)), '')

    def test_text_from_the_run_cannot_become_markup(self):
        self.assertIn('Opened &lt;Kadan&gt; &amp; more', walk.page(self.summary, self.folder))

    def test_a_part_that_did_not_run_says_so_with_its_reason(self):
        text = walk.page(self.summary, self.folder)
        self.assertIn(walk.NOT_RUN + ' The walkthrough needs a project an earlier release imported.', text)
        self.assertIn('<span class="badge not-run">not run</span>', text)

    def test_without_a_recording_the_page_says_why(self):
        text = walk.page(self.summary, self.folder)
        self.assertIn('ffmpeg is not on the desktop&#x27;s PATH', text)
        self.assertNotIn('<video', text)

    def test_a_small_recording_is_inside_the_page_and_a_large_one_beside_it(self):
        self.summary['recording'] = dict(recorded=True, file='walkthrough.mp4', reason='')
        (self.folder / 'walkthrough.mp4').write_bytes(b'video')
        text = walk.page(self.summary, self.folder)
        self.assertIn('<video controls preload="metadata" src="data:video/mp4;base64,', text)
        limit, walk.EMBED_VIDEO = walk.EMBED_VIDEO, 2
        try:
            text = walk.page(self.summary, self.folder)
        finally:
            walk.EMBED_VIDEO = limit
        self.assertIn('<video controls preload="metadata" src="walkthrough.mp4">', text)
        self.assertIn('beside this page', text)

    def test_a_step_without_its_picture_says_so(self):
        (self.folder / 'first-hour/02-tools.png').unlink()
        self.assertEqual(walk.page(self.summary, self.folder).count('No picture was saved for this step.'), 1)

    def test_exit_codes_tell_failed_from_not_run_from_passed(self):
        passed, failed, absent = dict(state='passed'), dict(state='failed'), dict(state='not run')
        self.assertEqual(walk.exit_code(dict(parts=[passed, passed])), 0)
        self.assertEqual(walk.exit_code(dict(parts=[passed, absent])), 2)
        self.assertEqual(walk.exit_code(dict(parts=[failed, absent])), 1)
        self.assertEqual(walk.exit_code(dict(parts=[])), 1)

    def test_the_page_can_be_written_again_from_a_fetched_folder(self):
        (self.folder / 'walkthrough.json').write_text(json.dumps(self.summary), encoding='utf-8')
        with contextlib.redirect_stdout(io.StringIO()):
            code = walk.main(['--page', str(self.folder)], {})
        self.assertEqual(code, 1)
        self.assertIn('Step tools', (self.folder / 'walkthrough.html').read_text(encoding='utf-8'))


class TierTest(unittest.TestCase):
    def test_the_walkthrough_tests_are_declared_and_heavy(self):
        fast, heavy = test_tiers.tiers()
        for part in walk.PARTS.values():
            self.assertIn(part['test'], heavy)
            self.assertNotIn(part['test'], fast)

    def test_the_tests_read_the_variables_the_runner_sets(self):
        source = (walk.REPO / 'Kadan/Source/P4MapKitTests/Private/Authoring/P4KadanWalkthroughAutomation.cpp').read_text(
            encoding='utf-8')
        read = [variable for key, variable in walk.INPUTS.items() if key not in walk.RUNNER_ONLY]
        for variable in read + [part['variable'] for part in walk.PARTS.values()]:
            self.assertIn('"' + variable + '"', source.replace('TEXT("P4_KADAN_WALK_")) + FString(Tool).ToUpper()',
                                                               '"P4_KADAN_WALK_RETOC" "P4_KADAN_WALK_REPAK"'))
        self.assertIn(walk.NOT_RUN.rstrip('.'), 'P4MapKit test not run')
        self.assertIn('P4_MAPKIT_NOT_RUN_TEXT', source)


if __name__ == '__main__':
    unittest.main()
