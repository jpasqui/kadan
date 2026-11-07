"""The portable gate cannot hide skipped modules or claim partial work as full coverage."""
import contextlib
import io
import json
import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check as checks  # noqa: E402


class CheckRunner(unittest.TestCase):
    def test_default_keeps_both_host_groups_without_ci_preflight(self):
        with patch.object(checks, 'core_tests', return_value=0) as core, \
                patch.object(checks, 'tool_tests', return_value=0) as tools, \
                patch.object(checks, 'preflight') as preflight:
            self.assertEqual(checks.main([]), 0)
        core.assert_called_once_with([], strict=False)
        tools.assert_called_once_with([], strict=False)
        preflight.assert_not_called()

    def test_core_only_ci_report_does_not_claim_complete_host_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'checks.json'

            def core(records, strict=False):
                self.assertTrue(strict)
                records.append(dict(name='authored.cpp', status='passed', tests_run=1))
                return 0

            with patch.object(checks, 'core_tests', side_effect=core), \
                    patch.object(checks, 'tool_tests') as tools, \
                    patch.object(checks, 'preflight', return_value=dict(problems=[])) as preflight:
                code = checks.main(['--ci', '--core', '--report', str(output)])
            report = json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual((code, report['status'], report['tests_run']), (0, 'passed', 1))
            self.assertEqual(report['scope'], ['core'])
            self.assertFalse(report['complete_host_gate'])
            self.assertEqual(report['counts']['core_passed'], 1)
            preflight.assert_called_once_with(needs_tools=False)
            tools.assert_not_called()

    def test_missing_required_dependency_prevents_any_test_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'checks.json'
            with patch.object(checks, 'preflight', return_value=dict(problems=['missing lupa'])), \
                    patch.object(checks, 'core_tests') as core, patch.object(checks, 'tool_tests') as tools, \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(checks.main(['--ci', '--report', str(output)]), 2)
            report = json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(report['status'], 'preflight_failed')
            self.assertFalse(report['complete_host_gate'])
            core.assert_not_called()
            tools.assert_not_called()

    def test_tools_only_keeps_core_out_of_the_run_and_records_case_count(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'checks.json'

            def tools(records, strict=False):
                self.assertTrue(strict)
                records.append(dict(module='authored', status='passed', tests_run=3))
                return 0

            with patch.object(checks, 'core_tests') as core, \
                    patch.object(checks, 'tool_tests', side_effect=tools), \
                    patch.object(checks, 'preflight', return_value=dict(problems=[])) as preflight:
                self.assertEqual(checks.main(['--ci', '--tools', '--report', str(output)]), 0)
            report = json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(report['scope'], ['tools'])
            self.assertEqual(report['tests_run'], 3)
            self.assertEqual(report['counts']['tool_modules_passed'], 1)
            self.assertFalse(report['complete_host_gate'])
            core.assert_not_called()
            preflight.assert_called_once_with(needs_tools=True)

    def test_module_skip_fails_and_records_the_exact_reason(self):
        def skip():
            raise unittest.SkipTest('missing required Lua runtime')

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'module.json'
            with patch.object(unittest.defaultTestLoader, 'loadTestsFromName',
                              return_value=unittest.TestSuite([unittest.FunctionTestCase(skip)])), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(checks.unittest_module('authored', output), 1)
            report = json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(report['tests_run'], 1)
            self.assertEqual(report['skipped'][0]['reason'], 'missing required Lua runtime')
            self.assertEqual(report['status'], 'failed')

    def test_undiscovered_module_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'module.json'
            with patch.object(unittest.defaultTestLoader, 'loadTestsFromName', return_value=unittest.TestSuite()), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(checks.unittest_module('authored', output), 1)
            self.assertEqual(json.loads(output.read_text(encoding='utf-8'))['tests_run'], 0)

    def test_outer_gate_rejects_skips_even_when_a_module_claims_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / 'tools').mkdir()

            def process(command, **kwargs):
                output = Path(command[command.index('--_module-report') + 1])
                checks.write_report(output, dict(module='authored', status='passed', tests_run=1,
                                                 skipped=[dict(test='required.test', reason='missing tool')]))
                return subprocess.CompletedProcess(command, 0, stdout='', stderr='OK (skipped=1)\n')

            records = []
            with patch.object(checks, 'ROOT', root), patch.object(checks, 'tool_modules', return_value=['authored']), \
                    patch.object(checks.subprocess, 'run', side_effect=process), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(checks.tool_tests(records, strict=True), 1)
            self.assertEqual(records[0]['status'], 'failed')
            self.assertEqual(records[0]['skipped'][0]['reason'], 'missing tool')

    def test_preflight_requires_clang_and_lua_dependency(self):
        with patch.object(checks.shutil, 'which', return_value=None), \
                patch.object(checks.importlib, 'import_module', side_effect=ImportError('missing')):
            report = checks.preflight()
        problems = '\n'.join(report['problems'])
        for name in ('clang is required', 'clang++ is required', 'lupa is required'):
            self.assertIn(name, problems)

    def test_nested_stub_and_shader_verifiers_are_in_the_canonical_gate(self):
        self.assertIn('stubs.test_regenerate', checks.tool_modules())
        self.assertIn('own_shaders.test_verify_sources', checks.tool_modules())
