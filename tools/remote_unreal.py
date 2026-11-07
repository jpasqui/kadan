"""Run the project's Unreal work on the desktop. Nothing of Unreal runs on the laptop.

The desktop holds a mirror of the repository (local connection settings name it): every tracked file, and the
private folders the Unreal work reads. The laptop edits files, sends them across and asks for a command
to be run there.

    py -3.11 tools/remote_unreal.py sync [--all]
    py -3.11 tools/remote_unreal.py run [--env NAME=VALUE ...] -- <command...>
    py -3.11 tools/remote_unreal.py run --background NAME [--session] -- <command...>
    py -3.11 tools/remote_unreal.py wait NAME
    py -3.11 tools/remote_unreal.py log [NAME] [--tail N]
    py -3.11 tools/remote_unreal.py stop NAME
    py -3.11 tools/remote_unreal.py fetch <path...> [--to folder]
    py -3.11 tools/remote_unreal.py push <path...> [--skip name ...]

Configure the desktop host and mirror root in ignored `.remote/config.json`, P4_UNREAL_HOST and
P4_UNREAL_ROOT, or --host and --root. docs/REMOTE_UNREAL.md lists the settings. `--mirror 2` or `--mirror 3` before the
action, or P4_UNREAL_MIRROR=2 in the environment, adds " 2" or " 3" to the configured base folder. Each mirror takes the files of the
checkout the command is run from, so one worker and one checkout use one mirror. Builds in the
mirrors use their own build outputs; editors do too. Check process ownership and memory before launching.

sync   sends the files under SYNC that differ from the mirror's (tracked files and files that are
       neither tracked nor ignored, uncommitted edits included) and removes from the mirror the files it
       sent earlier that are gone here. Files are compared by content. --all also sends every other
       tracked file of the repository.
run    syncs, then runs the command in the mirror's root with the engine's folder in P4_UE_ROOT, and
       ends with the command's exit code. `py`, `py -3.11` and `python` at the start of the command
       become the desktop's Python. The command is tied to the connection: a run of more than a few
       minutes is started with --background NAME, which returns at once. Its output goes to
       private/remote-runs/NAME/log.txt in the mirror, and closing the laptop does not stop it.
wait   prints the output of a background run as it comes and ends with its exit code. It can be
       stopped and started again. log prints the end of the output and whether the run goes on;
       without a name it lists the runs. stop ends a background run and everything it started.
fetch  brings named result files or small folders back, into private/desktop-results/<path>.
push   sends named files or folders of this machine to the same place in the mirror, whole, without
       the folders Unreal makes again (MADE) and without folder links. A file the mirror has with the
       same size and time is not sent again. For the private folders a script reads.

Paths are relative to the repository root on both machines.

An SSH command runs without a visible desktop. When an editor task needs an interactive rendering
session, use --background NAME --session. This schedules the job in the session of the person logged
in at the desktop, and removes the task when the ended run is inspected with wait or log.
Somebody has to be logged in there.

Connection configuration is documented in docs/REMOTE_UNREAL.md. The native validation boundaries
are recorded in docs/NATIVE_REWRITE.md.
"""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import time
from pathlib import Path, PureWindowsPath

# Connection values belong in ignored .remote/config.json, the environment, or CLI arguments.
# Importing this module and running its desktop-side worker need no laptop connection settings.
HOST = ''
KEY = None
MIRROR = ''
BASE_MIRROR = ''
MIRRORS = (1, 2, 3)                                 # mirror N adds " N" to the configured base folder
ENGINE = ''
PYTHON = ['py', '-3.12']                            # override when the desktop uses another launcher
DEFAULT_PAKS = 'private/game-paks'
PAKS = DEFAULT_PAKS                                # the game's Paks folder in the mirror (hard links)
SYNC = ('Kadan', 'core', 'compiler', 'tests', 'tools')   # compiler: the plugin's Maxwell compiler
MADE = ('Binaries', 'Intermediate', 'DerivedDataCache')   # push and fetch leave these behind
RUNS = 'private/remote-runs'                        # in the mirror: one folder per background run
FETCHED = 'private/desktop-results'                 # on the laptop: where fetch puts what it brings
FETCH_LIMIT = 500 * 2**20                           # fetch refuses more than this
SPARE = 20 * 2**30                                  # push leaves this much of the desktop's disk free
ENVIRONMENT = {'PYTHONIOENCODING': 'utf-8', 'PYTHONUNBUFFERED': '1'}



