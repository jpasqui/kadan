"""Run the authoring plugin's automation tests on a development build.

    py -3.11 tools/dev_build.py --host private/mapkit-native-workflow/dev-host-tests --tests
    py -3.11 tools/dev_tests.py [--tier fast|heavy|all] [--only <test name> ...]

The tests need a project that has the stub classes. The built plugin is copied from the build host
into that project (three files), the editor runs the tests there once, and what passed and failed is
printed. Every editor call has a time limit.

Without --only the fast tests run (test_tiers.py says which those are): the ones that neither import
a whole map nor run the whole export, a few minutes together. --tier heavy runs the others that can use
a copy of a kept imported project (kadan_fixture.py copy), --tier fresh the ones that test a map's
import itself and need a project without it. Each run records how long every test took, so a test that
becomes slow moves to the heavy tier by itself.

Unreal's automation controller gives up on the whole queue when one test runs longer than its
lost-instance deadline. Each editor start gets a deadline long enough for the slowest test it runs, by
the recorded times (at least 15 minutes, at most --timeout or two hours); --controller-timeout sets one
for every start instead. The editor runs with -d3d11, so over ssh run this with
`remote_unreal.py run --background NAME --session`.
"""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
WORK = REPO / 'private/mapkit-native-workflow'
CONTAINERS = Path(os.environ.get('APPDATA', '')) / 'eden/dump/0100B7C00933A000/romfs/Carrot4/Content/Paks'


def fixtures(input_root=None):
    """The game files two mesh tests read, by the paths the earlier census recorded."""
    root = Path(input_root).resolve() if input_root is not None else REPO
    census = json.loads((root / 'private/mapkit-native-workflow/texture-properties/next-import-audit/original-mesh-comparison.json').read_text(encoding='utf-8'))
    rows = {row['name']: row for row in census['records']}
    return {name: str((root / rows[row]['source_path']).resolve())
            for name, row in (('P4_NATIVE_CYLINDER_SOURCE', 'SM_stg_Cylinder01'), ('P4_NATIVE_SHIZAI_SOURCE', 'SM_stg_ShizaiBF01'))}


# The words a test reports when it did not run (its opt-in inputs or development seams are absent).
# Unreal records such a test as passed; it is counted as not run here.
NOT_RUN = 'P4MapKit test not run.'


def not_run_tests(text):
    """Full names of the tests whose reported events say they did not run."""
    found, current = set(), None
    for line in text.splitlines():
        if 'LogAutomationController: BeginEvents: ' in line:
            current = line.split('BeginEvents: ', 1)[1].strip()
        elif 'LogAutomationController: EndEvents: ' in line:
            current = None
        elif current and NOT_RUN in line:
            found.add(current)
    return found


def unfinished(text):
    """The short name of the test that was running when the log ends, or None when every started test ended."""
    running = None
    for line in text.splitlines():
        if 'Test Started. Name={' in line:
            running = line.split('Name={', 1)[1].split('}', 1)[0]
        elif 'Test Completed. Result={' in line:
            running = None
    return running


def selected_paths(text):
    """The full names of the tests the editor selected, which it lists before running them."""
    return [line.split('Display: \t', 1)[1].strip() for line in text.splitlines()
            if 'LogAutomationCommandLine: Display: \t' in line]


def completed_paths(text):
    return {line.split('Path={', 1)[1].split('}', 1)[0] for line in text.splitlines()
            if 'Test Completed. Result={' in line and 'Path={' in line}


def controller_deadline_readback(text, seconds):
    """Require the stock controller CDO value, not the echoed command line."""
    values = set()
    for line in text.splitlines():
        if 'Default__AutomationControllerSettings' not in line:
            continue
        match = re.search(r'GameInstanceLostTimerSeconds\s*=\s*([0-9]+(?:\.[0-9]+)?)', line)
        if match:
            values.add(float(match.group(1)))
    return values == {float(seconds)}


DEADLINE_FLOOR = 900
DEADLINE_CEILING = 7200
# Unreal keeps the first 16,384 characters of its command line and drops the rest without a word, which once
# cut off -TestExit and -AbsLog after a long filter list.
COMMAND_LINE_LIMIT = 16384


