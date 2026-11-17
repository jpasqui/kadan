"""Run the first-hour walkthrough on the desktop and make one page of it.

    py -3.11 tools/kadan_walkthrough.py --game DIR --retoc FILE --repak FILE [--earlier-project FOLDER]
                                        [--map /Game/...] [--parts first-hour earlier-project]
                                        [--name NAME] [--mirror N] [--built HOST]

The walkthrough is two automation tests of the plugin (P4KadanWalkthroughAutomation.cpp) that drive the real
Kadan panel through what a new creator does and save a picture and a result after every step:

first-hour       in a new project that has only the Kadan and P4Stubs plugins: open the panel, give it the game
                 folder and the tools, set the project up (Kadan restarts Unreal Editor), import one map, open
                 it, Save Current Level, add a static mesh actor, make a mod, add the map, export, and read the
                 export's containers.
earlier-project  in a copy of a project that an earlier Kadan release imported the map into: the same from the
                 opened map on.

This script sends the checkout to the desktop (tools/remote_unreal.py), runs both parts there in the session of
the person logged in, brings the pictures and each part's steps.json back, and writes one page,
walkthrough.html, that shows every step in order with its result and picture. With ffmpeg on the desktop's PATH
the desktop's screen is recorded for the whole run and the page shows the video; without it the page says so.
Nothing is installed.

The inputs are folders and files on the desktop, absolute or relative to the mirror's root. Each comes from its
option or, without it, from the environment variable of the same name the tests read:

--game             P4_KADAN_WALK_GAME             the game dump's romfs or Paks folder
--retoc, --repak   P4_KADAN_WALK_RETOC, _REPAK    the tools, when Kadan's own Tools folder has neither
--earlier-project  P4_KADAN_WALK_EARLIER_PROJECT  the folder of a project the earlier release imported --map into
--map              P4_KADAN_WALK_MAP              the map's package (the tests have a small cave as the default)
--earlier-map      P4_KADAN_WALK_EARLIER_MAP      the earlier project's map, when it is not --map

A part whose input is missing is not run: the page and the output say "P4MapKit test not run." for it, and the
exit code is 2. A step that fails makes it 1. It is 0 only when every chosen part ran and passed.

These are editor tests. Nothing here runs the exported mod in the game.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
WORK = 'private/kadan-walkthrough'              # in the mirror: one folder per run
BUILT = 'private/mapkit-native-workflow/dev-host'
STUBS = 'private/mapkit-native-workflow/native-coverage-clean-host/Plugins/P4Stubs'
NOT_RUN = 'P4MapKit test not run.'
PARTS = {
    'first-hour': dict(test='P4MapKit.Kadan.WalkthroughFirstHour', variable='P4_KADAN_WALK_PROJECT',
                       title='A new creator\'s first hour, in a new project'),
    'earlier-project': dict(test='P4MapKit.Kadan.WalkthroughEarlierProject', variable='P4_KADAN_WALK_EARLIER_PROJECT',
                            title='Open, save and export in a project an earlier release imported'),
}
INPUTS = dict(game='P4_KADAN_WALK_GAME', retoc='P4_KADAN_WALK_RETOC', repak='P4_KADAN_WALK_REPAK',
              earlier_project='P4_KADAN_WALK_EARLIER_PROJECT', map='P4_KADAN_WALK_MAP',
              earlier_map='P4_KADAN_WALK_EARLIER_MAP')
# Inputs the runner reads itself. The earlier project's map is handed to its test as P4_KADAN_WALK_MAP.
RUNNER_ONLY = ('earlier_project', 'earlier_map')
# Of an earlier project, what Unreal makes again is not copied. Its Saved folder is: Kadan's settings and what it
# keeps of the game for the project's exports are there.
LEFT_OUT = ('Intermediate', 'DerivedDataCache')
LEFT_OUT_OF_SAVED = ('Logs', 'Crashes', 'Autosaves', 'Backup', 'webcache', 'KadanWalkthrough')
SESSIONS = 4                                    # editor starts per part: the first, and one after each restart
EMBED_VIDEO = 24 * 2**20                        # a recording up to this size goes into the page itself
BLANK_ENGINE_INI = '''[/Script/HardwareTargeting.HardwareTargetingSettings]
TargetedHardwareClass=Desktop
AppliedTargetedHardwareClass=Desktop
DefaultGraphicsPerformance=Maximum
AppliedDefaultGraphicsPerformance=Maximum
'''


def inputs(args, environment):
    """{input: value}, each from its option or else from its environment variable; '' when neither has it."""
    return {name: (getattr(args, name, None) or environment.get(variable, '') or '').strip()
            for name, variable in INPUTS.items()}


def missing_inputs(values, part):
    """What the part cannot run without, as the words that follow "P4MapKit test not run."."""
    lacking = []
    if not values['game']:
        lacking.append('the game folder (--game or P4_KADAN_WALK_GAME)')
    if part == 'earlier-project' and not values['earlier_project']:
        lacking.append('a project an earlier release imported (--earlier-project or P4_KADAN_WALK_EARLIER_PROJECT)')
    return 'The walkthrough needs {}.'.format(' and '.join(lacking)) if lacking else ''


def exit_code(summary):
    """1 when a part failed or stopped, 2 when the only trouble is a part that did not run, 0 when all passed."""
    states = [part['state'] for part in summary['parts']]
    if not states or any(state not in ('passed', 'not run') for state in states):
        return 1
    return 2 if 'not run' in states else 0


# ---- the desktop's side ----------------------------------------------------------------------------------------

def on_desktop(path):
    """An input's path on the desktop: as given when absolute, else under the mirror."""
    return Path(path) if Path(path).is_absolute() else REPO / path


