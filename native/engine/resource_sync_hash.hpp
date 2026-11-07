// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "goldcraft/resource_sync.hpp"
#include <cstddef>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace goldcraft::resource_sync {
// The current host target is Windows. Failure is explicit when its SHA
// provider is unavailable; a failed digest never becomes a trusted entry.
class Hasher {
public:
    Hasher() {
#ifdef _WIN32
        valid_ = BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &handle_, nullptr, 0,
                                 nullptr, 0, 0) >= 0;
#endif
    }
    ~Hasher() {
#ifdef _WIN32
        if (handle_) BCryptDestroyHash(handle_);
#endif
    }
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;
    bool Update(const void* bytes, std::size_t size) {
#ifdef _WIN32
        valid_ = valid_ && size <= 0xffffffffu &&
            BCryptHashData(handle_, static_cast<PUCHAR>(const_cast<void*>(bytes)),
                           static_cast<ULONG>(size), 0) >= 0;
#endif
        return valid_;
    }
    bool Finish(Digest& digest) {
#ifdef _WIN32
        const bool ok = valid_ && BCryptFinishHash(handle_, digest.data(),
                                                 static_cast<ULONG>(digest.size()), 0) >= 0;
#else
        const bool ok = false;
#endif
        valid_ = false;
        return ok;
    }
private:
#ifdef _WIN32
    BCRYPT_HASH_HANDLE handle_{};
#endif
    bool valid_{};
};
} // namespace goldcraft::resource_sync
