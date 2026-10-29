// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "damage_numbers.h"

namespace
{
// Kept outside map-owned objects: engine message hooks may retire the owner.
bool writingDamageNumber{};
struct WriteScope
{
    WriteScope() { writingDamageNumber = true; }
    ~WriteScope() { writingDamageNumber = false; }
};
}

bool CSOLWriteDamageNumber(int messageId, edict_s* recipient, CSOLDamageNumber message)
{
    if (writingDamageNumber || messageId < 64 || messageId > 255 ||
        !recipient || recipient->free || !(recipient->v.flags & FL_CLIENT) ||
        message.target <= 0 || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnWriteShort || !g_engfuncs.pfnWriteLong)
        return false;

    // All values are snapshots before the first host callback. Never dereference
    // a target, a damage context or the owner after MESSAGE_BEGIN.
    const WriteScope scope;
    MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, messageId, nullptr, recipient);
    WRITE_BYTE(206);
    WRITE_SHORT(message.target);
    WRITE_BYTE(message.style);
    WRITE_BYTE(message.flags);
    WRITE_LONG(message.value);
    MESSAGE_END();
    return true;
}