def fresh_project(folder, stubs):
    """A project as the launcher's Blank template leaves it, with the P4Stubs plugin in its Plugins folder.
    The Kadan plugin is put beside it by the caller. A folder an earlier run made is replaced."""
    folder = Path(folder)
    remove_tree(folder)
    (folder / 'Config').mkdir(parents=True)
    (folder / 'Content').mkdir()
    project = folder / 'KadanFirstHour.uproject'
    project.write_text(json.dumps(dict(FileVersion=3, EngineAssociation='4.26', Category='', Description=''), indent='\t'),
                       encoding='utf-8')
    (folder / 'Config/DefaultEngine.ini').write_text(BLANK_ENGINE_INI, encoding='utf-8')
    copy_plugin(stubs, folder / 'Plugins/P4Stubs')
    return project


def earlier_copy(source, target, stubs):
    """A working copy of an earlier release's project, with that release's Kadan and P4Stubs folders taken out, as
    the update steps say, and its settings and Kadan's own files in Saved kept. The project it is copied from is
    left as it is."""
    source, target = Path(source), Path(target)
    found = sorted(source.glob('*.uproject')) if source.is_dir() else []
    if len(found) != 1:
        raise ValueError('Expected one .uproject in the earlier project folder ' + str(source))
    remove_tree(target)
    shutil.copytree(source, target, ignore=lambda folder, names: [
        name for name in names if (Path(folder) == source and name in LEFT_OUT) or
        (Path(folder) == source / 'Saved' and name in LEFT_OUT_OF_SAVED)])
    for name in ('Kadan', 'P4Stubs'):
        remove_tree(target / 'Plugins' / name)
    copy_plugin(stubs, target / 'Plugins/P4Stubs')
    return target / found[0].name


def copy_plugin(source, target):
    source = Path(source)
    if not (source / (source.name + '.uplugin')).is_file():
        raise ValueError('No built {} plugin at {}'.format(source.name, source))
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('Intermediate'))


def remove_tree(folder):
    """Removes a folder this script made. Kadan marks the game's packages read-only, so marks are cleared first."""
    def unlock(function, path, error):
        os.chmod(path, stat.S_IWRITE)
        function(path)
    if Path(folder).exists():
        shutil.rmtree(folder, **{'onexc' if sys.version_info >= (3, 12) else 'onerror': unlock})


def plugin_identity(project):
    """The plugin's built files as the project has them: each DLL's name and the start of its SHA-256."""
    folder = Path(project).parent / 'Plugins/Kadan/Binaries/Win64'
    return {path.name: hashlib.sha256(path.read_bytes()).hexdigest()[:16] for path in sorted(folder.glob('UE4Editor-*.dll'))}