def use_mirror(number):
    """Works in mirror `number` from here on. The game's Paks are hard links to the same files in each."""
    global MIRROR
    MIRROR = BASE_MIRROR if number == 1 else '{} {}'.format(BASE_MIRROR, number)
    ENVIRONMENT['P4_NATIVE_ORIGINAL_CONTAINERS'] = MIRROR + '/' + PAKS


HERE = Path(__file__).resolve().parent
REPO = HERE.parent          # on the desktop this is the mirror
TOOL = 'tools/remote_unreal.py'
LISTED = REPO / '.remote/synced.json'
SSH = []
SMALL = 2**20         # a file up to this size is read whole, with others, before it goes into a tar stream
BLOCK = 8 * 2**20     # a tar stream goes to ssh this much at a time: small writes with gaps between them crawl
NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}')


def python_command(value):
    """A JSON array avoids shell-dependent quoting of a remote Python executable."""
    words = json.loads(value) if isinstance(value, str) else value
    if not isinstance(words, list) or not words or any(not isinstance(word, str) or not word or
            any(mark in word for mark in ('\n', '\r', '\x00', '&', '|', '<', '>', '^', '%')) for word in words):
        raise ValueError('Remote Python must be a nonempty JSON array of command arguments')
    return words


def remote_settings(config=None, environment=None, overrides=None):
    """Read local settings; CLI values override the environment, which overrides the file."""
    environment = os.environ if environment is None else environment
    supplied = config if config is not None else environment.get('P4_UNREAL_CONFIG')
    path = Path(supplied).expanduser() if supplied else REPO / '.remote/config.json'
    values = {'python': ['py', '-3.12'], 'paks': DEFAULT_PAKS}
    if path.is_file():
        loaded = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(loaded, dict) or set(loaded) - {'host', 'root', 'key', 'engine', 'python', 'ssh', 'paks'}:
            raise ValueError('Remote configuration must contain only documented connection settings')
        values.update(loaded)
    elif supplied:
        raise ValueError('The configured remote settings file is missing')
    for field, variable in (('host', 'P4_UNREAL_HOST'), ('root', 'P4_UNREAL_ROOT'), ('key', 'P4_UNREAL_KEY'),
                            ('engine', 'P4_UNREAL_ENGINE'), ('python', 'P4_UNREAL_PYTHON'),
                            ('ssh', 'P4_UNREAL_SSH'), ('paks', 'P4_UNREAL_PAKS')):
        if variable in environment:
            values[field] = environment[variable]
    values.update({key: value for key, value in (overrides or {}).items() if value is not None})
    if not isinstance(values.get('host'), str) or not re.fullmatch(r'[A-Za-z0-9_.@:\[\]-]+', values['host']) \
            or values['host'].startswith('-'):
        raise ValueError('Set the desktop host with --host, P4_UNREAL_HOST, or .remote/config.json')
    for field in ('root', 'engine'):
        value = values.get(field)
        if field == 'engine' and not value:
            continue
        if not isinstance(value, str) or not PureWindowsPath(value).is_absolute() or \
                any(mark in value for mark in ('\n', '\r', '\x00', '"', '&', '|', '<', '>', '^', '%')):
            raise ValueError('The remote ' + field + ' must be an absolute Windows path without shell control characters')
        values[field] = value.replace('\\', '/').rstrip('/')
    values['python'] = python_command(values['python'])
    paks = values['paks']
    if not isinstance(paks, str) or not paks or paks.startswith(('/', '\\')) or ':' in paks or \
            any(part == '..' for part in paks.replace('\\', '/').split('/')):
        raise ValueError('The remote Paks folder must be relative to the mirror')
    values['paks'] = paks.replace('\\', '/')
    for field in ('key', 'ssh'):
        if values.get(field) is not None and not isinstance(values[field], str):
            raise ValueError('The remote ' + field + ' setting must be text')
    return values


