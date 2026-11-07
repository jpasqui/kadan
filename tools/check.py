"""Compile and run the portable core tests, then the tool tests. Neither needs Unreal.

    python tools/check.py [--core] [--tools]
    python tools/check.py --ci --report build/ci/checks.json

The core tests are C++17 programs in tests/ built with Clang (or g++) against core/. The tool tests are the
unittest modules tools/test_*.py. Without an option both run. The exit code is 0 when everything passed.
"""
import argparse
import importlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback
import unittest

ROOT = Path(__file__).resolve().parents[1]
CORE_TESTS = (
    'authoring_definitions_test.cpp', 'creator_lighting_recipe_test.cpp', 'creator_mesh_geometry_test.cpp',
    'cooked_name_sort_test.cpp', 'export_container_test.cpp', 'export_contract_test.cpp', 'export_package_test.cpp',
    'export_registry_test.cpp', 'game_builds_test.cpp', 'lighting_sun_test.cpp', 'lighting_registry_test.cpp',
    'material_source_complements_test.cpp', 'material_source_runtime_textures_test.cpp',
    'material_source_shadow_dispatch_test.cpp', 'material_source_shadow_factor_test.cpp',
    'material_source_shadow_span_test.cpp', 'mesh_collision_fidelity_test.cpp', 'mesh_nav_collision_test.cpp',
    'native_custom_backend_archive_test.cpp', 'navigation_boundary_test.cpp', 'new_map_census_test.cpp',
    'ampere_encode_test.cpp', 'own_sage_payload_test.cpp',
    'package_compare_test.cpp', 'package_index_test.cpp', 'physics_body_test.cpp', 'placement_params_test.cpp', 'quality_test.cpp',
    'static_mesh_facts_test.cpp', 'table_edit_export_test.cpp',
)
# Tests that link the compiler, staged as the plugin builds it (tools/native_custom_backend_sources.py).
STAGED_TESTS = ('sage_convert_test.cpp',)


def preflight(needs_tools=True):
    """Missing CI dependencies fail before a test can skip its work."""
    report = dict(problems=[], python=sys.version.split()[0], compilers={}, dependencies={})
    if sys.version_info < (3, 11):
        report['problems'].append('Python 3.11 or later is required')
    for name in ('clang', 'clang++'):
        compiler = shutil.which(name)
        if not compiler:
            report['problems'].append(name + ' is required on PATH')
            continue
        try:
            done = subprocess.run([compiler, '--version'], capture_output=True, text=True, timeout=15)
            if done.returncode:
                raise ValueError('version command exited ' + str(done.returncode))
            lines = (done.stdout + done.stderr).splitlines()
            report['compilers'][name] = lines[0] if lines else 'version not reported'
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            report['problems'].append(name + ': ' + str(error))
    if needs_tools:
        try:
            importlib.import_module('lupa')
            report['dependencies']['lupa'] = importlib.metadata.version('lupa')
        except (ImportError, OSError, importlib.metadata.PackageNotFoundError) as error:
            report['problems'].append('lupa is required for the preview controller checks: ' + str(error))
    return report


def unittest_report(result):
    """A passing unittest exit alone can hide skipped or undiscovered checks."""
    return dict(tests_run=result.testsRun,
                failures=[dict(test=test.id(), traceback=detail[-12000:]) for test, detail in result.failures],
                errors=[dict(test=test.id(), traceback=detail[-12000:]) for test, detail in result.errors],
                skipped=[dict(test=test.id(), reason=reason) for test, reason in result.skipped],
                expected_failures=[dict(test=test.id(), traceback=detail[-12000:])
                                   for test, detail in result.expectedFailures],
                unexpected_successes=[test.id() for test in result.unexpectedSuccesses])


def write_report(path, report):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n', encoding='utf-8')


def unittest_module(name, report_path):
    """Isolate one tool module while retaining exact test counts and skips."""
    sys.path.insert(0, str(ROOT))
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromName(name))
    report = dict(module=name, **unittest_report(result))
    report['status'] = 'passed' if result.wasSuccessful() and not result.skipped and result.testsRun else 'failed'
    write_report(report_path, report)
    return 0 if report['status'] == 'passed' else 1


