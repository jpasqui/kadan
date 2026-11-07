"""Boundary tests for the actual owned native test runner and stock controller deadline."""
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

from tools import dev_tests


class TestManagedLogNames(unittest.TestCase):
    def test_only_exact_current_and_restart_names_are_managed(self):
        for name in ('dev-tests.log', 'dev-tests-2.log', 'dev-tests-9.log', 'dev-tests-10.log', 'dev-tests-100.log'):
            with self.subTest(name=name):
                self.assertTrue(dev_tests.managed_log_name(name))
        for name in ('dev-tests.prior-' + 'f' * 64 + '.log', 'dev-tests-2.prior-' + 'a' * 64 + '.log',
                     'dev-tests-other.log', 'dev-tests-1.log', 'dev-tests-0.log', 'dev-tests-02.log',
                     'dev-tests.log.backup', 'dev-tests-2.log.extra', 'DEV-TESTS.log'):
            with self.subTest(name=name):
                self.assertFalse(dev_tests.managed_log_name(name))

    def test_actual_cleanup_preserves_prior_and_unmatched_log_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            removed = ('dev-tests.log', 'dev-tests-2.log', 'dev-tests-10.log')
            retained = ('dev-tests.prior-' + 'f' * 64 + '.log', 'dev-tests-2.prior-' + 'a' * 64 + '.log',
                        'dev-tests-other.log', 'dev-tests-01.log', 'another.log')
            for name in removed + retained:
                (root / name).write_bytes(('actual evidence: ' + name).encode('utf-8'))
            before = {name: (root / name).read_bytes() for name in retained}
            dev_tests.clear_managed_logs(root)
            self.assertTrue(all(not (root / name).exists() for name in removed))
            self.assertEqual({name: (root / name).read_bytes() for name in retained}, before)


class TestNativeControllerDeadline(unittest.TestCase):
    def run_runner(self, readback, extra=(), selected=1, completed=1):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            project = root / 'Owned.uproject'
            project.write_text('{}', encoding='utf-8')
            lock = types.SimpleNamespace(close=lambda: None)
            bake = types.SimpleNamespace(find_editor=lambda: root / 'UE4Editor.exe', _lock=lambda **kwargs: lock)
            build = types.SimpleNamespace(install=lambda *args, **kwargs: None)
            calls = []
            def run(command, **kwargs):
                calls.append((command, kwargs))
                log = project.parent / 'Saved/dev-tests.log'
                lines = ["Found {} Automation Tests, based on 'P4MapKit'".format(selected)]
                lines += ['Test Completed. Result={Passed} Name={RealFixture} Path={P4MapKit.RealFixture' + str(index) + '}' for index in range(completed)]
                if readback is not None:
                    lines += ['LogEngine: 0) AutomationControllerSettings /Script/AutomationController.Default__AutomationControllerSettings GameInstanceLostTimerSeconds = ' + str(readback)]
                # An echoed expected ini value is never independent configuration proof.
                lines += ['LogInit: Command Line: -ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:GameInstanceLostTimerSeconds=900']
                log.write_text('\n'.join(lines), encoding='utf-8')
                return types.SimpleNamespace(returncode=0)
            def popen(command, **kwargs):
                result = run(command, **kwargs)
                return types.SimpleNamespace(wait=lambda timeout=None: result.returncode, kill=lambda: None)
            overlay = types.SimpleNamespace(
                install=lambda archive, project: calls.append(('shipping-installed', {})) or {'exact': 'binding'},
                verify=lambda binding: calls.append(('shipping-verified', binding)))
            argv = ['dev_tests.py', '--project', str(project), '--built', str(root), '--timeout', '1800'] + list(extra)
            with patch.dict(sys.modules, {'ue_bake': bake, 'dev_build': build, 'native_release_test_runtime': overlay}), patch.object(dev_tests, 'fixtures', return_value={}), patch.object(dev_tests.subprocess, 'Popen', side_effect=popen), patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                result = dev_tests.main()
            return result, calls

    def test_real_runner_sets_bounded_stock_ini_and_requests_actual_readback(self):
        result, calls = self.run_runner('900.000000')
        self.assertEqual(result, 0)
        command, kwargs = calls[0]
        self.assertIn('-ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:GameInstanceLostTimerSeconds=900', command)
        self.assertTrue(any('GetAll AutomationControllerSettings GameInstanceLostTimerSeconds SHOWDEFAULTS' in arg for arg in command))
        self.assertIs(kwargs['stdin'], subprocess.DEVNULL)  # the run's own --timeout is enforced by the wait loop
        self.assertFalse(any('Disable' in arg for arg in command))

    def test_missing_actual_readback_fails_despite_echo_and_passed_tests(self):
        result, _ = self.run_runner(None)
        self.assertEqual(result, 1)

    def test_wrong_actual_readback_fails(self):
        result, _ = self.run_runner('300.000000')
        self.assertEqual(result, 1)

    def test_incomplete_suite_fails_even_with_verified_deadline(self):
        result, _ = self.run_runner('900.000000', selected=5, completed=1)
        self.assertEqual(result, 1)

    def test_requested_deadline_cannot_disable_or_escape_outer_bound(self):
        for value in ('0', '-1', '7201', '1801'):
            with self.subTest(value=value), self.assertRaises(SystemExit) as raised:
                self.run_runner('900', extra=('--controller-timeout', value))
            self.assertEqual(raised.exception.code, 2)

    def test_deadline_follows_the_slowest_test_an_editor_start_runs(self):
        times = {'P4MapKit.Export.Area001ImportAndPlacementOptions': 5280.0, 'P4MapKit.Export.PlacementsCave000': 1000.0,
                 'P4MapKit.Kadan.Fast': 12.0}
        self.assertEqual(dev_tests.controller_deadline(['P4MapKit.Kadan.Fast'], times, 7200), 900)
        self.assertEqual(dev_tests.controller_deadline(['P4MapKit.Kadan.Unknown'], times, 7200), 900)
        self.assertEqual(dev_tests.controller_deadline(['P4MapKit.Export.PlacementsCave000', 'P4MapKit.Kadan.Fast'], times, 7200), 1800)
        self.assertEqual(dev_tests.controller_deadline(['P4MapKit.Export.Pl'], times, 7200), 1800)
        self.assertEqual(dev_tests.controller_deadline(['P4MapKit.Export.Area001ImportAndPlacementOptions'], times, 14400), 7200)
        self.assertEqual(dev_tests.controller_deadline(['P4MapKit.Export.PlacementsCave000'], times, 1200), 1200)

    def test_conflicting_actual_cdo_values_fail(self):
        text = 'Default__AutomationControllerSettings GameInstanceLostTimerSeconds=900\nDefault__AutomationControllerSettings GameInstanceLostTimerSeconds=300'
        self.assertFalse(dev_tests.controller_deadline_readback(text, 900))

    def test_shipping_overlay_installed_before_editor_and_verified_after(self):
        result, calls = self.run_runner('900', extra=('--release-archive', 'Shipping.zip'))
        self.assertEqual(result, 0)
        self.assertEqual(calls[0], ('shipping-installed', {}))
        self.assertIsInstance(calls[1][0], list)
        self.assertEqual(calls[2], ('shipping-verified', {'exact': 'binding'}))

    def test_input_root_controls_native_fixture_environment(self):
        inputs = Path('Owned/private-inputs').resolve()
        result, calls = self.run_runner('900', extra=('--input-root', str(inputs)))
        self.assertEqual(result, 0)
        self.assertEqual(calls[0][1]['env']['P4_NATIVE_ROOT'], str(inputs / 'private/mapkit-native-workflow'))