def configure_remote(args):
    """Apply one validated connection before any client-side network operation."""
    global HOST, KEY, MIRROR, BASE_MIRROR, ENGINE, PYTHON, PAKS, SSH
    values = remote_settings(args.config, overrides={
        field: getattr(args, field) for field in ('host', 'root', 'key', 'engine', 'python', 'ssh', 'paks')})
    HOST, BASE_MIRROR = values['host'], values['root']
    MIRROR = BASE_MIRROR
    KEY = Path(values['key']).expanduser() if values.get('key') else None
    ENGINE, PYTHON, PAKS = values.get('engine', ''), values['python'], values['paks']
    executable = values.get('ssh')
    if not executable:
        git = shutil.which('git')
        if git:
            executable = next((str(folder / 'usr/bin/ssh.exe') for folder in Path(git).resolve().parents
                               if (folder / 'usr/bin/ssh.exe').is_file()), None)
        executable = executable or shutil.which('ssh')
    if not executable:
        raise ValueError('An SSH client is required; set --ssh or P4_UNREAL_SSH')
    SSH = [executable, *(['-i', str(KEY)] if KEY else []), '-o', 'BatchMode=yes', '-o', 'LogLevel=ERROR', HOST]
    ENVIRONMENT.pop('P4_UE_ROOT', None)
    if ENGINE:
        ENVIRONMENT['P4_UE_ROOT'] = ENGINE
    use_mirror(args.mirror)


def digest(path):
    with open(path, 'rb') as stream:
        return hashlib.sha1(stream.read()).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1), encoding='utf-8')


def read(path):
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else None


def unpack(stream, target):
    """Extracts a tar stream below target. A name that leads outside it is refused."""
    count = 0
    with tarfile.open(fileobj=stream, mode='r|') as tar:
        for member in tar:
            parts = Path(member.name).parts
            if not member.isfile() or member.name.startswith(('/', '\\')) or '..' in parts or ':' in member.name:
                raise SystemExit('refused in a tar stream: ' + member.name)
            try:
                tar.extract(member, str(target), filter='data')
            except TypeError:      # a Python before the filters
                tar.extract(member, str(target))
            count += 1
    return count


class Blocks:
    """Collects the small writes of a tar stream and passes them on a block at a time."""

    def __init__(self, stream):
        self.stream, self.held = stream, bytearray()

    def write(self, data):
        self.held += data
        if len(self.held) >= BLOCK:
            self.finish()
        return len(data)

    def finish(self):
        self.stream.write(self.held)
        self.stream.flush()
        self.held = bytearray()


def pack(stream, base, names, progress=None):
    """Writes the named files (relative to base) to a tar stream. Returns the names that were gone.
    Small files are read several at a time: a first read of a small file costs milliseconds on the laptop."""
    def load(name):
        try:
            status = os.stat(str(base / name))
            if status.st_size > SMALL:
                return status, None
            with open(str(base / name), 'rb') as source:
                return status, source.read()
        except OSError:
            return None, None

    gone, count = [], 0
    blocks = Blocks(stream)
    # dereference: a file that is a hard link of another goes as a file of its own.
    with ThreadPoolExecutor(8) as pool, tarfile.open(fileobj=blocks, mode='w|', format=tarfile.PAX_FORMAT, dereference=True) as tar:
        for first in range(0, len(names), 256):
            batch = names[first:first + 256]
            for name, (status, data) in zip(batch, pool.map(load, batch)):
                if status is None:
                    gone.append(name)
                elif data is None:
                    try:
                        tar.add(str(base / name), arcname=name, recursive=False)
                    except FileNotFoundError:
                        gone.append(name)
                else:
                    member = tarfile.TarInfo(name)
                    member.size, member.mtime, member.mode = len(data), status.st_mtime, 0o644
                    tar.addfile(member, io.BytesIO(data))
                count += 1
                if progress:
                    progress(count)
    blocks.finish()
    return gone


def walk(base, names, skip=()):
    """[(relative name, size, time)] of every file under the named files and folders of base, without the
    folders Unreal makes again, without folder links, and without what `skip` names (a name, or the end of a path)."""
    def skipped(relative):
        return any(relative == item or relative.endswith('/' + item) for item in skip)

    found = []
    for name in names:
        source = Path(os.path.abspath(str(base / name)))
        relative = Path(os.path.relpath(str(source), str(base))).as_posix()
        if relative.startswith('..'):
            raise SystemExit('not inside ' + str(base) + ': ' + name)
        if source.is_file():
            status = source.stat()
            found.append((relative, status.st_size, int(status.st_mtime)))
            continue
        if not source.is_dir():
            raise SystemExit('not there: ' + name)
        for folder, folders, files in os.walk(str(source)):
            inside = Path(os.path.relpath(folder, str(base))).as_posix()
            folders[:] = sorted(one for one in folders if one not in MADE and not skipped(inside + '/' + one) and not
                                os.lstat(os.path.join(folder, one)).st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)
            for one in sorted(files):
                if skipped(inside + '/' + one):
                    continue
                try:
                    status = os.stat(os.path.join(folder, one))
                except OSError:
                    continue
                found.append((inside + '/' + one, status.st_size, int(status.st_mtime)))
    return found


