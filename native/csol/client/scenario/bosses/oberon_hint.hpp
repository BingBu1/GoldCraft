// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace csol::client
{
// Returns true only for the dedicated effect22 subset of MPToCL0x5A.
// Death/removal notifications retire attachments and remain available to
// the downstream consumers.
bool ConsumeOberonHint(int size, const void* data);
void ResetOberonHints();
}
