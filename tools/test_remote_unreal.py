"""Remote connection settings remain local and no connection uses an implicit host."""
import argparse
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
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


if __name__ == '__main__':
    unittest.main()
