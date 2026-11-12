// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Windows.h>

#if !defined(_M_IX86)
#error The original precise-math register ABI is x86 only.
#endif

namespace csol::client
{
// The pinned client imports these exact CRT exports. Their double input and
// result use XMM0, unlike the x86 C double-return ABI. Resolve only at cold init.
class PreciseTrigonometry
{
    HMODULE module_{};
    FARPROC cosine_{}, sine_{};

    static __declspec(noinline) double Invoke(FARPROC target, double value)
    {
        double result;
        __asm
        {
            movsd xmm0, value
            call target
            movsd result, xmm0
        }
        return result;
    }

public:
    PreciseTrigonometry() = default;
    PreciseTrigonometry(const PreciseTrigonometry&) = delete;
    PreciseTrigonometry& operator=(const PreciseTrigonometry&) = delete;

    bool Initialize()
    {
        if (module_) return true;
        auto module = LoadLibraryExW(L"api-ms-win-crt-math-l1-1-0.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return false;
        const auto cosine = GetProcAddress(module, "_libm_sse2_cos_precise");
        const auto sine = GetProcAddress(module, "_libm_sse2_sin_precise");
        if (!cosine || !sine) { FreeLibrary(module); return false; }
        module_ = module;
        cosine_ = cosine;
        sine_ = sine;
        return true;
    }
    void Shutdown()
    {
        if (module_) FreeLibrary(module_);
        module_ = nullptr;
        cosine_ = sine_ = nullptr;
    }
    [[nodiscard]] HMODULE Module() const { return module_; }
    double cosine(double value) const { return Invoke(cosine_, value); }
    double sine(double value) const { return Invoke(sine_, value); }
};
}