def core_tests(records=None, strict=False):
    compiler = shutil.which('clang++') or (None if strict else shutil.which('g++'))
    if not compiler:
        print('Install a C++17 compiler (clang++ or g++)')
        return 1
    out = ROOT / 'build/tests'
    out.mkdir(parents=True, exist_ok=True)
    failures = []
    for name in CORE_TESTS:
        started = time.monotonic()
        record = dict(name=name, status='passed', tests_run=1)
        exe = out / (Path(name).stem + ('.exe' if os.name == 'nt' else ''))
        command = [compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-I', str(ROOT), '-I', str(ROOT / 'core'),
                   str(ROOT / 'tests' / name), '-o', str(exe)]
        try:
            subprocess.run(command, cwd=ROOT, check=True, timeout=600)
            subprocess.run([str(exe)], cwd=ROOT, check=True, timeout=600, stdout=subprocess.DEVNULL)
            print('PASS ' + name, flush=True)
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            failures.append(name)
            record.update(status='failed', error=str(error))
            print('FAIL {}: {}'.format(name, error), flush=True)
        record['duration_seconds'] = round(time.monotonic() - started, 3)
        if records is not None:
            records.append(record)
    sys.path.insert(0, str(ROOT / 'tools'))
    import native_custom_backend_sources
    staged = out / 'staged'
    sources = [str(path) for path in native_custom_backend_sources.stage_core(staged)]
    for name in STAGED_TESTS:
        started = time.monotonic()
        record = dict(name=name, status='passed', tests_run=1)
        exe = out / (Path(name).stem + ('.exe' if os.name == 'nt' else ''))
        command = [compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-I', str(ROOT), '-I', str(ROOT / 'core'),
                   '-I', str(staged), str(ROOT / 'tests' / name), *sources, '-o', str(exe)]
        try:
            subprocess.run(command, cwd=ROOT, check=True, timeout=1200, stderr=subprocess.DEVNULL)
            subprocess.run([str(exe)], cwd=ROOT, check=True, timeout=600, stdout=subprocess.DEVNULL)
            print('PASS ' + name, flush=True)
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            failures.append(name)
            record.update(status='failed', error=str(error))
            print('FAIL {}: {}'.format(name, error), flush=True)
        record['duration_seconds'] = round(time.monotonic() - started, 3)
        if records is not None:
            records.append(record)
    total = len(CORE_TESTS) + len(STAGED_TESTS)
    print('{}/{} core tests passed'.format(total - len(failures), total), flush=True)
    return 1 if failures else 0


def tool_modules():
    names = sorted(path.stem for path in (ROOT / 'tools').glob('test_*.py')
                   if 'import unittest' in path.read_text(encoding='utf-8'))
    return [*names, 'stubs.test_regenerate', 'own_shaders.test_verify_sources']


