"""Run a command and sample the main thread of the Unreal editor it starts, to see where its time goes.

    py -3.11 tools/sample_stacks.py [--every 1] [--out FILE] -- <command...>

Every --every seconds the editor's main thread is stopped for a moment and its stack is written as one
line of FILE (stacks.jsonl next to the editor's project when left out). At the end the functions of
Kadan seen most often are printed: a function in 30% of the samples took about 30% of the run. Windows
only; the symbols come from the PDBs beside the editor's and the plugins' DLLs.
"""
import argparse
from collections import Counter
import ctypes as C
import ctypes.wintypes as W
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

k = C.WinDLL('kernel32', use_last_error=True)
d = C.WinDLL('dbghelp', use_last_error=True)


def api(lib, name, restype, args):
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = args
    return f


OpenProcess = api(k, 'OpenProcess', W.HANDLE, [W.DWORD, W.BOOL, W.DWORD])
OpenThread = api(k, 'OpenThread', W.HANDLE, [W.DWORD, W.BOOL, W.DWORD])
CloseHandle = api(k, 'CloseHandle', W.BOOL, [W.HANDLE])
SuspendThread = api(k, 'SuspendThread', W.DWORD, [W.HANDLE])
ResumeThread = api(k, 'ResumeThread', W.DWORD, [W.HANDLE])
GetThreadContext = api(k, 'GetThreadContext', W.BOOL, [W.HANDLE, C.c_void_p])
GetThreadTimes = api(k, 'GetThreadTimes', W.BOOL, [W.HANDLE, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p])
GetProcessTimes = api(k, 'GetProcessTimes', W.BOOL, [W.HANDLE, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p])
GetSystemTimes = api(k, 'GetSystemTimes', W.BOOL, [C.c_void_p, C.c_void_p, C.c_void_p])
Snapshot = api(k, 'CreateToolhelp32Snapshot', W.HANDLE, [W.DWORD, W.DWORD])


class IoCounters(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in ('ReadOps', 'WriteOps', 'OtherOps', 'ReadBytes', 'WriteBytes', 'OtherBytes')]


GetProcessIoCounters = api(k, 'GetProcessIoCounters', W.BOOL, [W.HANDLE, C.POINTER(IoCounters)])


class ProcessEntry(C.Structure):
    _fields_ = [('size', W.DWORD), ('usage', W.DWORD), ('pid', W.DWORD), ('heap', C.c_size_t), ('module', W.DWORD),
                ('threads', W.DWORD), ('parent', W.DWORD), ('base', W.LONG), ('flags', W.DWORD), ('exe', W.WCHAR * 260)]


class ThreadEntry(C.Structure):
    _fields_ = [('size', W.DWORD), ('use', W.DWORD), ('tid', W.DWORD), ('pid', W.DWORD), ('base', W.LONG), ('delta', W.LONG), ('flags', W.DWORD)]


P32First = api(k, 'Process32FirstW', W.BOOL, [W.HANDLE, C.POINTER(ProcessEntry)])
P32Next = api(k, 'Process32NextW', W.BOOL, [W.HANDLE, C.POINTER(ProcessEntry)])
T32First = api(k, 'Thread32First', W.BOOL, [W.HANDLE, C.POINTER(ThreadEntry)])
T32Next = api(k, 'Thread32Next', W.BOOL, [W.HANDLE, C.POINTER(ThreadEntry)])


def processes():
    snap = Snapshot(2, 0)
    entry = ProcessEntry()
    entry.size = C.sizeof(entry)
    rows = []
    ok = P32First(snap, C.byref(entry))
    while ok:
        rows.append((entry.pid, entry.parent, entry.exe))
        ok = P32Next(snap, C.byref(entry))
    CloseHandle(snap)
    return rows


def descendants(root, rows):
    children = {}
    for pid, parent, exe in rows:
        children.setdefault(parent, []).append((pid, exe))
    out, todo = [], [root]
    while todo:
        for pid, exe in children.get(todo.pop(), []):
            if pid not in [p for p, _ in out]:
                out.append((pid, exe))
                todo.append(pid)
    return out


