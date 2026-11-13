// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct cl_entity_s;
namespace guest::cof::client
{
// Original d2a04641 client1008D21C. Call at actual Studio model draw before
// bone setup, with the current original-weapon identity and server angle.
// The host authenticates the packet generation; this function owns no reset,
// network channel or persistent controller storage.
bool ApplyRevolverCylinder(cl_entity_s&,const cl_entity_s* currentViewModel,
    unsigned originalWeapon,float angle) noexcept;
}