def tool_tests(records=None, strict=False):
    # The tools import each other by module name, and test_dev_tests imports tools.dev_tests.
    environment = dict(os.environ, PYTHONPATH=os.pathsep.join([str(ROOT), str(ROOT / 'tools')]))
    failures = []
    names = tool_modules()
    for name in names:
        started = time.monotonic()
        module_path = ROOT / 'build/ci/modules' / (name + '.json')
        command = [sys.executable, '-m', 'unittest', name]
        if strict:
            module_path.unlink(missing_ok=True)
            command = [sys.executable, str(Path(__file__).resolve()), '--_unittest-module', name,
                       '--_module-report', str(module_path)]
        try:
            result = subprocess.run(command, cwd=ROOT / 'tools', env=environment,
                                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=1800)
        except (OSError, subprocess.TimeoutExpired) as error:
            failures.append(name)
            if records is not None:
                records.append(dict(module=name, status='failed', runner_error=str(error), tests_run=0))
            print('FAIL {}: {}'.format(name, error), flush=True)
            continue
        record = dict(module=name, status='passed' if result.returncode == 0 else 'failed')
        if strict:
            try:
                parsed = json.loads(module_path.read_text(encoding='utf-8'))
                if not isinstance(parsed, dict):
                    raise ValueError('the module report must be a JSON object')
                record = parsed
                if (record.get('module') != name or type(record.get('tests_run')) is not int or
                        record['tests_run'] <= 0 or record.get('status') != 'passed' or
                        record.get('skipped') or record.get('failures') or record.get('errors') or
                        record.get('unexpected_successes') or result.returncode):
                    raise ValueError('the tool module failed, skipped checks, or produced an invalid result')
            except (OSError, ValueError) as error:
                record.update(status='failed', runner_error=str(error))
        record['duration_seconds'] = round(time.monotonic() - started, 3)
        if records is not None:
            records.append(record)
        summary = [line for line in result.stderr.splitlines() if line.startswith(('Ran ', 'OK', 'FAILED'))]
        print('{} {}: {}'.format('PASS' if record['status'] == 'passed' else 'FAIL', name, ', '.join(summary)), flush=True)
        if record['status'] != 'passed':
            failures.append(name)
            print(result.stderr[-3000:], flush=True)
    print('{}/{} tool test modules passed'.format(len(names) - len(failures), len(names)), flush=True)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--core', action='store_true', help='only the portable core tests')
    parser.add_argument('--tools', action='store_true', help='only the tool tests')
    parser.add_argument('--ci', action='store_true', help='require CI tools and reject every skipped test')
    parser.add_argument('--report', type=Path, help='CI result JSON (default build/ci/checks.json)')
    parser.add_argument('--_unittest-module', help=argparse.SUPPRESS)
    parser.add_argument('--_module-report', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args._unittest_module:
        if args._module_report is None:
            parser.error('--_unittest-module requires --_module-report')
        return unittest_module(args._unittest_module, args._module_report)
    if args.report and not args.ci:
        parser.error('--report requires --ci')
    both = not args.core and not args.tools
    run_core, run_tools = args.core or both, args.tools or both
    started = time.monotonic()
    report = dict(format=1, scope=[name for name, selected in (('core', run_core), ('tools', run_tools)) if selected],
                  complete_host_gate=bool(run_core and run_tools), core=[], tools=[], platform=sys.platform)
    if args.ci:
        report['preflight'] = preflight(needs_tools=run_tools)
        if report['preflight']['problems']:
            for problem in report['preflight']['problems']:
                print(problem, file=sys.stderr)
            report.update(status='preflight_failed', complete_host_gate=False, tests_run=0,
                          duration_seconds=round(time.monotonic() - started, 3))
            write_report(args.report or ROOT / 'build/ci/checks.json', report)
            return 2
    code = 0
    try:
        if run_core:
            code |= core_tests(report['core'], strict=args.ci)
        if run_tools:
            code |= tool_tests(report['tools'], strict=args.ci)
    except Exception:
        if not args.ci:
            raise
        report['runner_error'] = traceback.format_exc()[-12000:]
        print(report['runner_error'], file=sys.stderr)
        code = 1
    if args.ci:
        report.update(status='passed' if code == 0 else 'failed',
                      complete_host_gate=bool(run_core and run_tools and code == 0),
                      tests_run=sum(row.get('tests_run', 0) for row in report['core'] + report['tools']),
                      counts=dict(core_programs=len(report['core']),
                                  core_passed=sum(row['status'] == 'passed' for row in report['core']),
                                  tool_modules=len(report['tools']),
                                  tool_modules_passed=sum(row['status'] == 'passed' for row in report['tools']),
                                  tool_modules_failed=sum(row['status'] != 'passed' for row in report['tools']),
                                  skipped=sum(len(row.get('skipped', [])) for row in report['tools']),
                                  failures=sum(row['status'] != 'passed' for row in report['core']) +
                                           sum(len(row.get('failures', [])) for row in report['tools']),
                                  errors=sum(len(row.get('errors', [])) for row in report['tools'])),
                      duration_seconds=round(time.monotonic() - started, 3))
        write_report(args.report or ROOT / 'build/ci/checks.json', report)
    return code


if __name__ == '__main__':
    sys.exit(main())