def ptimes(pid):
    h = OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        t = [C.c_uint64() for _ in range(4)]
        io = IoCounters()
        if not GetProcessTimes(h, *[C.byref(x) for x in t]):
            return None
        GetProcessIoCounters(h, C.byref(io))
        return (t[2].value + t[3].value) / 1e7, io.ReadBytes, io.WriteBytes
    finally:
        CloseHandle(h)


def threads(pid):
    snap = Snapshot(4, 0)
    entry = ThreadEntry()
    entry.size = C.sizeof(entry)
    rows = {}
    ok = T32First(snap, C.byref(entry))
    while ok:
        if entry.pid == pid:
            h = OpenThread(0x0800, False, entry.tid)  # THREAD_QUERY_LIMITED_INFORMATION
            if h:
                t = [C.c_uint64() for _ in range(4)]
                if GetThreadTimes(h, *[C.byref(x) for x in t]):
                    rows[entry.tid] = (t[0].value, (t[2].value + t[3].value) / 1e7)
                CloseHandle(h)
        ok = T32Next(snap, C.byref(entry))
    CloseHandle(snap)
    return rows


class Stacks:
    def __init__(self, pid, paths):
        self.pid = pid
        self.h = OpenProcess(0x0410, False, pid)
        api(d, 'SymSetOptions', W.DWORD, [W.DWORD])(0x4 | 0x2 | 0x10 | 0x200 | 0x80000 | 0x1000)
        self.ok = bool(self.h) and api(d, 'SymInitializeW', W.BOOL, [W.HANDLE, W.LPCWSTR, W.BOOL])(self.h, ';'.join(paths), True)
        self.walk = api(d, 'StackWalk64', W.BOOL, [W.DWORD, W.HANDLE, W.HANDLE, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p])
        self.base = api(d, 'SymGetModuleBase64', C.c_uint64, [W.HANDLE, C.c_uint64])
        self.name = api(d, 'SymFromAddr', W.BOOL, [W.HANDLE, C.c_uint64, C.POINTER(C.c_uint64), C.c_void_p])
        self.refresh = api(d, 'SymRefreshModuleList', W.BOOL, [W.HANDLE])
        self.line = api(d, 'SymGetLineFromAddrW64', W.BOOL, [W.HANDLE, C.c_uint64, C.POINTER(W.DWORD), C.c_void_p])
        self.file = api(k, 'K32GetModuleFileNameExW', W.DWORD, [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD])
        self.labels = {}

    def sample(self, tid, depth=40):
        if not self.ok:
            return []
        self.refresh(self.h)
        ht = OpenThread(0x4a, False, tid)
        if not ht:
            return []
        raw = C.create_string_buffer(1250)
        at = (C.addressof(raw) + 15) & ~15
        C.memset(at, 0, 1232)
        C.c_uint32.from_address(at + 0x30).value = 0x10000b
        pcs = []
        held = SuspendThread(ht) != 0xffffffff
        try:
            if held and GetThreadContext(ht, at):
                sf = C.create_string_buffer(1024)
                rip, rsp, rbp = (C.c_uint64.from_address(at + o).value for o in (0xf8, 0x98, 0xa0))
                for offset, value in [(0, rip), (32, rbp), (48, rsp)]:
                    struct.pack_into('<Q', sf, offset, value)
                    struct.pack_into('<I', sf, offset + 12, 3)
                pcs.append(rip)
                for i in range(depth):
                    if not self.walk(0x8664, self.h, ht, sf, at, None, C.cast(d.SymFunctionTableAccess64, C.c_void_p),
                                     C.cast(d.SymGetModuleBase64, C.c_void_p), None):
                        break
                    pc = struct.unpack_from('<Q', sf, 0)[0]
                    if not pc:
                        break
                    if pc == pcs[-1]:
                        if i == 0:
                            continue
                        break
                    pcs.append(pc)
        finally:
            if held:
                ResumeThread(ht)
            CloseHandle(ht)
        return [self.label(pc) for pc in pcs]

    def label(self, pc):
        if pc in self.labels:
            return self.labels[pc]
        symbol = C.create_string_buffer(1200)
        struct.pack_into('<I', symbol, 0, 88)
        struct.pack_into('<I', symbol, 80, 1024)
        disp = C.c_uint64()
        base = self.base(self.h, pc)
        path = C.create_unicode_buffer(1024)
        self.file(self.h, base, path, len(path))
        name = symbol.raw[84:].split(b'\0', 1)[0].decode(errors='replace') if self.name(self.h, pc, C.byref(disp), symbol) else hex(pc - base)
        text = path.value.rsplit('\\', 1)[-1].replace('UE4Editor-', '').replace('.dll', '') + '!' + name
        if text.startswith(OWN):
            # Inlined and file-local functions take the name of a nearby public one in an optimized build; the
            # source line says where the time really is.
            line, shift = C.create_string_buffer(40), W.DWORD()
            struct.pack_into('<I', line, 0, 40)
            if self.line(self.h, pc, C.byref(shift), line):
                number, at = struct.unpack_from('<I', line, 16)[0], struct.unpack_from('<Q', line, 24)[0]
                if at:
                    text += ' @' + C.wstring_at(at).rsplit('\\', 1)[-1] + ':' + str(number)
        self.labels[pc] = text
        return text


