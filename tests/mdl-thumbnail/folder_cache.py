"""Measure current Windows folder covers and model thumbnails at the Explorer view size."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from collections import Counter
import json
import os
from pathlib import Path
import stat
import statistics
import time

from verify import (
    HR, P, cache_cls, cache_id, checked, co_create, co_init, co_uninit, create_item,
    item_id, method, release,
)

WTS_EXTRACT = 0
WTS_INCACHEONLY = 1
WTS_LOWQUALITY = 1


def measure(folder, size, fill):
    with os.scandir(folder) as entries:
        items = []
        for entry in entries:
            if entry.stat(follow_symlinks=False).st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                continue
            if entry.is_dir(follow_symlinks=False):
                items.append(("folder", entry.path))
            elif entry.name.lower().endswith(".mdl") and entry.is_file(follow_symlinks=False):
                items.append(("model", entry.path))
    items.sort(key=lambda item: (item[0], os.path.normcase(item[1])))
    cache = P()
    checked(co_init(None, 2))
    rows = []
    try:
        checked(co_create(C.byref(cache_cls), None, 1, C.byref(cache_id), C.byref(cache)))
        get = method(cache, 3, HR, P, W.UINT, W.DWORD, P, P, P)
        for kind, path in items:
            item, shared = P(), P()
            flags = W.DWORD()
            started = time.perf_counter()
            try:
                hr = create_item(path, None, C.byref(item_id), C.byref(item))
                if hr >= 0:
                    hr = get(cache, item, size, WTS_EXTRACT if fill else WTS_INCACHEONLY,
                             C.byref(shared), C.byref(flags), None)
            finally:
                release(shared)
                release(item)
            elapsed = (time.perf_counter() - started) * 1000
            rows.append(dict(kind=kind, name=os.path.basename(path), hresult=f"{hr & 0xffffffff:08x}",
                             low_quality=bool(flags.value & WTS_LOWQUALITY), ms=round(elapsed, 3)))
    finally:
        release(cache)
        co_uninit()
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--size", type=int, required=True, help="Actual view icon size in pixels")
    parser.add_argument("--fill", action="store_true", help="Allow the Shell to generate and cache missing images")
    args = parser.parse_args()
    if args.size < 1:
        parser.error("size must be positive")
    rows = measure(args.folder.resolve(), args.size, args.fill)
    summary = {}
    for kind in ("folder", "model"):
        group = [r for r in rows if r["kind"] == kind]
        if group:
            summary[kind] = dict(count=len(group), results=dict(Counter(r["hresult"] for r in group)),
                                 low_quality=sum(r["low_quality"] for r in group),
                                 seconds=round(sum(r["ms"] for r in group) / 1000, 3),
                                 median_ms=statistics.median(r["ms"] for r in group))
    report = dict(mode="normal" if args.fill else "cache-only", size=args.size, summary=summary, items=rows)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