def command_line_fits(command):
    return len(subprocess.list2cmdline(command)) < COMMAND_LINE_LIMIT


def command_groups(filters, command_for):
    """The filters in groups, in order, each as '+'-joined text whose editor command (command_for) fits the
    command line. A filter that does not fit even alone stops the run."""
    groups, current = [], []
    for item in filters:
        if command_line_fits(command_for('+'.join(current + [item]))):
            current.append(item)
            continue
        if not command_line_fits(command_for(item)):
            raise SystemExit('The test filter {}... alone makes the editor command line longer than {} characters.'.format(
                item[:80], COMMAND_LINE_LIMIT))
        groups.append('+'.join(current))
        current = [item]
    if current:
        groups.append('+'.join(current))
    return groups


def controller_deadline(filters, times, timeout):
    """The controller's lost-instance deadline in seconds for an editor start that runs the tests the filters
    pick: the slowest one's recorded time and half again, plus five minutes, within the floor and ceiling."""
    if str(HERE) not in sys.path:
        sys.path.insert(0, str(HERE))
    import test_tiers
    picked = [seconds for name, seconds in times.items() if any(test_tiers.picks(item, name) for item in filters)]
    slowest = max(picked, default=0)
    return int(min(DEADLINE_CEILING, timeout, max(DEADLINE_FLOOR, slowest * 1.5 + 300)))


def managed_log_name(name):
    """Only names produced by editor_run, never archived or unrelated logs."""
    return re.fullmatch(r'dev-tests(?:-(?:[2-9]|[1-9][0-9]+))?\.log', name) is not None


