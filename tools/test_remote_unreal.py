"""Remote connection settings remain local and no connection uses an implicit host."""
import argparse
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

import remote_unreal as remote


class RemoteConfiguration(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.start = {}
        for name in ('HOST', 'KEY', 'MIRROR', 'BASE_MIRROR', 'ENGINE', 'PYTHON', 'PAKS', 'SSH', 'ENVIRONMENT'):
            value = getattr(remote, name)
            self.start[name] = value.copy() if isinstance(value, (dict, list)) else value
        self.addCleanup(self.restore)

    def restore(self):
        for name, value in self.start.items():
            setattr(remote, name, value)

    def settings_file(self, **changes):
        values = {'host': 'build-host', 'root': 'R:/Build Workspace', 'ssh': 'ssh-client'}
        values.update(changes)
        path = self.root / 'settings.json'
        path.write_text(json.dumps(values), encoding='utf-8')
        return path

    def arguments(self, path, mirror=1):
        return argparse.Namespace(config=path, mirror=mirror, **{name: None for name in (
            'host', 'root', 'key', 'engine', 'python', 'ssh', 'paks')})

    def test_cli_overrides_environment_and_file_without_rewriting_the_file(self):
        path = self.settings_file(engine='R:/Engines/UE4.26', python=['py', '-3.12'])
        original = path.read_bytes()
        values = remote.remote_settings(path, {'P4_UNREAL_HOST': 'environment-host',
            'P4_UNREAL_ROOT': 'S:/Environment Mirror'}, {'host': 'cli-host', 'root': 'T:/CLI Mirror'})
        self.assertEqual((values['host'], values['root']), ('cli-host', 'T:/CLI Mirror'))
        self.assertEqual(values['engine'], 'R:/Engines/UE4.26')
        self.assertEqual(values['python'], ['py', '-3.12'])
        self.assertEqual(path.read_bytes(), original)

    def test_environment_can_configure_a_checkout_without_a_connection_file(self):
        with patch.object(remote, 'REPO', self.root):
            values = remote.remote_settings(environment={'P4_UNREAL_HOST': 'build-host',
                'P4_UNREAL_ROOT': 'R:/Build Workspace', 'P4_UNREAL_PYTHON': '["py", "-3.11"]'})
        self.assertEqual(values['python'], ['py', '-3.11'])
        self.assertEqual(values['paks'], 'private/game-paks')

    def test_invalid_or_missing_settings_fail_before_any_connection(self):
        for changes in ({'host': ''}, {'host': '-oProxyCommand=bad'}, {'host': 'two hosts'},
                        {'root': 'relative/mirror'}, {'root': 'R:/Mirror&command'},
                        {'engine': 'relative/engine'}, {'paks': '../outside'},
                        {'python': []}, {'python': ['py', 'bad\ncommand']}):
            with self.subTest(changes=changes):
                path = self.settings_file(**changes)
                with self.assertRaises(ValueError):
                    remote.remote_settings(path, environment={})
        with self.assertRaisesRegex(ValueError, 'missing'):
            remote.remote_settings(self.root / 'absent.json', environment={})
        with patch.object(remote, 'REPO', self.root), patch.dict(remote.os.environ, {}, clear=True), \
                patch.object(remote, 'sync') as sync, patch.object(remote.subprocess, 'run') as run, \
                redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                remote.main(['sync'])
        self.assertEqual(error.exception.code, 2)
        sync.assert_not_called()
        run.assert_not_called()

    def test_mirror_selection_is_repeatable_and_remote_arguments_keep_spaces(self):
        path = self.settings_file(engine='R:/Engines/UE4.26')
        with patch.dict(remote.os.environ, {}, clear=True):
            remote.configure_remote(self.arguments(path, 2))
        self.assertEqual(remote.MIRROR, 'R:/Build Workspace 2')
        self.assertEqual(remote.ENVIRONMENT['P4_UE_ROOT'], 'R:/Engines/UE4.26')
        self.assertEqual(remote.ENVIRONMENT['P4_NATIVE_ORIGINAL_CONTAINERS'],
                         'R:/Build Workspace 2/private/game-paks')
        command = remote.desktop('_poll', 'job', '0')
        self.assertEqual(command[0], 'ssh-client')
        self.assertIn('"R:/Build Workspace 2/tools/remote_unreal.py"', command[-1])
        remote.use_mirror(3)
        self.assertEqual(remote.MIRROR, 'R:/Build Workspace 3')
        remote.use_mirror(1)
        self.assertEqual(remote.MIRROR, 'R:/Build Workspace')

    def test_optional_key_is_passed_as_one_ssh_argument(self):
        path = self.settings_file(key=str(self.root / 'key with spaces'))
        with patch.dict(remote.os.environ, {}, clear=True):
            remote.configure_remote(self.arguments(path))
        index = remote.SSH.index('-i')
        self.assertEqual(remote.SSH[index + 1], str(self.root / 'key with spaces'))

    def test_a_new_connection_does_not_inherit_optional_paths_from_an_earlier_one(self):
        path = self.settings_file(engine='R:/Engines/UE4.26', paks='private/another-build')
        with patch.dict(remote.os.environ, {}, clear=True):
            remote.configure_remote(self.arguments(path))
            path = self.settings_file()
            remote.configure_remote(self.arguments(path))
        self.assertEqual(remote.PAKS, 'private/game-paks')
        self.assertNotIn('P4_UE_ROOT', remote.ENVIRONMENT)
        self.assertEqual(remote.ENVIRONMENT['P4_NATIVE_ORIGINAL_CONTAINERS'],
                         'R:/Build Workspace/private/game-paks')

    def test_help_and_desktop_worker_need_no_laptop_connection(self):
        with patch.object(remote, 'configure_remote') as configure, redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as help_result:
                remote.main(['--help'])
        self.assertEqual(help_result.exception.code, 0)
        configure.assert_not_called()
        with patch.object(remote, 'configure_remote') as configure, \
                patch.object(remote, 'on_desktop', return_value=7) as worker:
            self.assertEqual(remote.main(['_poll', 'job', '0']), 7)
        worker.assert_called_once_with('_poll', ['job', '0'])
        configure.assert_not_called()


class RunNames(unittest.TestCase):
    def test_every_action_that_takes_a_run_name_refuses_shell_marks_before_connecting(self):
        for argv in (['stop', 'x&calc'], ['wait', 'x|calc'], ['log', 'x>out'], ['stop', 'x^y'], ['wait', '-x'],
                     ['run', '--background', 'x<y', '--', 'py', 'tools/dev_build.py']):
            with self.subTest(argv=argv), patch.object(remote, 'configure_remote'), \
                    patch.object(remote.subprocess, 'run') as run, patch.object(remote.subprocess, 'Popen') as popen, \
                    patch.object(remote, 'sync') as sync, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    remote.main(argv[:1] + ['--'] + argv[1:] if argv[0] != 'run' else argv)
                self.assertEqual(str(error.exception), 'a run name is letters, digits, dot, dash and underscore')
                run.assert_not_called()
                popen.assert_not_called()
                sync.assert_not_called()

    def test_a_valid_name_reaches_the_desktop_unchanged(self):
        with patch.object(remote, 'configure_remote'), patch.object(remote, 'ask', return_value={'told': 'stopped'}) as ask, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(remote.main(['stop', 'build-2.tests_1']), 0)
        ask.assert_called_once_with('_stop', {}, 'build-2.tests_1')

    def test_the_desktop_side_refuses_a_name_an_older_laptop_did_not_check(self):
        with patch.object(remote.sys, 'stdin', types.SimpleNamespace(buffer=io.BytesIO(b'{}'))), \
                patch.object(remote.subprocess, 'run') as run:
            with self.assertRaises(SystemExit):
                remote.on_desktop('_stop', ['x&calc'])
        run.assert_not_called()


class RunIdentity(unittest.TestCase):
    """A run is alive, and stop ends it, only while its PID belongs to the process that started at the recorded time."""

    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        patcher = patch.object(remote, 'REPO', self.root)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.closed = []
        # The process table: PID 4242 now belongs to a process that started at 2000.
        opened = lambda pid, created=None: ('handle', pid) if pid == 4242 and created in (None, 2000) else None
        for name, value in (('opened', opened), ('close', self.closed.append)):
            patcher = patch.object(remote, name, side_effect=value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_state(self, **state):
        folder = self.root / remote.RUNS / 'build'
        remote.write(folder / 'job.json', dict(command=['py'], env={}, task=None))
        remote.write(folder / 'state.json', dict(runner=4242, pid=4343, started=1.0, **state))
        return folder

    def stop(self):
        output = io.BytesIO()
        with patch.object(remote.sys, 'stdin', types.SimpleNamespace(buffer=io.BytesIO(b'{}'))), \
                patch.object(remote.sys, 'stdout', types.SimpleNamespace(buffer=output)), \
                patch.object(remote.subprocess, 'run') as run:
            self.assertEqual(remote.on_desktop('_stop', ['build']), 0)
        return json.loads(output.getvalue().decode('utf-8'))['told'], run

    def test_a_reused_pid_is_neither_alive_nor_killed(self):
        folder = self.run_state(runner_created=1000)
        self.assertEqual(remote.state_of('build'), dict(name='build', done=None, alive=False))
        told, run = self.stop()
        self.assertEqual(told, 'build is not running')
        run.assert_not_called()
        self.assertFalse((folder / 'done.json').exists())

    def test_the_run_that_started_at_the_recorded_time_is_alive_and_stopped(self):
        folder = self.run_state(runner_created=2000)
        self.assertTrue(remote.state_of('build')['alive'])
        told, run = self.stop()
        self.assertEqual(told, 'stopped build')
        self.assertEqual(run.call_args_list[0].args[0], ['taskkill', '/PID', '4242', '/T', '/F'])
        self.assertEqual(self.closed[-1], ('handle', 4242))
        self.assertEqual(remote.read(folder / 'done.json')['error'], 'stopped')

    def test_a_state_written_by_the_older_script_is_judged_by_its_pid_as_before(self):
        self.run_state()
        self.assertTrue(remote.state_of('build')['alive'])
        told, run = self.stop()
        self.assertEqual(told, 'stopped build')
        remote.opened.assert_called_with(4242, None)

    def test_the_holder_records_its_creation_time_with_its_pid(self):
        folder = self.root / remote.RUNS / 'build'
        remote.write(folder / 'job.json', dict(command=['tool'], env={}, task=None))
        process = types.SimpleNamespace(pid=4343, wait=lambda: 0)
        with patch.object(remote, 'performance_cores', return_value=0), \
                patch.object(remote, 'own_creation_time', return_value=2000), \
                patch.object(remote.subprocess, 'Popen', return_value=process), \
                patch.object(remote.subprocess, 'CREATE_NO_WINDOW', 0, create=True):
            self.assertEqual(remote.run_job('build'), 0)
        state = remote.read(folder / 'state.json')
        self.assertEqual((state['runner'], state['runner_created'], state['pid']), (remote.os.getpid(), 2000, 4343))
        self.assertEqual(remote.read(folder / 'done.json')['exit'], 0)


class WindowsProcessTimes(unittest.TestCase):
    def test_creation_time_tells_this_process_from_a_later_one_with_its_pid(self):
        if remote.os.name != 'nt':      # the desktop side runs on Windows only
            return
        created = remote.own_creation_time()
        self.assertIsInstance(created, int)
        self.assertTrue(remote.alive(remote.os.getpid()))
        self.assertTrue(remote.alive(remote.os.getpid(), created))
        self.assertFalse(remote.alive(remote.os.getpid(), created + 1))


class LaptopWalk(unittest.TestCase):
    def test_folders_are_walked_where_stat_has_no_windows_attributes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'results/inner').mkdir(parents=True)
            (root / 'results/inner/one.txt').write_text('authored', encoding='utf-8')
            with patch.object(remote.os, 'lstat', return_value=types.SimpleNamespace(st_mode=0o40755)):
                found = remote.walk(root, ['results'])
        self.assertEqual([name for name, _, _ in found], ['results/inner/one.txt'])


if __name__ == '__main__':
    unittest.main()
