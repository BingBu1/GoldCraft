// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"

namespace csol::client {
// Original HUD_StudioEvent 5050 (02A41BAD): play the sentence named by the
// event options in the view player's character voice. Returns true when the
// event is consumed.
bool EmotionVoiceStudioEvent(const mstudioevent_t* event, const cl_entity_t* entity);
void ResetEmotionVoices();
}
