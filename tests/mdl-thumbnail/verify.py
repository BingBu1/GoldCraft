"""Exercise an installed or private GoldSrc thumbnail provider using actual Windows bitmaps."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import statistics
import time
import uuid

P = C.c_void_p
HR = C.c_long
ole = C.WinDLL("ole32")
gdi = C.WinDLL("gdi32")
shell = C.WinDLL("shell32")


class Guid(C.Structure):
    _fields_ = [("data", C.c_ubyte * 16)]

    @classmethod
    def parse(cls, text):
        return cls.from_buffer_copy(uuid.UUID(text).bytes_le)


class Bitmap(C.Structure):
    _fields_ = [("kind", W.LONG), ("width", W.LONG), ("height", W.LONG),
                ("stride", W.LONG), ("planes", W.WORD), ("depth", W.WORD), ("bits", P)]


class Info(C.Structure):
    _fields_ = [("size", W.DWORD), ("width", W.LONG), ("height", W.LONG),
                ("planes", W.WORD), ("depth", W.WORD), ("rest", W.DWORD * 6)]


def api(dll, name, result, *args):
    fn = getattr(dll, name)
    fn.restype, fn.argtypes = result, args
    return fn


def method(obj, index, result, *args):
    table = C.cast(obj, C.POINTER(C.POINTER(P))).contents
    return C.WINFUNCTYPE(result, P, *args)(table[index])


def release(obj):
    if obj.value:
        method(obj, 2, W.ULONG)(obj)


def checked(result):
    if result < 0:
        raise OSError(f"HRESULT 0x{result & 0xffffffff:08x}")


cls = Guid.parse("507aeaaa-1c07-42b0-bc09-213f4ee640b8")
factory_id = Guid.parse("00000001-0000-0000-c000-000000000046")
file_id = Guid.parse("b7d14566-0509-4cce-a71f-0a554233bd9b")
thumb_id = Guid.parse("e357fccd-a995-4576-b01f-234630154e96")
cache_cls = Guid.parse("50ef4544-ac9f-4a8e-b21b-8a26180db13f")
cache_id = Guid.parse("f676c15d-596a-4ce2-8234-33996f445db1")
item_id = Guid.parse("43826d1e-e718-42ee-bc55-a1e261c37bfe")
co_init = api(ole, "CoInitializeEx", HR, P, W.DWORD)
co_uninit = api(ole, "CoUninitialize", None)
co_create = api(ole, "CoCreateInstance", HR, P, P, W.DWORD, P, P)
create_item = api(shell, "SHCreateItemFromParsingName", HR, W.LPCWSTR, P, P, P)
get_obj = api(gdi, "GetObjectW", C.c_int, P, C.c_int, P)
del_obj = api(gdi, "DeleteObject", W.BOOL, P)
get_dib = api(gdi, "GetDIBits", C.c_int, P, P, W.UINT, W.UINT, P, P, W.UINT)
get_dc = api(gdi, "CreateCompatibleDC", P, P)
del_dc = api(gdi, "DeleteDC", W.BOOL, P)


def pixels(handle):
    bm = Bitmap()
    if get_obj(handle, C.sizeof(bm), C.byref(bm)) != C.sizeof(bm):
        raise RuntimeError("Invalid HBITMAP")
    info = Info(C.sizeof(Info), bm.width, -bm.height, 1, 32)
    data = C.create_string_buffer(bm.width * bm.height * 4)
    dc = get_dc(None)
    try:
        if get_dib(dc, handle, 0, bm.height, data, C.byref(info), 0) != bm.height:
            raise RuntimeError("Could not read DIB")
    finally:
        del_dc(dc)
    return [bm.width, bm.height], hashlib.sha256(data.raw).hexdigest()


def extract(factory, path, size, use_shell, shell_inproc=False):
    init, provider, bitmap, item, shared = (P() for _ in range(5))
    alpha = W.DWORD()
    try:
        if use_shell:
            checked(create_item(str(path), None, C.byref(item_id), C.byref(item)))
            # Force extraction without populating the user's normal thumbnail cache.
            flags = 0x4 | 0x20 | (0x100 if shell_inproc else 0)
            checked(method(factory, 3, HR, P, W.UINT, W.DWORD, P, P, P)(
                factory, item, size, flags, C.byref(shared), None, None))
            checked(method(shared, 3, HR, P)(shared, C.byref(bitmap)))
        else:
            checked(method(factory, 3, HR, P, P, P)(factory, None, C.byref(file_id), C.byref(init)))
            checked(method(init, 3, HR, W.LPCWSTR, W.DWORD)(init, str(path), 0))
            checked(method(init, 0, HR, P, P)(init, C.byref(thumb_id), C.byref(provider)))
            checked(method(provider, 3, HR, W.UINT, P, P)(provider, size, C.byref(bitmap), C.byref(alpha)))
        dimensions, digest = pixels(bitmap)
        return {"size": dimensions, "sha256": digest, "alpha": None if use_shell else alpha.value}
    finally:
        if bitmap.value and not use_shell:
            del_obj(bitmap)
        for obj in (shared, item, provider, init):
            release(obj)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("folder", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--dll", type=Path, help="Direct provider DLL (default: registered Windows thumbnail cache)")
    ap.add_argument("--sizes", type=int, nargs="+", default=[256])
    ap.add_argument("--workers", type=int, default=1)
    ap.add_argument("--limit", type=int)
    ap.add_argument("--shell-inproc", action="store_true", help="Force in-process extraction through IThumbnailCache")
    ap.add_argument("--compare", type=Path)
    args = ap.parse_args()
    files = sorted(args.folder.resolve().glob("*.mdl"))[:args.limit]
    if not files or args.workers < 1:
        ap.error("A nonempty directory and at least one worker are required")
    tasks = [(p, s) for p in files for s in args.sizes]
    dll = C.WinDLL(str(args.dll.resolve())) if args.dll else None
    class_object = api(dll, "DllGetClassObject", HR, P, P, P) if dll else None

    def run(chunk):
        checked(co_init(None, 2))
        factory = P()
        results = {}
        try:
            if dll:
                checked(class_object(C.byref(cls), C.byref(factory_id), C.byref(factory)))
            else:
                checked(co_create(C.byref(cache_cls), None, 1, C.byref(cache_id), C.byref(factory)))
            for path, size in chunk:
                start = time.perf_counter()
                try:
                    row = extract(factory, path, size, not dll, args.shell_inproc)
                except OSError as err:
                    row = {"error": str(err)}
                row["ms"] = round(1000 * (time.perf_counter() - start), 3)
                results[f"{path.name}/{size}"] = row
        finally:
            release(factory)
            co_uninit()
        return results

    start = time.perf_counter()
    chunks = [tasks[i::args.workers] for i in range(args.workers)]
    with ThreadPoolExecutor(args.workers) as pool:
        results = {k: v for batch in pool.map(run, chunks) for k, v in batch.items()}
    elapsed = time.perf_counter() - start
    times = [r["ms"] for r in results.values()]
    stats = {"count": len(results), "rendered": sum("sha256" in v for v in results.values()),
             "seconds": round(elapsed, 3), "medianMs": statistics.median(times),
             "p95Ms": sorted(times)[max(0, (len(times)*95 + 99)//100 - 1)]}
    diffs = []
    if args.compare:
        old = json.loads(args.compare.read_bytes())["images"]
        if set(old) != set(results):
            raise ValueError("Source folder or size list changed")
        diffs = [k for k, v in results.items() if any(v.get(f) != old[k].get(f)
                 for f in ("error", "size", "sha256", "alpha"))]
        stats["mismatches"] = len(diffs)
    args.output.write_text(json.dumps({"stats": stats, "images": results, "mismatches": diffs},
                                      indent=2), encoding="utf-8")
    print(json.dumps(stats), flush=True)
    if diffs:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
