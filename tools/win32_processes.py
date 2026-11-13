"""Read process identities without an optional process-monitoring dependency."""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os


class _Entry(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD),
                ("th32ProcessID", W.DWORD), ("th32DefaultHeapID", C.c_size_t),
                ("th32ModuleID", W.DWORD), ("cntThreads", W.DWORD),
                ("th32ParentProcessID", W.DWORD), ("pcPriClassBase", W.LONG),
                ("dwFlags", W.DWORD), ("szExeFile", W.WCHAR * 260)]


def processes(names: tuple[str, ...] = ()) -> list[dict]:
    """Snapshot matching PIDs, creation stamps, images and accumulated CPU time.

    An inaccessible image remains an explicit error; callers must not infer that
    a runtime directory is unused when a matching process cannot be identified.
    """
    if os.name != "nt":
        raise OSError("Windows process inspection requires Windows")
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
    kernel.Process32FirstW.argtypes = [W.HANDLE, C.POINTER(_Entry)]
    kernel.Process32FirstW.restype = W.BOOL
    kernel.Process32NextW.argtypes = [W.HANDLE, C.POINTER(_Entry)]
    kernel.Process32NextW.restype = W.BOOL
    kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenProcess.restype = W.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD,
                                                W.LPWSTR, C.POINTER(W.DWORD)]
    kernel.QueryFullProcessImageNameW.restype = W.BOOL
    kernel.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
    kernel.GetProcessTimes.restype = W.BOOL
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.CloseHandle.restype = W.BOOL
    wanted = {name.casefold() for name in names}
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    rows = []
    try:
        entry = _Entry()
        entry.dwSize = C.sizeof(entry)
        success = kernel.Process32FirstW(snapshot, C.byref(entry))
        if not success:
            raise C.WinError(C.get_last_error())
        while success:
            if not wanted or entry.szExeFile.casefold() in wanted:
                row = dict(pid=int(entry.th32ProcessID),
                           ppid=int(entry.th32ParentProcessID), name=entry.szExeFile,
                           threads=int(entry.cntThreads), image=None)
                handle = kernel.OpenProcess(0x1000, False, entry.th32ProcessID)
                if handle:
                    try:
                        image = C.create_unicode_buffer(32768)
                        size = W.DWORD(len(image))
                        if kernel.QueryFullProcessImageNameW(handle, 0, image, C.byref(size)):
                            row["image"] = image.value
                        else:
                            row["imageError"] = C.get_last_error()
                        times = [W.FILETIME() for _ in range(4)]
                        if kernel.GetProcessTimes(handle, *(C.byref(t) for t in times)):
                            ticks = [(int(t.dwHighDateTime) << 32) | int(t.dwLowDateTime)
                                     for t in times]
                            row["creationTime100ns"] = ticks[0]
                            row["cpuSeconds"] = (ticks[2] + ticks[3]) / 10_000_000
                        else:
                            row["timesError"] = C.get_last_error()
                    finally:
                        kernel.CloseHandle(handle)
                else:
                    row["openError"] = C.get_last_error()
                rows.append(row)
            success = kernel.Process32NextW(snapshot, C.byref(entry))
        error = C.get_last_error()
        if error != 18:  # ERROR_NO_MORE_FILES
            raise C.WinError(error)
    finally:
        kernel.CloseHandle(snapshot)
    return rows


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="*", help="Exact executable basenames")
    print(json.dumps(processes(tuple(parser.parse_args().names)), ensure_ascii=True))