# ---------------------------------------------------------------------------------------------------
# The laptop's side.

def desktop(*words):
    """The ssh command that runs this file's desktop side."""
    return SSH + [subprocess.list2cmdline(PYTHON + [MIRROR + '/' + TOOL] + list(words))]


def ask(word, request, *more):
    """Runs a desktop-side step that takes JSON and answers JSON."""
    result = subprocess.run(desktop(word, *more), input=json.dumps(request).encode('utf-8'), stdout=subprocess.PIPE)
    if result.returncode:
        raise SystemExit(result.stdout.decode('utf-8', 'replace').strip() or 'the desktop did not answer ({})'.format(word))
    return json.loads(result.stdout.decode('utf-8'))


def send(names, note=None):
    """Sends the named files of the repository to the same places in the mirror."""
    process = subprocess.Popen(desktop('_unpack'), stdin=subprocess.PIPE)
    try:
        gone = pack(process.stdin, REPO, names, note)
        process.stdin.close()
    except OSError:      # the desktop's side ended and said why
        gone = None
    if process.wait() or gone is None:
        raise SystemExit('the desktop did not take the files')
    return gone


def send_tool():
    """Puts this file into the mirror with the desktop's own tar: the first time, and when it changed."""
    folder = MIRROR.replace('/', '\\')
    process = subprocess.Popen(SSH + ['(if not exist "{0}" mkdir "{0}") && tar -xf - -C "{0}"'.format(folder)], stdin=subprocess.PIPE)
    pack(process.stdin, REPO, [TOOL])
    process.stdin.close()
    if process.wait():
        raise SystemExit('could not put {} into the mirror'.format(TOOL))


def listed(*arguments):
    out = subprocess.run(['git', '-C', str(REPO), 'ls-files', '-z', *arguments], stdout=subprocess.PIPE, check=True).stdout
    return {name for name in out.decode('utf-8').split('\0') if name}


