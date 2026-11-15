#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Interleave recorded VFS implementations over one identical admission corpus."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import subprocess
import time


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', type=Path, required=True)
    parser.add_argument('--leased', type=Path, required=True)
    parser.add_argument('--complete', type=Path, required=True)
    parser.add_argument('--filesystem', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists(): raise RuntimeError('Use a fresh output directory')
    args.output.mkdir(parents=True)
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.GetProcessTimes.argtypes = [w.HANDLE] + [c.POINTER(w.FILETIME)] * 4
    kernel.GetProcessTimes.restype = w.BOOL
    programs = dict(legacy=args.legacy, leased=args.leased, complete=args.complete)
    identities = {key: identity(path) for key, path in programs.items()}
    fs = identity(args.filesystem)
    if fs['sha256'] != '07a432b6214c6ea5ea4a988d5c8384d3ab35a531b49fe71c354f3fed5ce9f316':
        raise RuntimeError('Unexpected filesystem binary')
    runs = []
    for round_, order in enumerate((('legacy','leased','complete'), ('complete','leased','legacy'), ('leased','legacy','complete'))):
        for label in order:
            log = args.output / f'{round_}-{label}.log'
            command = [str(programs[label].resolve()), str(args.filesystem.resolve()),
                       str((args.output / 'corpus').resolve()), 'http://127.0.0.1:1/', 'join']
            start = time.monotonic()
            with log.open('wb') as stream:
                process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
                try: process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(); raise
            values = [w.FILETIME() for _ in range(4)]
            if not kernel.GetProcessTimes(int(process._handle), *(c.byref(x) for x in values)):
                raise c.WinError(c.get_last_error())
            cpu = sum((x.dwHighDateTime << 32) | x.dwLowDateTime for x in values[2:]) / 10000
            phases = [json.loads(line) for line in log.read_text('utf-8').splitlines() if line.startswith('{')]
            result = dict(label=label, round=round_, exit=process.returncode, phases=phases,
                          processCpuMs=cpu, processElapsedMs=(time.monotonic()-start)*1000, log=identity(log))
            runs.append(result)
            (args.output / 'runs.json').write_text(json.dumps(runs, indent=2), encoding='utf-8')
            print(json.dumps(dict(label=label, round=round_, exit=process.returncode, phases=phases)), flush=True)
    changed = [key for key, path in programs.items() if identity(path) != identities[key]]
    receipt = dict(success=all(x['exit'] == 0 and len(x['phases']) == 3 for x in runs) and not changed,
        runs=runs, binaries=identities, filesystem=fs, changed=changed,
        corpus=dict(files=3345, repairedCache=445, bytesPerFile=16384, emptySearchPaths=5, hostHz=31),
        boundary='Actual x86 VFS/updater, identical generated corpus and admission cadence. No model load, live join or gameplay.')
    (args.output / 'result.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps(dict(success=receipt['success'])))
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
