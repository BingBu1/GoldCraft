// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::client {
// CsoMonFX subtypes 6/7/8 carrying CSO private TE133/137/138. Returns true
// for a recognized subtype; malformed or unloadable records draw nothing.
bool ConsumeCsoTent(int size, const void* data);
}