OWN = ('Kadan!', 'P4BlueprintImport!', 'P4MapKitTests!')


def summary(rows, top=30):
    """Inclusive counts: each function once per sample it is anywhere in."""
    seen, leaves = Counter(), Counter()
    for frames in rows:
        for frame in dict.fromkeys(frames):
            if frame.startswith(OWN):
                seen[frame] += 1
        if frames:
            leaves[frames[0]] += 1
    lines = ['{} samples'.format(len(rows))]
    lines += ['{:4.0f}% {}'.format(100.0 * n / max(1, len(rows)), frame[:160]) for frame, n in seen.most_common(top)]
    lines.append('where the thread was:')
    lines += ['{:4.0f}% {}'.format(100.0 * n / max(1, len(rows)), frame[:160]) for frame, n in leaves.most_common(8)]
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--every', type=float, default=1.0, help='seconds between samples')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--engine', default='C:/UE_4.26')
    parser.add_argument('--project', type=Path, help='the .uproject the editor opens, for the symbols of its plugins, when the command does not name it')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('give the command after --')
    child = subprocess.Popen(command)
    editor = stacks = main_tid = None
    rows, last = [], 0.0
    out = args.out
    stream = None
    try:
        while child.poll() is None:
            time.sleep(min(0.25, args.every))
            if editor is None:
                for pid, exe in descendants(child.pid, processes()):
                    if exe.lower() in ('ue4editor.exe', 'ue4editor-cmd.exe'):
                        editor = pid
                        break
                if editor is None:
                    continue
                time.sleep(1.0)
                project = args.project or next((Path(part) for part in command if str(part).endswith('.uproject')), None)
                paths = [args.engine + '/Engine/Binaries/Win64']
                if project:
                    paths += [str(p) for p in project.parent.glob('Plugins/*/Binaries/Win64')]
                stacks = Stacks(editor, paths)
                current = threads(editor)
                main_tid = min(current, key=lambda tid: current[tid][0]) if current else None
                out = out or (project.parent / 'Saved/stacks.jsonl' if project else Path('stacks.jsonl'))
                out.parent.mkdir(parents=True, exist_ok=True)
                stream = out.open('w', encoding='utf-8')
            now = time.time()
            if main_tid and now - last >= args.every:
                last = now
                frames = stacks.sample(main_tid)
                if frames:
                    rows.append(frames)
                    stream.write(json.dumps(frames) + '\n')
    finally:
        if stream:
            stream.close()
    for line in summary(rows):
        print(line)
    if out:
        print('stacks:', out)
    return child.returncode


if __name__ == '__main__':
    sys.exit(main())
