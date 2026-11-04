// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <initializer_list>

namespace csol::client {
// Records the first access violation raised inside the watched modules (by
// default hw.dll, client.dll and this plugin): a text log with the faulting
// module offset, registers, the EBP chain, return-address candidates from the
// raw stack and, for the engine's Q_strcasecmp (hw+1BB070), both arguments and
// the direct caller. A small minidump with stack-referenced memory is written
// beside it. The exception always continues to the existing handlers.
// CSOL_CRASH_RECORDER=0 disables it; only the two newest records are kept.
void InstallCrashRecorder();
void RemoveCrashRecorder();
// Test seam: watch explicit modules and write under directory.
void InstallCrashRecorderFor(std::initializer_list<HMODULE> modules, const wchar_t* directory);
}
