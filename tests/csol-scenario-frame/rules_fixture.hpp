// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"

// The production accessors and field layout are the pinned SDK's. Construction
// is deliberately linked to rules_stubs.cpp; it is not the real server startup.
class FrameRules final : public CHalfLifeMultiplay
{
public:
    void Started(bool started) { m_bGameStarted = started; }
};