def sync(everything=False):
    start = time.time()
    names = listed('-c', '-o', '--exclude-standard', '--', *SYNC)
    if everything:
        names |= listed('-c')
    names = sorted(name for name in names if (REPO / name).is_file())
    here = {name: digest(REPO / name) for name in names}
    request = dict(files=names, scope=None if everything else list(SYNC))
    probe = subprocess.run(desktop('_state'), input=json.dumps(request).encode('utf-8'), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    there = json.loads(probe.stdout.decode('utf-8')) if probe.returncode == 0 else None
    if there is None or there.get('tool') != here.get(TOOL):
        send_tool()
        there = ask('_state', request)
    changed = [name for name in names if there['have'].get(name) != here[name]]
    if changed:
        send(changed)
    if changed or there['gone'] or not there['same']:
        ask('_synced', request)
    print('sync: {} files, {} sent, {} removed, {:.1f} s'.format(len(names), len(changed), len(there['gone']), time.time() - start),
          file=sys.stderr)
    for name in (changed + ['removed ' + name for name in there['gone']])[:12]:
        print('  ' + name, file=sys.stderr)


def job_of(args):
    command = list(args.command)
    if command[:1] == ['--']:
        command = command[1:]
    if not command:
        raise SystemExit('no command given: run -- <command...>')
    if command[0] in ('py', 'python', 'python3'):
        command = ['{python}'] + command[2 if re.fullmatch(r'-3(\.\d+)?', ''.join(command[1:2])) else 1:]
    environment = dict(ENVIRONMENT)
    for item in args.env:
        key, value = item.split('=', 1)
        environment[key] = value
    return dict(command=command, env=environment)


def run(args):
    job = job_of(args)
    if args.session and not args.background:
        raise SystemExit('--session goes with --background NAME')
    if args.background and not NAME.fullmatch(args.background):
        raise SystemExit('a run name is letters, digits, dot, dash and underscore')
    if not args.no_sync:
        sync()
    if args.background:
        print(ask('_start', job, args.background, *(['--session'] if args.session else []))['told'])
        return 0
    return subprocess.run(desktop('_run'), input=json.dumps(job).encode('utf-8')).returncode


def poll(name, offset):
    result = subprocess.run(desktop('_poll', name, str(offset)), stdout=subprocess.PIPE)
    if result.returncode:
        raise SystemExit('no run named {} on the desktop'.format(name))
    header, _, data = result.stdout.partition(b'\n')
    return json.loads(header.decode('utf-8')), data


def told(header):
    if header['done']:
        if header['done'].get('error'):
            return '{} after {} s'.format(header['done']['error'], header['done'].get('seconds'))
        return 'ended with exit code {} after {} s'.format(header['done'].get('exit'), header['done'].get('seconds'))
    return 'still running' if header['alive'] else 'ended without a result (stopped from outside?)'


def wait(args):
    offset, lost, unanswered, start = 0, 0, 0, time.time()
    while True:
        # One failed connection says nothing about the run: keep asking for two minutes before giving up.
        try:
            header, data = poll(args.name, offset)
            unanswered = 0
        except (SystemExit, ValueError) as error:
            unanswered += 1
            if unanswered * args.every > 120:
                print('{}: the desktop stopped answering: {}'.format(args.name, error), file=sys.stderr)
                return 1
            time.sleep(args.every)
            continue
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        offset += len(data)
        if header['done'] and offset >= header['size']:
            print('{}: {}'.format(args.name, told(header)), file=sys.stderr)
            code = header['done'].get('exit')
            return code if isinstance(code, int) else 1
        # A loaded desktop can miss a look at the runner for a while; only a long gap means it really died.
        lost = 0 if header['alive'] or header['done'] else lost + 1
        if lost * args.every > 120:
            print('{}: {}'.format(args.name, told(header)), file=sys.stderr)
            return 1
        if args.timeout and time.time() - start > args.timeout:
            print('{}: still running; wait again to go on'.format(args.name), file=sys.stderr)
            return 124
        time.sleep(args.every)


def log(args):
    if not args.name:
        for row in ask('_runs', {}):
            print('{:<28} {}'.format(row['name'], told(row)))
        return 0
    header, data = poll(args.name, 0)
    lines = data.splitlines(keepends=True)
    sys.stdout.buffer.write(b''.join(lines[-args.tail:]))
    sys.stdout.buffer.flush()
    print('{}: {}'.format(args.name, told(header)), file=sys.stderr)
    return 0


def stop(args):
    print(ask('_stop', {}, args.name)['told'])
    return 0


def fetch(args):
    size = ask('_size', dict(paths=args.paths))
    if size['missing']:
        raise SystemExit('not in the mirror: ' + ', '.join(size['missing']))
    if size['bytes'] > args.limit_mb * 2**20:
        raise SystemExit('{:.0f} MB in {} files is more than fetch brings ({} MB): name the result files'.format(
            size['bytes'] / 2**20, size['files'], args.limit_mb))
    target = (REPO / args.to).resolve()
    target.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen(desktop('_pack'), stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    process.stdin.write(json.dumps(dict(paths=args.paths)).encode('utf-8'))
    process.stdin.close()
    count = unpack(process.stdout, target)
    # Streaming tar may stop before trailing padding; let the sender finish.
    while process.stdout.read(64 * 1024):
        pass
    if process.wait():
        raise SystemExit('the desktop did not send the files')
    print('fetched {} files, {:.1f} MB, into {}'.format(count, size['bytes'] / 2**20, target))
    return 0


def push(args):
    start = time.time()
    found = walk(REPO, args.paths, args.skip)
    reply = ask('_lacks', dict(files=found))
    lacking = [found[index] for index in reply['lacks']]
    total = sum(size for _, size, _ in lacking)
    print('push: {} files, {:.2f} GB here; {} files, {:.2f} GB to send; the desktop has {:.1f} GB free'.format(
        len(found), sum(size for _, size, _ in found) / 2**30, len(lacking), total / 2**30, reply['free'] / 2**30), file=sys.stderr)
    if total + SPARE > reply['free']:
        raise SystemExit('not sent: it would leave the desktop less than {:.0f} GB free'.format(SPARE / 2**30))
    if not lacking:
        return 0
    state = dict(last=time.time(), bytes=0)

    def note(count):
        state['bytes'] += lacking[count - 1][1]
        if time.time() - state['last'] > 15:
            state['last'] = time.time()
            print('  {} of {} files, {:.2f} of {:.2f} GB'.format(count, len(lacking), state['bytes'] / 2**30, total / 2**30),
                  file=sys.stderr, flush=True)
    gone = send([name for name, _, _ in lacking], note)
    seconds = time.time() - start
    print('pushed {} files, {:.2f} GB in {:.0f} s ({:.0f} MB/s){}'.format(
        len(lacking) - len(gone), total / 2**30, seconds, total / 2**20 / max(seconds, 1),
        ', {} were gone when read'.format(len(gone)) if gone else ''), file=sys.stderr)
    return 0


# ---------------------------------------------------------------------------------------------------
# The desktop's side: the same file in the mirror, started over ssh by the functions above.

def safe(name):
    parts = Path(name).parts
    if not name or name.startswith(('/', '\\')) or ':' in name or '..' in parts:
        raise SystemExit('not a path inside the mirror: ' + name)
    return REPO / name


def within(name, scope):
    return scope is None or any(name == one or name.startswith(one + '/') for one in scope)


def alive(pid):
    import ctypes
    handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, int(pid))
    if not handle:
        return False
    code = ctypes.c_ulong()
    found = ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    ctypes.windll.kernel32.CloseHandle(handle)
    return bool(found) and code.value == 259


def state_of(name):
    folder = safe(RUNS + '/' + name)
    state, done = read(folder / 'state.json'), read(folder / 'done.json')
    return dict(name=name, done=done, alive=bool(state) and not done and alive(state['runner']))


def started(job):
    command = [sys.executable.replace('pythonw.exe', 'python.exe') if word == '{python}' else word for word in job['command']]
    return command, dict(os.environ, **job['env'])


def on_desktop(word, rest):
    request = json.loads(sys.stdin.buffer.read().decode('utf-8')) if word not in ('_unpack', '_job', '_poll') else None
    if word == '_unpack':
        unpack(sys.stdin.buffer, REPO)
        return 0
    if word in ('_state', '_synced'):
        files, scope = request['files'], request['scope']
        wanted = set(files)
        before = read(LISTED) or []
        gone = [name for name in before if within(name, scope) and name not in wanted]
        if word == '_state':
            answer = dict(have={name: digest(safe(name)) for name in files if safe(name).is_file()}, gone=gone,
                          same={name for name in before if within(name, scope)} == wanted, tool=digest(__file__))
        else:
            for name in gone:
                path = safe(name)
                if path.is_file():
                    path.unlink()
                    folder = path.parent
                    while folder != REPO and not any(folder.iterdir()):
                        folder.rmdir()
                        folder = folder.parent
            write(LISTED, sorted({name for name in before if not within(name, scope)} | wanted))
            answer = dict(removed=len(gone))
    elif word == '_lacks':
        lacks = []
        for index, (name, size, moment) in enumerate(request['files']):
            path = safe(name)
            try:
                status = path.stat()
            except OSError:
                lacks.append(index)
                continue
            if status.st_size != size or abs(int(status.st_mtime) - moment) > 2:
                lacks.append(index)
        answer = dict(lacks=lacks, free=shutil.disk_usage(str(REPO)).free)
    elif word in ('_size', '_pack'):
        missing = [name for name in request['paths'] if not safe(name).exists()]
        found = walk(REPO, [name for name in request['paths'] if name not in missing])
        if word == '_pack':
            pack(sys.stdout.buffer, REPO, [name for name, _, _ in found])
            sys.stdout.buffer.flush()
            return 0
        answer = dict(missing=missing, files=len(found), bytes=sum(size for _, size, _ in found))
    elif word == '_run':
        command, environment = started(request)
        return subprocess.run(command, cwd=str(REPO), env=environment, stdin=subprocess.DEVNULL).returncode
    elif word == '_start':
        answer = start_job(rest[0], request, '--session' in rest)
    elif word == '_job':
        return run_job(rest[0])
    elif word == '_poll':
        folder, offset = safe(RUNS + '/' + rest[0]), int(rest[1])
        if not (folder / 'job.json').is_file():
            return 1
        data = b''
        if (folder / 'log.txt').is_file():
            with open(folder / 'log.txt', 'rb') as stream:
                stream.seek(offset)
                data = stream.read()
        header = dict(state_of(rest[0]), size=offset + len(data))
        task = (read(folder / 'job.json') or {}).get('task')
        if task and header['done']:
            # The run cannot remove its own task (it has the session's rights, the task was made with ssh's).
            subprocess.run(['schtasks', '/Delete', '/TN', task, '/F'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        sys.stdout.buffer.write(json.dumps(header).encode('utf-8') + b'\n' + data)
        sys.stdout.buffer.flush()
        return 0
    elif word == '_runs':
        folder = REPO / RUNS
        answer = [state_of(one.name) for one in sorted(folder.iterdir()) if (one / 'job.json').is_file()] if folder.is_dir() else []
    elif word == '_stop':
        folder = safe(RUNS + '/' + rest[0])
        state = read(folder / 'state.json')
        if not state or (folder / 'done.json').is_file() or not alive(state['runner']):
            answer = dict(told='{} is not running'.format(rest[0]))
        else:
            # Only the run's own processes: the one that holds the run and everything under it.
            subprocess.run(['taskkill', '/PID', str(state['runner']), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            write(folder / 'done.json', dict(exit=None, seconds=round(time.time() - state['started'], 1), error='stopped'))
            task = (read(folder / 'job.json') or {}).get('task')
            if task:
                subprocess.run(['schtasks', '/Delete', '/TN', task, '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            answer = dict(told='stopped {}'.format(rest[0]))
    else:
        raise SystemExit('unknown step ' + word)
    sys.stdout.buffer.write(json.dumps(answer).encode('utf-8'))
    return 0


def start_job(name, job, session):
    """Starts a run that does not end with the ssh connection: outside the connection's job object, or,
    with `session`, as a scheduled task in the session of the person logged in."""
    if not NAME.fullmatch(name):
        raise SystemExit('a run name is letters, digits, dot, dash and underscore')
    folder = REPO / RUNS / name
    if state_of(name)['alive']:
        raise SystemExit('a run named {} is still going: wait for it, stop it or take another name'.format(name))
    for one in ('done.json', 'state.json', 'log.txt'):
        if (folder / one).is_file():
            (folder / one).unlink()
    # The mirror's number is in the task's name: the same run name in two mirrors is two tasks.
    job['task'] = '{}-{}'.format(REPO.name.replace(' ', ''), name) if session else None
    write(folder / 'job.json', job)
    holder = [str(Path(sys.executable).with_name('pythonw.exe')), str(REPO / TOOL), '_job', name]   # no console window
    if session:
        # -Priority 4 is normal. A task's default (7) is background priority for the processor and the disk.
        script = '\n'.join([
            "$action = New-ScheduledTaskAction -Execute '{}' -Argument '\"{}\" _job {}' -WorkingDirectory '{}'".format(holder[0], holder[1], name, REPO),
            '$principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\\$env:USERNAME" -LogonType Interactive -RunLevel Limited',
            '$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 24) -Priority 4',
            "Register-ScheduledTask -TaskName '{}' -Action $action -Principal $principal -Settings $settings -Force | Out-Null".format(job['task']),
            "Start-ScheduledTask -TaskName '{}'".format(job['task'])])
        encoded = base64.b64encode(script.encode('utf-16-le')).decode('ascii')
        result = subprocess.run(['powershell', '-NoProfile', '-EncodedCommand', encoded], stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit('the scheduled task was not made: ' + result.stdout.decode('utf-8', 'replace')[-600:])
    else:
        subprocess.Popen(holder, cwd=str(REPO), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=subprocess.CREATE_BREAKAWAY_FROM_JOB | subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)
    end = time.time() + 30
    while time.time() < end and not (folder / 'state.json').is_file() and not (folder / 'done.json').is_file():
        time.sleep(0.5)
    if not (folder / 'state.json').is_file() and not (folder / 'done.json').is_file():
        if session:
            subprocess.run(['schtasks', '/Delete', '/TN', job['task'], '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        raise SystemExit('{} did not start{}'.format(name, ': is somebody logged in at the desktop?' if session else ''))
    return dict(told='started {}{}: its output is {}/{}/log.txt in the mirror'.format(
        name, ' in the logged-in session' if session else '', RUNS, name))


def performance_cores():
    """The affinity mask of the processor's fastest cores (the highest efficiency class), or 0 if unknown.
    On the desktop, Windows schedules a process started over ssh on the four efficiency cores only, which
    made builds and imports several times slower; a run pinned to the performance cores, and every process
    it starts (they inherit the mask), uses those."""
    import ctypes
    kernel = ctypes.windll.kernel32
    size = ctypes.c_ulong(0)
    kernel.GetSystemCpuSetInformation(None, 0, ctypes.byref(size), None, 0)
    if not size.value:
        return 0
    buffer = ctypes.create_string_buffer(size.value)
    if not kernel.GetSystemCpuSetInformation(buffer, size, ctypes.byref(size), None, 0):
        return 0
    data, at, cpus = buffer.raw[:size.value], 0, []
    # SYSTEM_CPU_SET_INFORMATION: Size, Type, then Id, Group, LogicalProcessorIndex, ..., EfficiencyClass at 18.
    while at + 19 <= len(data):
        entry = int.from_bytes(data[at:at + 4], 'little')
        if not entry:
            break
        if int.from_bytes(data[at + 12:at + 14], 'little') == 0:   # processor group 0
            cpus.append((data[at + 18], data[at + 14]))
        at += entry
    fastest = max((efficiency for efficiency, _ in cpus), default=0)
    return sum(1 << index for efficiency, index in cpus if efficiency == fastest and index < 64)


def run_job(name):
    """Holds one background run: the command's output goes to the run's log, its exit code to done.json."""
    folder = REPO / RUNS / name
    job = read(folder / 'job.json')
    start, code, error = time.time(), None, None
    try:
        try:
            mask = performance_cores()
            if mask:
                import ctypes
                kernel = ctypes.windll.kernel32
                kernel.GetCurrentProcess.restype = ctypes.c_void_p
                kernel.SetProcessAffinityMask(ctypes.c_void_p(kernel.GetCurrentProcess()), ctypes.c_size_t(mask))
        except Exception:     # pinning only speeds the run up; without it the run still runs
            pass
        command, environment = started(job)
        with open(folder / 'log.txt', 'wb') as stream:
            process = subprocess.Popen(command, cwd=str(REPO), env=environment, stdin=subprocess.DEVNULL, stdout=stream,
                                       stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            write(folder / 'state.json', dict(runner=os.getpid(), pid=process.pid, started=start))
            code = process.wait()
    except Exception as problem:     # the run's record says why nothing ran
        error = repr(problem)
    write(folder / 'done.json', dict(exit=code, seconds=round(time.time() - start, 1), error=error))
    return 0     # a session run's scheduled task is removed by the next wait or log


def main(argv):
    if argv and argv[0].startswith('_'):
        return on_desktop(argv[0], argv[1:])
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--config', type=Path, help='local JSON settings file (default .remote/config.json or P4_UNREAL_CONFIG)')
    parser.add_argument('--host', help='SSH host or SSH configuration alias')
    parser.add_argument('--root', help='absolute repository mirror folder on the Windows desktop')
    parser.add_argument('--key', help='local SSH identity file; otherwise use the SSH client configuration')
    parser.add_argument('--engine', help='absolute Unreal installation folder on the desktop')
    parser.add_argument('--python', help='remote Python command as a JSON array, such as ["py", "-3.12"]')
    parser.add_argument('--ssh', help='local SSH client executable')
    parser.add_argument('--paks', help='Paks folder relative to the mirror (default private/game-paks)')
    parser.add_argument('--mirror', type=int, choices=MIRRORS, default=int(os.environ.get('P4_UNREAL_MIRROR') or 1),
                        help='which mirror on the desktop to work in (default 1, or P4_UNREAL_MIRROR)')
    sub = parser.add_subparsers(dest='action', required=True)
    one = sub.add_parser('sync')
    one.add_argument('--all', action='store_true', help='every tracked file of the repository, not only those under SYNC')
    one = sub.add_parser('run')
    one.add_argument('--background', metavar='NAME', help='start the command and return; collect it with wait NAME or log NAME')
    one.add_argument('--session', action='store_true', help='with --background: run in the session of the person logged in at the desktop')
    one.add_argument('--env', action='append', default=[], metavar='NAME=VALUE', help='an environment variable for the command')
    one.add_argument('--no-sync', action='store_true', help='do not send changed files first')
    one.add_argument('command', nargs=argparse.REMAINDER, help='after --: the command, as it would be typed in the repository root')
    one = sub.add_parser('wait')
    one.add_argument('name')
    one.add_argument('--every', type=float, default=5.0, help='seconds between looks')
    one.add_argument('--timeout', type=float, default=0.0, help='stop waiting after this many seconds (the run goes on)')
    one = sub.add_parser('log')
    one.add_argument('name', nargs='?')
    one.add_argument('--tail', type=int, default=40)
    one = sub.add_parser('stop')
    one.add_argument('name')
    one = sub.add_parser('fetch')
    one.add_argument('paths', nargs='+')
    one.add_argument('--to', default=FETCHED, help='the folder the paths are put under (default {})'.format(FETCHED))
    one.add_argument('--limit-mb', type=float, default=FETCH_LIMIT / 2**20)
    one = sub.add_parser('push')
    one.add_argument('paths', nargs='+')
    one.add_argument('--skip', action='append', default=[], help='a file or folder name, or the end of a path, to leave out')
    args = parser.parse_args(argv)
    try:
        configure_remote(args)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.action == 'sync':
        sync(args.all)
        return 0
    return dict(run=run, wait=wait, log=log, stop=stop, fetch=fetch, push=push)[args.action](args)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
