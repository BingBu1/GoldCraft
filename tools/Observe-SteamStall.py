"""Capture both ends of a Steam stall without suspending processes or sending input.

Run with explicit live game/Steam PIDs. Output must be a new directory under
build. CDB uses noninvasive, nonsuspending attachment; no ETW session is changed.
"""
import argparse
import concurrent.futures
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent


def utc():
    return datetime.now(timezone.utc).isoformat()


def workspace_path(path, directory):
    path = Path(os.path.abspath(path))
    if not path.is_relative_to(ROOT / directory):
        raise ValueError(f'Path must be below {directory}')
    for parent in (path, *path.parents):
        if parent == ROOT.parent:
            break
        if parent.is_symlink() or (parent.exists() and
                getattr(parent.stat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError('Reparse paths are not permitted')
    if not path.resolve().is_relative_to((ROOT / directory).resolve()):
        raise ValueError('Resolved path left the workspace scope')
    return path


def summarize(rows):
    if not rows:
        raise ValueError('No status samples')
    identity = lambda event: (event['stage'], event['completedSteadySeconds'],
                              event['elapsedMs'], event.get('key'))
    old = {identity(event) for event in rows[0]['longCalls']}
    seen, edges = {}, []
    previous = rows[0]
    for row in rows:
        if row['slowCalls'] < previous['slowCalls']:
            raise ValueError('Diagnostic counters reset; do not merge game sessions')
        for event in row['longCalls']:
            key = identity(event)
            if key not in old:
                seen.setdefault(key, event)
        if row['hostGameMenu'] != previous['hostGameMenu']:
            edges.append(dict(utc=row['utc'], menu=row['hostGameMenu'],
                              previousUtc=previous['utc']))
        previous = row
    gaps = [event['elapsedMs'] for event in seen.values() if event['stage'] == 'frameGap']
    delta = rows[-1]['slowCalls'] - rows[0]['slowCalls']
    return dict(samples=len(rows), firstUtc=rows[0]['utc'], lastUtc=rows[-1]['utc'],
                baselineHistoryExcluded=len(old), slowCallDelta=delta,
                uniqueNewHistoryEvents=len(seen), observedNewFrameGaps=len(gaps),
                maxObservedNewFrameGapMs=max(gaps, default=0),
                unobservedHistoryEvents=max(0, delta-len(seen)), menuEdges=edges,
                caveats=['Rolling history can omit events during long stalls.',
                         'Menu transitions are observed after frames resume, not physical key times.',
                         'Nonsuspending stacks can race and sampling adds overhead.'])


class Process:
    """Hold a query-only handle so PID reuse cannot silently change the target."""
    def __init__(self, pid, names):
        self.pid = pid
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                      wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)]*4
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.WaitForSingleObject.restype = wintypes.DWORD
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.api.OpenProcess(0x1000 | 0x100000, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            path, size = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
            if not self.api.QueryFullProcessImageNameW(self.handle, 0, path, ctypes.byref(size)):
                raise ctypes.WinError(ctypes.get_last_error())
            self.path = Path(path.value)
            if self.path.name.lower() not in names:
                raise ValueError('Unexpected target executable')
            times = [wintypes.FILETIME() for _ in range(4)]
            if not self.api.GetProcessTimes(self.handle, *[ctypes.byref(x) for x in times]):
                raise ctypes.WinError(ctypes.get_last_error())
            created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            self.created = created / 10_000_000 - 11644473600
            data = self.path.read_bytes()
            pe = struct.unpack_from('<I', data, 0x3c)[0]
            if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0':
                raise ValueError('Target is not a PE image')
            self.machine = struct.unpack_from('<H', data, pe+4)[0]
            if self.machine not in (0x14c, 0x8664):
                raise ValueError('Unsupported debugger architecture')
            self.sha256 = hashlib.sha256(data).hexdigest()
        except BaseException:
            self.close()
            raise

    def alive(self):
        value = self.api.WaitForSingleObject(self.handle, 0)
        if value == 0xffffffff:
            raise ctypes.WinError(ctypes.get_last_error())
        return value == 258

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None

    def record(self):
        return dict(pid=self.pid, image=str(self.path), createdUnix=self.created,
                    machine=self.machine, sha256=self.sha256)


def debugger_args(debugger, pid, symbols):
    return [str(debugger), '-pvr', '-p', str(pid), '-sins', '-y', str(symbols),
            '-c', '~* kn 35;lm;qd']


def stack(process, debugger, symbols, output):
    start = time.monotonic()
    result = dict(pid=process.pid, startedUtc=utc(), mode='noninvasive-nonsuspending')
    if not process.alive():
        return dict(result, skipped='target exited')
    try:
        with output.open('xb') as log:
            child = subprocess.run(debugger_args(debugger, process.pid, symbols),
                                   stdout=log, stderr=subprocess.STDOUT, timeout=10,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        result['exitCode'] = child.returncode
    except subprocess.TimeoutExpired:
        # subprocess.run kills only its nonsuspending CDB child on timeout.
        result['debuggerTimeout'] = True
    result.update(durationSeconds=time.monotonic()-start, file=output.name,
                  bytes=output.stat().st_size, sha256=hashlib.sha256(output.read_bytes()).hexdigest())
    return result


def read_status(path, created):
    stat = path.stat()
    if stat.st_mtime < created:
        raise ValueError('Status belongs to an older process')
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    history = value['nativeWorkHistory']
    row = {key: value.get(key) for key in ('hostGameMenu', 'windowFocused', 'minecraftForm',
                                          'hostUiWork', 'steamAsync')}
    row.update(utc=utc(), mtime=stat.st_mtime, age=max(0, time.time()-stat.st_mtime),
               slowCalls=history['slowCalls'], longCalls=history['longCalls'])
    return stat.st_mtime_ns, row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-pid', type=int, required=True)
    parser.add_argument('--steam-pid', type=int, required=True)
    parser.add_argument('--status', type=Path, default=ROOT/'sandbox/cs-client-b/logs/goldcraft-client-status.json')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--debuggers', type=Path, default=Path(os.environ.get('ProgramFiles(x86)', ''))/'Windows Kits/10/Debuggers')
    parser.add_argument('--symbols', type=Path, default=ROOT/'build/native-clang-x86-Release')
    parser.add_argument('--seconds', type=float, default=120)
    parser.add_argument('--max-stacks', type=int, default=6)
    parser.add_argument('--stall-ms', type=float, default=650)
    args = parser.parse_args()
    if os.name != 'nt' or args.game_pid <= 0 or args.steam_pid <= 0 or args.game_pid == args.steam_pid:
        parser.error('Distinct live Windows game and Steam process IDs are required')
    if not (1 <= args.seconds <= 300 and 1 <= args.max_stacks <= 12 and 200 <= args.stall_ms <= 10000):
        parser.error('Use 1–300 seconds, 1–12 stack pairs and 200–10000 ms threshold')
    output = workspace_path(args.output, 'build')
    status = workspace_path(args.status, 'sandbox')
    symbols = workspace_path(args.symbols, 'build')
    processes, debuggers = {}, {}
    try:
        processes['game'] = Process(args.game_pid, {'metahook.exe', 'hl.exe'})
        workspace_path(processes['game'].path, 'sandbox')
        processes['steam'] = Process(args.steam_pid, {'steam.exe'})
        for role, process in processes.items():
            if not process.alive():
                raise ValueError(f'{role} has exited')
            debugger = args.debuggers / ('x86' if process.machine == 0x14c else 'x64') / 'cdb.exe'
            if not debugger.is_file():
                raise ValueError(f'Debugger not found: {debugger}')
            debuggers[role] = debugger
        last_stamp, first = read_status(status, processes['game'].created)
        if first['age'] > 30:
            raise ValueError('No recent game diagnostics; refusing stale-file sampling')
        output.mkdir(parents=True, exist_ok=False)
        manifest = dict(startedUtc=utc(), targets={k: v.record() for k, v in processes.items()},
                        seconds=args.seconds, maxStackPairs=args.max_stacks, captures=[],
                        mode='read-only; no input, process restart, or ETW changes')
        rows, pending, index, next_stack = [first], [], 0, 0
        deadline = time.monotonic() + args.seconds
        try:
            print('READY: paired game/Steam passive sampling', flush=True)
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool, (output/'status.jsonl').open('x', encoding='utf-8') as log:
                log.write(json.dumps(first)+'\n')
                while time.monotonic() < deadline:
                    if not all(p.alive() for p in processes.values()):
                        manifest['stoppedReason'] = 'target exited'
                        break
                    try:
                        stamp, row = read_status(status, processes['game'].created)
                        if stamp != last_stamp:
                            rows.append(row)
                            log.write(json.dumps(row)+'\n')
                            log.flush()
                            last_stamp = stamp
                    except (OSError, json.JSONDecodeError):
                        time.sleep(0.06)
                        continue
                    if pending and all(f.done() for f in pending):
                        manifest['captures'].append([f.result() for f in pending])
                        pending = []
                    now = time.monotonic()
                    if row['age']*1000 >= args.stall_ms and not pending and index < args.max_stacks and now >= next_stack:
                        index += 1
                        pending = [pool.submit(stack, p, debuggers[role], symbols, output/f'{role}-{index:02d}.txt')
                                   for role, p in processes.items()]
                        next_stack = now + 2
                    time.sleep(0.06)
                if pending:
                    manifest['captures'].append([f.result() for f in pending])
        finally:
            manifest['finishedUtc'] = utc()
            (output/'capture.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
            (output/'summary.json').write_text(json.dumps(summarize(rows), indent=2)+'\n', encoding='utf-8')
        print(json.dumps(dict(samples=len(rows), pairs=len(manifest['captures']), output=str(output))))
    finally:
        for process in processes.values():
            process.close()


if __name__ == '__main__':
    main()