def clear_managed_logs(folder):
    for earlier in folder.glob('dev-tests*.log'):
        if managed_log_name(earlier.name):
            earlier.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--built', type=Path, default=WORK / 'dev-host-tests')
    parser.add_argument('--release-archive', type=Path,
                        help='exercise these exact shipping DLLs with the separate private test module')
    parser.add_argument('--input-root', type=Path, default=REPO,
                        help='owned repository root containing private immutable acceptance inputs')
    parser.add_argument('--project', type=Path, default=WORK / 'native-coverage-clean-host/NativeCoverageClean.uproject')
    parser.add_argument('--only', nargs='*', default=[], help='full test names; the --tier tests when left out')
    parser.add_argument('--tier', choices=('fast', 'heavy', 'fresh', 'all'), default='fast',
                        help='without --only: the fast tests (default), the heavy ones, the fresh ones, or every test')
    parser.add_argument('--timeout', type=float, default=7200.0)
    parser.add_argument('--controller-timeout', type=int,
                        help='stock UE4 lost-instance deadline in seconds for every editor start; by default each '
                             'start gets one long enough for its slowest test by the recorded times')
    parser.add_argument('--no-pacing', action='store_true')
    parser.add_argument('--restarts', type=int, default=4,
                        help='with a tier: how many times the editor is started again after a test stops it')
    parser.add_argument('--map', help='open this saved /Game package before running the selected tests')
    parser.add_argument('--migrate-plugin-name', action='store_true', help='explicitly migrate only this test project to the Kadan plugin name')
    args = parser.parse_args()
    if args.map and (not re.fullmatch(r'/Game/[A-Za-z0-9_/-]+', args.map) or '..' in args.map or '//' in args.map):
        parser.error('--map must be a canonical /Game package path')
    if args.controller_timeout is not None and not 1 <= args.controller_timeout <= min(DEADLINE_CEILING, args.timeout):
        parser.error('--controller-timeout must be 1..7200 seconds and no longer than --timeout')
    project = args.project.resolve()
    sys.path.insert(0, str(HERE))
    import ue_bake
    from dev_build import install
    inputs = args.input_root.resolve()
    environment = dict(os.environ, P4_NATIVE_ROOT=str(inputs / 'private/mapkit-native-workflow'), **fixtures(inputs))
    environment.setdefault('P4_NATIVE_ORIGINAL_CONTAINERS', CONTAINERS.as_posix())
    if not args.no_pacing:
        environment.update(P4_NATIVE_AUTOMATION_PACING='60', P4_NATIVE_AUTOMATION_HOST=project.as_posix())
    log = project.parent / 'Saved/dev-tests.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    clear_managed_logs(log.parent)
    import test_tiers
    chosen = everything = None
    if args.only:
        selector = '+'.join(args.only)
    elif args.tier == 'all':
        selector = 'P4MapKit+P4Blueprints'
    else:
        fast, heavy, fresh = test_tiers.tiers(with_fresh=True)
        chosen, everything = dict(fast=fast, heavy=heavy, fresh=fresh)[args.tier], fast + heavy + fresh
        selector = '+'.join(test_tiers.selector(chosen, everything))
        print('{} tier: {} tests'.format(args.tier, len(chosen)))
    editor = str(ue_bake.find_editor())
    recorded = test_tiers.timings()
    deadlines = []

    def editor_command(filters, run_log, deadline):
        return [editor, str(project)] + ([args.map] if args.map else []) + ['-unattended', '-nop4', '-nosplash', '-UTF8Output', '-d3d11', '-NoHMD',
                '-ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:GameInstanceLostTimerSeconds=' + str(deadline),
                '-ExecCmds=Automation RunTests ' + filters + ',GetAll AutomationControllerSettings GameInstanceLostTimerSeconds SHOWDEFAULTS',
                '-TestExit=Automation Test Queue Empty', '-AbsLog=' + str(run_log)]

    def editor_run(filters, run_log):
        deadline = args.controller_timeout or controller_deadline(filters.split('+'), recorded, args.timeout)
        command = editor_command(filters, run_log, deadline)
        if not command_line_fits(command):
            raise SystemExit('The editor command line would be {} characters, over the {} Unreal keeps; its -TestExit and '
                             '-AbsLog would be lost.'.format(len(subprocess.list2cmdline(command)), COMMAND_LINE_LIMIT))
        deadlines.append(deadline)
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   env=environment)
        started = time.time()
        result = None
        while result is None:
            try:
                result = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                text = run_log.read_text(encoding='utf-8', errors='replace') if run_log.exists() else ''
                # An editor that found none of the tests (a build without the test module) otherwise waits out the
                # controller's deadline doing nothing.
                if re.search(r'Found 0 Automation Tests, based on', text) or time.time() - started > args.timeout:
                    process.kill()
                    process.wait()
                    result = -1
        return result, run_log.read_text(encoding='utf-8', errors='replace') if run_log.exists() else ''

    lock = ue_bake._lock(project=project)
    start = time.time()
    shipping_runtime = None
    texts, logs, stopped = [], [], []
    try:
        install(args.built.resolve(), project, migrate_plugin_name=args.migrate_plugin_name)
        if args.release_archive:
            import native_release_test_runtime
            shipping_runtime = native_release_test_runtime.install(args.release_archive, project)
        # One test that stops the editor used to end the whole suite ("400 selected, 36 completed"). With a
        # tier the test names are known, so the editor starts again with the tests that have not run.
        filters, remaining = selector, chosen
        # Without a tier (no restarts) a filter list too long for one command line runs over several starts.
        groups = [filters] if chosen is not None else command_groups(
            filters.split('+'), lambda part: editor_command(part, log.with_name('dev-tests-99.log'), DEADLINE_CEILING))
        first_round = len(groups)
        if first_round > 1:
            print('The filters are too long for one command line; they run over {} editor starts.'.format(first_round))
            code = 0
            for number, part in enumerate(groups, 1):
                run_log = log if number == 1 else log.with_name('dev-tests-{}.log'.format(number))
                part_code, run_text = editor_run(part, run_log)
                code = code or part_code
                texts.append(run_text)
                logs.append(run_log)
                if part_code == -1:
                    break
        for attempt in range(1 + (max(0, args.restarts) if chosen is not None else 0)) if first_round == 1 else ():
            run_log = log if attempt == 0 else log.with_name('dev-tests-{}.log'.format(attempt + 1))
            code, run_text = editor_run(filters, run_log)
            texts.append(run_text)
            logs.append(run_log)
            running = unfinished(run_text)
            if remaining is None or running is None or code == -1:
                break
            if attempt == 0:
                remaining = selected_paths(run_text) or remaining
                everything = sorted(set(everything) | set(remaining))
            done = completed_paths(run_text)
            culprit = [name for name in remaining if name.split('.')[-1] == running and name not in done]
            stopped += culprit or [running]
            remaining = [name for name in remaining if name not in done and name not in culprit]
            choice = test_tiers.selector(remaining, everything, avoid=stopped)
            left_out = [name for name in remaining if not any(test_tiers.picks(item, name) for item in choice)]
            remaining = [name for name in remaining if name not in left_out]
            print('{} stopped the editor ({}); starting it again for the {} tests left.'.format(running, run_log.name, len(remaining)))
            for name in left_out:
                print('  not run, its name starts the name of a test that stopped the editor:', name)
            if not remaining:
                break
            filters = '+'.join(choice)
    finally:
        if shipping_runtime is not None:
            try:
                native_release_test_runtime.verify(shipping_runtime)
            finally:
                lock.close()
        else:
            lock.close()
    text = '\n'.join(texts)
    times = test_tiers.durations([path for path in logs if path.exists()]) if text else {}
    if times:
        test_tiers.record(times, test_tiers.LOCAL_TIMINGS)
    # Each start's log has to read back the deadline that start was given.
    deadline_verified = bool(texts) and all(controller_deadline_readback(run_text, deadline) for run_text, deadline in zip(texts, deadlines))
    # The first start's count, or every start's when a filter list was split over several.
    selected = sum(int(count) for run_text in texts[:first_round]
                   for count in re.findall(r'Found (\d+) Automation Tests, based on', run_text))
    passed_paths = {line.split('Path={', 1)[1].split('}', 1)[0] for line in text.splitlines()
                    if 'Result={Passed}' in line and 'Path={' in line}
    failed_paths = {line.split('Path={', 1)[1].split('}', 1)[0] for line in text.splitlines()
                    if 'Result={Failed}' in line and 'Path={' in line}
    # After a restart a test can run twice; each test counts once, a failure in any run as a failure.
    passed_paths -= failed_paths
    passed, failed = len(passed_paths), len(failed_paths)
    skipped = sorted(not_run_tests(text) & passed_paths)
    ran = passed - len(skipped)
    names = [line.split('Name={', 1)[1].split('}', 1)[0] for line in text.splitlines() if 'Result={Failed}' in line and 'Name={' in line]
    errors = [line.split('Error: ', 1)[1] for line in text.splitlines()
              if 'LogAutomationController: Error:' in line or 'LogAutomationTest: Error:' in line]
    print('{} passed, {} failed, {} not run in {:.0f} s (exit {}{})'.format(ran, failed, len(skipped), time.time() - start, code,
                                                                         ', timed out' if code == -1 else ''))
    for name in names[:40]:
        print('  failed:', name)
    for name in stopped:
        print('  stopped the editor:', name)
    for name in skipped[:80]:
        print('  not run:', name)
    for line in errors[:40]:
        print('  ' + line[:300])
    if selected != passed + failed + len(stopped):
        print('Incomplete suite: {} tests selected, {} completed, {} stopped the editor.'.format(selected, passed + failed, len(stopped)))
    print('Stock controller deadline: {} s, actual CDO readback {}'.format(
        ', '.join(str(deadline) for deadline in deadlines), 'verified' if deadline_verified else 'missing or different'))
    slow = sorted(times.items(), key=lambda item: -item[1])[:5]
    if slow:
        print('slowest:', ', '.join('{} {:.0f} s'.format(name.split('.')[-1], seconds) for name, seconds in slow))
    print('log:', ', '.join(str(path) for path in logs) or log)
    if shipping_runtime is not None:
        print('Shipping production DLLs and the separate native test DLL remained unchanged.')
    return 0 if code == 0 and ran and not failed and not stopped and selected == passed and deadline_verified else 1


if __name__ == '__main__':
    sys.exit(main())
