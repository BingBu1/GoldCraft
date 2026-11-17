// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#endif

namespace goldcraft::filesystem {
// The caller has already selected and opened its VFS winner. Query that
// stream instead of opening its path a second time for size and timestamps.
// Unknown backends and non-regular streams keep their existing path fallback.
inline bool OpenFileMetadata(FILE* stream,struct _stat& output) noexcept
{
#ifdef _WIN32
    if(!stream)return false;
    const auto descriptor=_fileno(stream);
    struct _stat value{};
    if(descriptor<0 || _fstat(descriptor,&value) || (value.st_mode&_S_IFMT)!=_S_IFREG)return false;
    output=value;
    return true;
#else
    return false;
#endif
}
}
