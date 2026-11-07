"""Which of the plugin's automation tests are fast and which are heavy.

A heavy test imports a whole map, exports a mod through the whole export, or took over a minute the
last time it was timed. Everything else is fast: together the fast tests take a few minutes, so they
run after every change. The heavy ones run once per batch of changes, in a fixture copy
(kadan_fixture.py) where the map is already imported. A fresh test checks the import of a map itself
and refuses a project that has it already: those run before a release, in an empty project.

    py -3.11 tools/test_tiers.py                       print the tiers
    py -3.11 tools/test_tiers.py --timings LOG...      time each test in automation logs and
                                                                 record the times in test_timings.json

The test names come from the plugin's sources, so a new test lands in a tier without a list to edit.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
SOURCES = (REPO / 'Kadan/Source',)
TIMINGS = HERE / 'test_timings.json'
# What dev_tests.py measured on this machine since; it wins over the checked-in times.
LOCAL_TIMINGS = REPO / 'private/mapkit-native-workflow/test-timings.json'
HEAVY_SECONDS = 60
DECLARED = re.compile(r'IMPLEMENT_\w*AUTOMATION_TEST\w*\(\s*(\w+)\s*,\s*"([^"]+)"', re.S)
# What a test body calls when it imports a whole map or runs the whole export.
WHOLE_RUNS = ('P4KadanImportMap', 'P4ExportMod', 'P4AddedLightingBakeTest::Run')
# What a test says when it needs a project without the map it imports.
FRESH_ONLY = ('has no Cave000 before the test', 'has no earlier Cave000 import',
              'starts without this imported map')
STAMP = re.compile(r'^\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d):(\d{3})\]')


def declared_tests(fresh=None):
    """{full test name: whether its body imports a whole map or runs the whole export}. Fills `fresh` with
    the names of the tests that need a project without the map they import."""
    tests = {}
    for root in SOURCES:
        for path in sorted(root.rglob('*.cpp')) if root.is_dir() else []:
            text = path.read_text(encoding='utf-8', errors='replace')
            found = list(DECLARED.finditer(text))
            for match in found:
                cls, name = match.groups()
                start = text.find(cls + '::RunTest')
                if start < 0:
                    tests[name] = tests.get(name, False)
                    continue
                ends = [text.find(other.group(1) + '::RunTest') for other in found]
                end = min([e for e in ends if e > start] or [len(text)])
                body = text[start:end]
                tests[name] = tests.get(name, False) or any(word in body for word in WHOLE_RUNS) \
                    or 'EveryMap' in name or name.endswith('Bench')
                if fresh is not None and any(words in body for words in FRESH_ONLY):
                    fresh.add(name)
    return tests


def timings():
    times = {}
    for path in (TIMINGS, LOCAL_TIMINGS):
        if path.is_file():
            times.update(json.loads(path.read_text(encoding='utf-8')))
    return times


def tiers(with_fresh=False):
    """(fast names, heavy names), both sorted, or (fast, heavy, fresh) with `with_fresh`. A time recorded in
    test_timings.json wins over the source rule. Without `with_fresh` the fresh tests are among the heavy."""
    times = timings()
    fast, heavy, fresh_names = [], [], set()
    for name, whole in sorted(declared_tests(fresh_names).items()):
        seconds = times.get(name)
        is_heavy = name in fresh_names or (seconds > HEAVY_SECONDS if seconds is not None else whole)
        (heavy if is_heavy else fast).append(name)
    if not with_fresh:
        return fast, heavy
    return fast, [name for name in heavy if name not in fresh_names], sorted(fresh_names)


def picks(item, name):
    """Whether the `Automation RunTests` filter `item` picks the test `name`: Unreal 4.26 picks every test
    whose name contains the filter, in any case."""
    return item.lower() in name.lower()


def selector(names, everything, avoid=None):
    """Filters for Unreal's `Automation RunTests` that pick `names` and none of the other tests in
    `everything`: a group's name where every test under it is picked, elsewhere the shortest start of
    a name that no other test's name contains. Unreal's command line is limited to 16,384 characters, and
    filters made of group names and whole test names did not fit it (18,462 characters for the fast tier).

    A filter picks every test whose name contains it (picks), so a test whose name is inside another test's
    name cannot be picked alone. With `avoid` (after a crash), such a filter is used anyway, rerunning tests
    that already ran, unless it would pick a test in `avoid`: then it is left out."""
    picked = set(names)
    others = [name for name in everything if name not in picked]
    out = []

    def cover(prefix, members, shortest):
        if len(prefix) >= shortest and not any(picks(prefix, other) for other in others):
            # A group's name stays as it is; a start inside a name grows to what its tests share, which
            # contains the start, so it picks no more tests, and is easier to read in a log.
            if all(name == prefix or name.startswith(prefix + '.') for name in members):
                out.append(prefix)
            else:
                out.append(os.path.commonprefix(members))
            return
        groups = {}
        for name in members:
            if name == prefix:
                out.append(name)
                continue
            groups.setdefault(name[:len(prefix) + 1], []).append(name)
        for child, inner in sorted(groups.items()):
            cover(child, inner, shortest)

    # A filter is never shorter than the first part of a name, so it cannot pick the engine's tests.
    roots = {}
    for name in sorted(picked):
        roots.setdefault(name.split('.')[0], []).append(name)
    for root, members in sorted(roots.items()):
        cover(root, members, len(root))
    # A filter that would also pick a test of the other tier is refused rather than run the wrong tests.
    if avoid is not None:
        return [item for item in out if not any(picks(item, name) for name in avoid)]
    for item in out:
        wrong = [other for other in others if picks(item, other)]
        if wrong:
            raise ValueError('The filter {} also picks another tier\'s test {}'.format(item, wrong[0]))
    return out


def durations(logs):
    """{test path: seconds} from automation logs: from a test's start to its Test Completed line."""
    found = {}
    for name in logs:
        started = None
        for line in open(name, encoding='utf-8', errors='replace'):
            match = STAMP.match(line)
            if not match:
                continue
            moment = datetime.strptime(match.group(1), '%Y.%m.%d-%H.%M.%S').timestamp() + int(match.group(2)) / 1000
            if 'Test Started. Name={' in line:
                started = moment
            elif 'Test Completed. Result={' in line and 'Path={' in line and started is not None:
                found[line.split('Path={', 1)[1].split('}', 1)[0]] = round(moment - started, 1)
                started = None
    return found


def record(found, path=TIMINGS):
    times = json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}
    times.update(found)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(sorted(times.items())), indent=1) + '\n', encoding='utf-8')
    return times


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--timings', nargs='+', type=Path, help='automation logs to time and record')
    args = parser.parse_args()
    if args.timings:
        found = durations(args.timings)
        record(found)
        print('recorded {} test times in {}'.format(len(found), TIMINGS))
    fast, heavy, fresh = tiers(with_fresh=True)
    times = timings()
    print('{} fast tests, {} heavy tests, {} fresh tests'.format(len(fast), len(heavy), len(fresh)))
    print('fast filters:', ' + '.join(selector(fast, fast + heavy + fresh)))
    for tier, names in (('heavy', heavy), ('fresh', fresh)):
        for name in names:
            print('  {}: {}{}'.format(tier, name, ' ({:.0f} s)'.format(times[name]) if name in times else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