class TestCommandLineLimit(unittest.TestCase):
    def test_groups_fit_keep_order_and_refuse_a_filter_too_long_alone(self):
        command_for = lambda filters: ['editor', '-ExecCmds=Automation RunTests ' + filters, '-AbsLog=x']
        filters = ['P4MapKit.Test{:04d}.'.format(index) + 'N' * 300 for index in range(120)]
        groups = dev_tests.command_groups(filters, command_for)
        self.assertGreater(len(groups), 1)
        self.assertEqual('+'.join(groups).split('+'), filters)
        self.assertTrue(all(dev_tests.command_line_fits(command_for(group)) for group in groups))
        with self.assertRaises(SystemExit):
            dev_tests.command_groups(['P4MapKit.' + 'N' * 17000], command_for)

    def test_a_long_only_list_runs_over_starts_that_each_keep_test_exit_and_abs_log(self):
        names = ['P4MapKit.Kadan.Long{:04d}'.format(index) + 'X' * 200 for index in range(150)]
        runner = TestNativeControllerDeadline()
        _, calls = runner.run_runner('900', extra=['--only'] + names, selected=len(names), completed=len(names))
        commands = [command for command, _ in calls if isinstance(command, list)]
        self.assertGreater(len(commands), 1)
        for command in commands:
            self.assertLess(len(subprocess.list2cmdline(command)), dev_tests.COMMAND_LINE_LIMIT)
            self.assertTrue(command[-2].startswith('-TestExit=') and command[-1].startswith('-AbsLog='))
        ran = [item for command in commands for arg in command if arg.startswith('-ExecCmds=Automation RunTests ')
               for item in arg.split(' ', 2)[2].split(',', 1)[0].split('+')]
        self.assertEqual(ran, names)


class TestNotRunCount(unittest.TestCase):
    def test_a_test_that_says_it_did_not_run_is_counted_apart(self):
        log = '\n'.join([
            'LogAutomationController: Display: Test Completed. Result={Passed} Name={A} Path={P4MapKit.Kadan.A}',
            'LogAutomationController: BeginEvents: P4MapKit.Kadan.A',
            'LogAutomationController: Warning: P4MapKit test not run. Set P4_X.',
            'LogAutomationController: EndEvents: P4MapKit.Kadan.A',
            'LogAutomationController: Display: Test Completed. Result={Passed} Name={B} Path={P4MapKit.Kadan.B}',
            'LogAutomationController: BeginEvents: P4MapKit.Kadan.B',
            'LogAutomationController: EndEvents: P4MapKit.Kadan.B',
            'LogTemp: Warning: P4MapKit test not run. outside any test'])
        self.assertEqual(dev_tests.not_run_tests(log), {'P4MapKit.Kadan.A'})


if __name__ == '__main__':
    unittest.main()
