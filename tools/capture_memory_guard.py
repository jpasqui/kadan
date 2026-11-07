"""Read-only host memory headroom checks for disposable capture emulators."""
import ctypes
from ctypes import wintypes
import time
import json
import threading

GIB = 1024**3
MINIMUM_COMMIT = 6*GIB
MINIMUM_PHYSICAL = 4*GIB


class MemoryStatus(ctypes.Structure):
    _fields_ = [('length',wintypes.DWORD),('load',wintypes.DWORD)] + [
        (name,ctypes.c_ulonglong) for name in (
            'total_physical','available_physical','total_pagefile','available_pagefile',
            'total_virtual','available_virtual','available_extended_virtual')]


def memory_headroom():
    status = MemoryStatus()
    status.length = ctypes.sizeof(status)
    api = ctypes.WinDLL('kernel32',use_last_error=True)
    api.GlobalMemoryStatusEx.argtypes = [ctypes.POINTER(MemoryStatus)]
    api.GlobalMemoryStatusEx.restype = wintypes.BOOL
    if not api.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise ctypes.WinError(ctypes.get_last_error())
    return dict(available_commit_bytes=status.available_pagefile,
                available_physical_bytes=status.available_physical,
                total_commit_limit_bytes=status.total_pagefile)


def capture_memory_ready(state):
    return state['available_commit_bytes'] >= MINIMUM_COMMIT and state['available_physical_bytes'] >= MINIMUM_PHYSICAL


def wait_for_capture_memory(report, *, timeout=43200):
    # Conservative launch headroom following an observed allocation failure.
    # This is not a promise of capacity throughout the run: other agents can
    # allocate concurrently. Never stop them or change system paging settings.
    minimum_commit, minimum_physical = MINIMUM_COMMIT, MINIMUM_PHYSICAL
    deadline = time.monotonic()+timeout
    while True:
        state = memory_headroom()
        state.update(minimum_commit_bytes=minimum_commit,minimum_physical_bytes=minimum_physical)
        if capture_memory_ready(state):
            report('memory_available',state)
            return
        report('waiting_for_memory',state)
        if time.monotonic() >= deadline:
            raise TimeoutError('Capture memory headroom unavailable; checkpoints preserved')
        time.sleep(20)


class OwnedMemoryMonitor:
    """Monitor one retained Popen handle; never select processes by name/PID.

    A 6 GiB launch gate permits a measured retry. By default preserve 2 GiB of
    system commit. A scoped 1 GiB retry samples every 250 ms; the launch gate is
    unchanged. Physical pressure alone may be managed by Windows.
    This is a best-effort reserve, not protection against sudden allocations.
    """
    def __init__(self, proc, output, *, reserve_gib=2):
        if reserve_gib not in (1, 2):
            raise ValueError('Commit reserve must be 1 or 2 GiB')
        self.proc, self.output = proc, output
        self.reserve_gib = reserve_gib
        self.interval = 0.25 if reserve_gib == 1 else 2
        self.stopped = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def sample(self):
        state = memory_headroom()
        state.update(at=time.time(), pid=self.proc.pid, reserve_bytes=self.reserve_gib*GIB)
        with (self.output / 'memory.jsonl').open('a', encoding='utf-8') as log:
            log.write(json.dumps(state)+'\n')
        if state['available_commit_bytes'] < self.reserve_gib*GIB and self.proc.poll() is None:
            self.error = f'Owned capture stopped to preserve {self.reserve_gib} GiB system commit reserve'
            self.proc.terminate()
            return False
        return True

    def _run(self):
        try:
            while not self.stopped.is_set() and self.proc.poll() is None:
                if not self.sample(): return
                self.stopped.wait(self.interval)
        except Exception as exc:
            self.error = 'Capture memory monitor failed: '+repr(exc)
            if self.proc.poll() is None: self.proc.terminate()

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=10)
