// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

void CBaseAnimating::ResetSequenceInfo()
{
    g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.callChain(
        &CBaseAnimating::ResetSequenceInfo_OrigFunc, this);
}
int SENTENCEG_Lookup(const char*, char (&)[32])
{ throw std::runtime_error("unexpected sentence lookup"); }
void Sys_Error(const char*, ...)
{ throw std::runtime_error("unexpected SDK fatal error"); }
