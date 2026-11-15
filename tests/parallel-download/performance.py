#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare real transports using process CPU/I/O, thread samples and HTTP concurrency."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import threading
import time

spec = importlib.util.spec_from_file_location('network_cases', Path(__file__).with_name('run.py'))
network = importlib.util.module_from_spec(spec)
spec.loader.exec_module(network)
kernel = c.WinDLL('kernel32', use_last_error=True)


class ThreadEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('tid', w.DWORD), ('pid', w.DWORD),
                ('priority', w.LONG), ('delta', w.LONG), ('flags', w.DWORD)]


class IOCounters(c.Structure):
    _fields_ = [(name, c.c_ulonglong) for name in
                ('readOperations', 'writeOperations', 'otherOperations', 'readBytes', 'writeBytes', 'otherBytes')]


for name, args, result in (
    ('CreateToolhelp32Snapshot', [w.DWORD, w.DWORD], w.HANDLE),
    ('Thread32First', [w.HANDLE, c.POINTER(ThreadEntry)], w.BOOL),
    ('Thread32Next', [w.HANDLE, c.POINTER(ThreadEntry)], w.BOOL),
    ('OpenThread', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
    ('GetThreadDescription', [w.HANDLE, c.POINTER(c.c_void_p)], w.LONG),
    ('GetThreadTimes', [w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)], w.BOOL),
    ('GetProcessTimes', [w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)], w.BOOL),
    ('GetProcessIoCounters', [w.HANDLE, c.POINTER(IOCounters)], w.BOOL),
    ('LocalFree', [c.c_void_p], c.c_void_p), ('CloseHandle', [w.HANDLE], w.BOOL)):
    function = getattr(kernel, name); function.argtypes = args; function.restype = result


def cpu(handle, thread=False):
    values = [w.FILETIME() for _ in range(4)]
    call = kernel.GetThreadTimes if thread else kernel.GetProcessTimes
    if not call(handle, *(c.byref(value) for value in values)):
        return None
    return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in values[2:]) / 10000


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))


def run(exe, output, burst=False):
    # Give every server its own counter class, including handlers still
    # unwinding after a deliberate /hang response in the preceding run.
    spec = importlib.util.spec_from_file_location('network_cases_' + output.name, Path(__file__).with_name('run.py'))
    network = importlib.util.module_from_spec(spec); spec.loader.exec_module(network)
    network.Handler.active = network.Handler.peak = 0
    clients = set()
    class PersistentHandler(network.Handler):
        protocol_version = 'HTTP/1.1'
        def handle(self):
            try: super().handle()
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError): pass
        def do_GET(self):
            clients.add(self.client_address)
            super().do_GET()
    class ConcurrentServer(network.http.server.ThreadingHTTPServer):
        request_queue_size = 128
    server = ConcurrentServer(('127.0.0.1', 0), PersistentHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    output.mkdir(parents=True)
    command = [str(exe.resolve()), f'http://127.0.0.1:{server.server_port}/', str(output / 'temporary')]
    if burst: command.append('burst')
    handles, threads, samples = {}, {}, []
    try:
        start = time.perf_counter()
        with (output / 'process.log').open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            while process.poll() is None:
                snapshot = kernel.CreateToolhelp32Snapshot(4, 0)
                entry = ThreadEntry(); entry.size = c.sizeof(entry)
                current = []
                found = kernel.Thread32First(snapshot, c.byref(entry))
                while found:
                    if entry.pid == process.pid:
                        current.append(entry.tid)
                        if entry.tid not in handles:
                            handle = kernel.OpenThread(0x0800, False, entry.tid)
                            if handle:
                                handles[entry.tid] = handle
                                description = c.c_void_p()
                                result = kernel.GetThreadDescription(handle, c.byref(description))
                                text = c.wstring_at(description) if result >= 0 and description.value else ''
                                if description.value: kernel.LocalFree(description)
                                threads[entry.tid] = dict(name=text, firstMs=(time.perf_counter()-start)*1000)
                    found = kernel.Thread32Next(snapshot, c.byref(entry))
                kernel.CloseHandle(snapshot)
                samples.append(dict(ms=(time.perf_counter()-start)*1000, tids=current,
                                    cpuMs=cpu(int(process._handle))))
                if time.perf_counter() - start > 20:
                    process.kill(); raise RuntimeError('Transport performance process timed out')
                time.sleep(.025)
            elapsed = (time.perf_counter() - start) * 1000
            counters = IOCounters()
            if not kernel.GetProcessIoCounters(int(process._handle), c.byref(counters)):
                raise c.WinError(c.get_last_error())
            for tid, handle in handles.items(): threads[tid]['cpuMs'] = cpu(handle, True)
            receipt = dict(exit=process.returncode, elapsedMs=elapsed, cpuMs=cpu(int(process._handle)),
                io={name: getattr(counters, name) for name, _ in counters._fields_},
                peakThreads=max((len(item['tids']) for item in samples), default=0),
                peakRequests=network.Handler.peak, threads=threads, samples=samples,
                connections=len(clients), binary=identity(exe), burst=burst)
            (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
            return receipt
    finally:
        for handle in handles.values(): kernel.CloseHandle(handle)
        server.shutdown(); server.server_close(); thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 5: raise ValueError('rounds must be 1..5')
    results = []
    for round_ in range(args.rounds):
        order = [('baseline', args.baseline), ('candidate', args.candidate)]
        if round_ % 2: order.reverse()
        for label, exe in order:
            result = run(exe, args.output / f'{round_}-{label}')
            results.append(dict(label=label, round=round_, **result))
            print(json.dumps({k: result[k] for k in ('exit', 'elapsedMs', 'cpuMs', 'peakThreads', 'peakRequests')}), flush=True)
    burst = run(args.candidate, args.output / 'burst-16', True)
    owned = [item for item in burst['threads'].values() if item['name'] == 'GoldCraft download IO']
    success = all(item['exit'] == 0 and item['peakRequests'] >= 4 and item['cpuMs'] is not None for item in results)
    success = success and burst['exit'] == 0 and burst['peakRequests'] >= 16 and len(owned) == 1
    receipt = dict(success=success, comparisons=results, burst=burst,
        boundary='Independent loopback transport workload, process/thread CPU and I/O counters; no gameplay/FPS acceptance.')
    (args.output / 'result.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps(dict(success=success, burstPeakRequests=burst['peakRequests'], ioDispatchers=len(owned))))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