# A person's editor is the window in front. Another editor on the desktop can take the front, and Unreal then runs
# this one at a fraction of its speed ("Use Less CPU when in Background"), so the walkthrough turns that off.
UNTHROTTLED = '-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False'
THROTTLE_READBACK = 'GetAll EditorPerformanceSettings bThrottleCPUWhenNotForeground SHOWDEFAULTS'


def editor_command(editor, project, test, log, deadline):
    """One editor start that runs one test and exits, as tools/dev_tests.py starts it."""
    return [str(editor), str(project), '-unattended', '-nop4', '-nosplash', '-UTF8Output', '-d3d11', '-NoHMD',
            '-ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:GameInstanceLostTimerSeconds='
            + str(int(deadline)), UNTHROTTLED,
            '-ExecCmds=Automation RunTests ' + test + ',' + THROTTLE_READBACK, '-TestExit=Automation Test Queue Empty',
            '-AbsLog=' + str(log)]


def unthrottled(text):
    """Whether the editor's log reads back the background throttle as off; None when it has no readback."""
    values = set(re.findall(r'Default__EditorPerformanceSettings.*?bThrottleCPUWhenNotForeground\s*=\s*(\w+)', text))
    return None if not values else values == {'False'}


def read_steps(folder):
    """A part's steps.json, or None while the editor has not written one."""
    try:
        return json.loads((Path(folder) / 'steps.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def not_run_reason(text):
    """Why the test said it did not run, from the editor's log; '' when it ran."""
    for line in text.splitlines():
        if NOT_RUN in line:
            return line.split(NOT_RUN, 1)[1].strip() or 'The test gave no reason.'
    return ''


def step_lines(steps, told):
    """The steps whose result changed since `told` ({number: result}), as lines for the run's output."""
    lines = []
    for step in (steps or {}).get('steps', []):
        if step['result'] not in ('not reached', told.get(step['number'])):
            told[step['number']] = step['result']
            lines.append('  {:02d} {}: {}{}'.format(step['number'], step['id'], step['result'],
                         ' ({:.0f} s)'.format(step['seconds']) if step['result'] == 'passed' else
                         ' - ' + step['error'] if step.get('error') else ''))
    return lines


def run_editor(command, environment, saved, timeout, log=print, told=None):
    """Starts the editor and waits for it, printing each step as the test records it. -1 when it ran too long."""
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                               env=environment)
    started, told = time.time(), {} if told is None else told
    while True:
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            code = None
        for line in step_lines(read_steps(saved), told):
            log(line)
        if code is not None:
            return code
        if time.time() - started > timeout:
            process.kill()
            process.wait()
            return -1


def stray_editor_script(project):
    """PowerShell that ends every editor process whose command line names the project."""
    named = "'*" + str(project).replace("'", "''") + "*'"
    return ("Get-CimInstance Win32_Process -Filter \"Name like 'UE4Editor%'\" | Where-Object { $_.CommandLine -like "
            + named + " } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")


def stop_stray_editors(project, run=subprocess.run):
    """Ends an editor that still has the project open: one the editor started for its own restart."""
    run(['powershell', '-NoProfile', '-Command', stray_editor_script(project)], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, check=False)


def run_part(name, project, values, results, timeout, editor, start=run_editor, log=print, stop=stop_stray_editors):
    """Runs one part's test in its project, over as many editor starts as its restarts need, and copies what it
    recorded into results/<name>. Returns the part's row of the summary."""
    part, project = PARTS[name], Path(project)
    saved = project.parent / 'Saved/KadanWalkthrough'
    remove_tree(saved)
    environment = dict(os.environ)
    for key, variable in INPUTS.items():
        environment.pop(variable, None)
        if values.get(key) and key not in RUNNER_ONLY:
            environment[variable] = str(on_desktop(values[key])) if key != 'map' else values[key]
    if name == 'earlier-project' and values.get('earlier_map'):
        environment[INPUTS['map']] = values['earlier_map']
    environment[part['variable']] = str(project)
    target = Path(results) / name
    remove_tree(target)
    target.mkdir(parents=True)
    row = dict(name=name, title=part['title'], test=part['test'], project=str(project), sessions=[], reason='')
    told = {}
    for session in range(1, SESSIONS + 1):
        session_log = project.parent / 'Saved' / 'walkthrough-{}-{}.log'.format(name, session)
        session_log.parent.mkdir(parents=True, exist_ok=True)
        session_log.unlink(missing_ok=True)
        log('{}: editor start {}'.format(name, session))
        code = start(editor_command(editor, project, part['test'], session_log, timeout), environment, saved, timeout,
                     log, told)
        stop(project)
        text = session_log.read_text(encoding='utf-8', errors='replace') if session_log.is_file() else ''
        if session_log.is_file():
            shutil.copyfile(session_log, target / session_log.name)
        row['sessions'].append(dict(log=session_log.name, exit=code, timed_out=code == -1, unthrottled=unthrottled(text)))
        row['reason'] = not_run_reason(text)
        steps = read_steps(saved)
        if row['reason'] or code == -1 or not steps or steps.get('state') != 'restarting':
            break
    steps = read_steps(saved)
    if saved.is_dir():
        for path in sorted(saved.iterdir()):
            if path.is_file() and path.suffix in ('.png', '.json'):
                shutil.copyfile(path, target / path.name)
    row.update(part_state(steps, row['reason'], row['sessions']))
    return row


def part_state(steps, reason, sessions):
    """A part's state and steps from what its editor sessions left. A step that was running when the editor
    ended is where the part stopped."""
    if reason:
        return dict(state='not run', steps=[], kadan='')
    if not steps:
        return dict(state='failed', steps=[], kadan='',
                    reason='The editor wrote no steps.json. Its log is beside the page\'s pictures.')
    rows = steps.get('steps', [])
    for step in rows:
        if step['result'] == 'running':
            step['result'] = 'stopped'
            step['error'] = step.get('error') or (
                'The editor ran past its time limit during this step.' if sessions and sessions[-1].get('timed_out')
                else 'The editor ended during this step.')
    passed = steps.get('state') == 'passed' and rows and all(step['result'] == 'passed' for step in rows)
    return dict(state='passed' if passed else 'failed', steps=rows, kadan=steps.get('kadan', ''),
                map=steps.get('map', ''), mod_id=steps.get('mod_id', ''))


def start_recording(target, which=shutil.which, popen=subprocess.Popen):
    """Records the desktop's screen with ffmpeg when it is on PATH. Returns (process or None, what to tell)."""
    ffmpeg = which('ffmpeg')
    if not ffmpeg:
        return None, dict(recorded=False, reason='ffmpeg is not on the desktop\'s PATH, so the run was not recorded. '
                                                 'Nothing was installed.')
    command = [ffmpeg, '-loglevel', 'error', '-y', '-f', 'gdigrab', '-framerate', '5', '-i', 'desktop',
               '-vf', 'scale=iw/2:-2', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '30', '-pix_fmt', 'yuv420p',
               str(target)]
    process = popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return process, dict(recorded=True, file=Path(target).name, reason='')


def stop_recording(process, record, target):
    if process is None:
        return record
    try:
        process.communicate(b'q', timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        process.kill()
    if not Path(target).is_file() or Path(target).stat().st_size == 0:
        return dict(recorded=False, reason='ffmpeg was started but wrote no recording.')
    return record


def desktop(args, values):
    """The run on the desktop: makes the projects, runs the parts, and leaves results/ for the laptop to fetch."""
    sys.path.insert(0, str(HERE))
    import ue_bake
    from dev_build import install
    work = REPO / WORK / args.name
    results = work / 'results'
    remove_tree(results)
    results.mkdir(parents=True)
    built, stubs = on_desktop(args.built), on_desktop(args.stubs)
    editor = ue_bake.find_editor()
    recorder, record = start_recording(results / 'walkthrough.mp4')
    summary = dict(name=args.name, started=datetime.now(timezone.utc).isoformat(timespec='seconds'), inputs=values,
                   built=str(built), parts=[], recording=record)
    try:
        for name in args.parts:
            reason = missing_inputs(values, name)
            if reason:
                print('{}: {} {}'.format(name, NOT_RUN, reason))
                summary['parts'].append(dict(name=name, title=PARTS[name]['title'], test=PARTS[name]['test'],
                                             state='not run', reason=reason, steps=[], sessions=[]))
                continue
            if name == 'first-hour':
                project = fresh_project(work / 'fresh', stubs)
            else:
                project = earlier_copy(on_desktop(values['earlier_project']), work / 'earlier', stubs)
            install(built, project)
            lock = ue_bake._lock(project=project)
            try:
                row = run_part(name, project, values, results, args.timeout, editor)
                row['plugin'] = plugin_identity(project)
            finally:
                lock.close()
            if row['state'] == 'not run':
                print('{}: {} {}'.format(name, NOT_RUN, row['reason']))
            print('{}: {}'.format(name, row['state']))
            summary['parts'].append(row)
    finally:
        summary['recording'] = stop_recording(recorder, record, results / 'walkthrough.mp4')
        summary['finished'] = datetime.now(timezone.utc).isoformat(timespec='seconds')
        (results / 'walkthrough.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')
    return exit_code(summary)


# ---- the page --------------------------------------------------------------------------------------------------

STYLE = '''
:root { color-scheme: light dark; --bg: #f6f5f1; --card: #ffffff; --ink: #1d1d1b; --soft: #62625c; --line: #dcdad2;
        --pass: #1f7a3d; --fail: #b3261e; --wait: #7a6a1f; }
@media (prefers-color-scheme: dark) { :root { --bg: #17181a; --card: #202225; --ink: #ecebe6; --soft: #a3a39b;
        --line: #33363a; --pass: #58c27d; --fail: #ff8a80; --wait: #d9c36a; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.5 "Segoe UI", system-ui, sans-serif; }
main { max-width: 1100px; margin: 0 auto; padding: 24px 16px 64px; }
h1 { font-size: 26px; margin: 0 0 4px; } h2 { font-size: 20px; margin: 40px 0 8px; }
p { margin: 6px 0; } .soft { color: var(--soft); } code { font: 13px Consolas, monospace; word-break: break-all; }
.badge { display: inline-block; padding: 1px 10px; border-radius: 999px; border: 1px solid currentColor;
         font-size: 13px; font-weight: 600; white-space: nowrap; }
.passed { color: var(--pass); } .failed, .stopped { color: var(--fail); } .not-reached, .not-run { color: var(--wait); }
.step { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 14px 16px; margin: 14px 0; }
.step.failed, .step.stopped { border-color: var(--fail); border-width: 2px; }
.step h3 { margin: 0 0 6px; font-size: 16px; display: flex; gap: 10px; align-items: baseline; flex-wrap: wrap; }
.step h3 .n { color: var(--soft); font-variant-numeric: tabular-nums; } .step h3 .t { flex: 1 1 300px; }
.step ul { margin: 6px 0; padding-left: 20px; } .step img { max-width: 100%; height: auto; border: 1px solid var(--line);
         border-radius: 6px; margin-top: 8px; display: block; }
.error { color: var(--fail); font-weight: 600; white-space: pre-wrap; } details { margin-top: 6px; }
video { max-width: 100%; border: 1px solid var(--line); border-radius: 6px; }
table { border-collapse: collapse; } td { padding: 2px 16px 2px 0; vertical-align: top; }
'''


def data_uri(path, kind):
    return 'data:{};base64,{}'.format(kind, base64.b64encode(Path(path).read_bytes()).decode('ascii'))


def page(summary, folder):
    """The whole run as one HTML page: every part's steps in order, each with its result and its picture inside
    the page. `folder` holds what the desktop's run left (results/)."""
    folder, text = Path(folder), html.escape
    code = exit_code(summary)
    verdict = {0: 'passed', 1: 'failed', 2: 'not run'}[code]
    out = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
           '<meta name="viewport" content="width=device-width, initial-scale=1">',
           '<title>Kadan walkthrough</title><style>', STYLE, '</style></head><body><main>',
           '<h1>Kadan walkthrough <span class="badge {}">{}</span></h1>'.format(verdict.replace(' ', '-'), verdict),
           '<p class="soft">An editor test on the desktop, driven on the real Kadan panel. Nothing here was run in '
           'the game.</p><table>']
    kadan = next((part.get('kadan') for part in summary['parts'] if part.get('kadan')), '')
    for label, value in (('Run', summary.get('name', '')), ('Started', summary.get('started', '')),
                         ('Finished', summary.get('finished', '')), ('Kadan.uplugin VersionName', kadan),
                         ('Source', summary.get('source', '')), ('Plugin build', summary.get('built', '')),
                         *((INPUTS[key], value) for key, value in summary.get('inputs', {}).items() if value)):
        if value:
            out.append('<tr><td class="soft">{}</td><td><code>{}</code></td></tr>'.format(text(label), text(str(value))))
    out.append('</table>')
    for part in summary['parts']:
        state = part['state']
        out.append('<h2>{} <span class="badge {}">{}</span></h2>'.format(text(part['title']), state.replace(' ', '-'), state))
        out.append('<p class="soft"><code>{}</code>{}</p>'.format(text(part['test']), ''.join(
            ' · editor start {} ended with exit code {}{}{}'.format(
                number, session['exit'], ' (stopped at its time limit)' if session.get('timed_out') else '',
                {True: '', False: ', with the background throttle on', None: ''}[session.get('unthrottled')])
            for number, session in enumerate(part.get('sessions', []), 1))))
        if part.get('plugin'):
            out.append('<p class="soft">Tested plugin files: {}</p>'.format(', '.join(
                '<code>{} {}</code>'.format(text(name), text(digest)) for name, digest in sorted(part['plugin'].items()))))
        if state == 'not run':
            out.append('<p class="error">{} {}</p>'.format(NOT_RUN, text(part.get('reason', ''))))
        elif part.get('reason'):
            out.append('<p class="error">{}</p>'.format(text(part['reason'])))
        for step in part.get('steps', []):
            result = step['result']
            out.append('<section class="step {0}"><h3><span class="n">{1:02d}</span><span class="t">{2}</span>'
                       '<span class="badge {0}">{3}</span></h3>'.format(
                           result.replace(' ', '-'), step['number'], text(step['title']),
                           result + (' · {:.0f} s'.format(step['seconds']) if step.get('seconds') else '')))
            if step.get('error'):
                out.append('<p class="error">{}</p>'.format(text(step['error'])))
            if step.get('notes'):
                out.append('<ul>{}</ul>'.format(''.join('<li>{}</li>'.format(text(note)) for note in step['notes'])))
            if step.get('log_errors'):
                out.append('<details><summary>{} error{} in the Output Log during this step</summary><ul>{}</ul></details>'.format(
                    len(step['log_errors']), '' if len(step['log_errors']) == 1 else 's',
                    ''.join('<li><code>{}</code></li>'.format(text(line)) for line in step['log_errors'])))
            picture = folder / part['name'] / step['picture'] if step.get('picture') else None
            if picture and picture.is_file():
                out.append('<img alt="{}" src="{}">'.format(text(step['title']), data_uri(picture, 'image/png')))
            elif result not in ('not reached',):
                out.append('<p class="soft">No picture was saved for this step.</p>')
            out.append('</section>')
    out.append('<h2>Recording</h2>')
    recording = summary.get('recording') or dict(recorded=False, reason='The run left no word about a recording.')
    video = folder / recording.get('file', '') if recording.get('recorded') else None
    if video and video.is_file():
        embedded = video.stat().st_size <= EMBED_VIDEO
        out.append('<video controls preload="metadata" src="{}"></video>'.format(
            data_uri(video, 'video/mp4') if embedded else text(video.name)))
        out.append('<p class="soft">The desktop\'s screen for the whole run{}.</p>'.format(
            '' if embedded else ', in the file {} beside this page: it is too large to put inside it'.format(text(video.name))))
    else:
        out.append('<p>{}</p>'.format(text(recording.get('reason') or 'The recording was not brought back.')))
    out.append('</main></body></html>')
    return '\n'.join(out)


# ---- the laptop's side -----------------------------------------------------------------------------------------

def remote(words, mirror=None, run=subprocess.run):
    command = [sys.executable, str(HERE / 'remote_unreal.py')] + (['--mirror', str(mirror)] if mirror else []) + words
    return run(command, check=False).returncode


def source_state(run=subprocess.run):
    """The checkout the run is made from: its commit, and whether files differ from it."""
    def git(*words):
        done = run(['git', '-C', str(REPO), *words], capture_output=True, text=True, check=False)
        return done.stdout.strip() if done.returncode == 0 else ''
    commit = git('rev-parse', 'HEAD')
    if not commit:
        return ''
    return 'commit ' + commit + (' with uncommitted changes' if git('status', '--porcelain') else '')


def desktop_words(args, values):
    """The command the desktop runs: this script's --desktop side with the same inputs."""
    words = ['py', 'tools/kadan_walkthrough.py', '--desktop', '--name', args.name, '--built', args.built,
             '--stubs', args.stubs, '--timeout', str(args.timeout), '--parts', *args.parts]
    for key, value in values.items():
        if value:
            words += ['--' + key.replace('_', '-'), value]
    return words


def laptop(args, values, call=remote):
    """Starts the run on the desktop, waits for it, fetches what it left and writes the page. Returns the exit code."""
    relative = '{}/{}/results'.format(WORK, args.name)
    source = source_state()
    if call(['run', '--background', args.name, '--session', '--'] + desktop_words(args, values), args.mirror):
        print('The run could not be started on the desktop.')
        return 1
    waited = call(['wait', args.name], args.mirror)
    if call(['fetch', relative, '--limit-mb', '2000'], args.mirror):
        print('The desktop\'s results could not be fetched (the run ended with exit code {}).'.format(waited))
        return 1
    folder = REPO / 'private/desktop-results' / relative
    summary = dict(json.loads((folder / 'walkthrough.json').read_text(encoding='utf-8')), source=source)
    (folder / 'walkthrough.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')
    target = folder / 'walkthrough.html'
    target.write_text(page(summary, folder), encoding='utf-8')
    for part in summary['parts']:
        print('{}: {}{}'.format(part['name'], part['state'], ' - {} {}'.format(NOT_RUN, part['reason'])
                                if part['state'] == 'not run' else ''))
        for step in part.get('steps', []):
            if step['result'] not in ('passed', 'not reached'):
                print('  {:02d} {}: {} - {}'.format(step['number'], step['id'], step['result'], step.get('error', '')))
            elif step['result'] == 'passed' and not step.get('picture'):
                print('  {:02d} {}: passed without a picture - {}'.format(step['number'], step['id'], ' '.join(
                    note for note in step.get('notes', []) if note.startswith('No picture'))))
    print('page:', target)
    return exit_code(summary)


def main(argv=None, environment=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in INPUTS:
        parser.add_argument('--' + name.replace('_', '-'))
    parser.add_argument('--parts', nargs='+', choices=tuple(PARTS), default=list(PARTS))
    parser.add_argument('--name', default='walk', help='the run\'s name: its folder on the desktop and its background run')
    parser.add_argument('--mirror', type=int, help='the desktop mirror to run in (tools/remote_unreal.py --mirror)')
    parser.add_argument('--built', default=BUILT, help='the dev_build.py host whose built plugin is tested')
    parser.add_argument('--stubs', default=STUBS, help='a built P4Stubs plugin folder on the desktop')
    parser.add_argument('--timeout', type=float, default=10800.0, help='seconds one editor start may take')
    parser.add_argument('--desktop', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--page', type=Path, help='only write walkthrough.html again from a fetched results folder')
    args = parser.parse_args(argv)
    values = inputs(args, os.environ if environment is None else environment)
    for key in ('map', 'earlier_map'):
        if values[key] and not re.fullmatch(r'/Game/[A-Za-z0-9_/-]+', values[key]):
            # Git Bash turns /Game/... into a Windows path before Python sees it: MSYS_NO_PATHCONV=1 stops it.
            parser.error('--{} must be a /Game package path, not {} (in Git Bash, set MSYS_NO_PATHCONV=1)'.format(
                key.replace('_', '-'), values[key]))
    if args.page:
        summary = json.loads((args.page / 'walkthrough.json').read_text(encoding='utf-8'))
        (args.page / 'walkthrough.html').write_text(page(summary, args.page), encoding='utf-8')
        print('page:', args.page / 'walkthrough.html')
        return exit_code(summary)
    if args.desktop:
        return desktop(args, values)
    # Nothing is sent to the desktop for a run in which no part could run.
    reasons = {name: missing_inputs(values, name) for name in args.parts}
    if all(reasons.values()):
        for name, reason in reasons.items():
            print('{}: {} {}'.format(name, NOT_RUN, reason))
        return 2
    return laptop(args, values)


if __name__ == '__main__':
    sys.exit(main())
