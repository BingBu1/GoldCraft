// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <optional>

namespace csol
{
// CSOL HUD_StudioEvent '#' branch / engine R_MuzzleFlash2, independent of bindSpr.
struct SpriteEvent
{
	int Resource = 2, Alternate = 0, Attachment = 0, Follow = -1;
	float Scale = .2f;
	double FPS = 20, Life = .01;
	int Rotation = 0;
	bool RandomFrame = true, Animate = false, Loop = false, Overlay = false;
	bool Alpha = false, HandFlip = false, SequenceOnly = false;
	std::string Notices;
	std::string ResourceName() const;
	double Duration(int frames) const;
};
std::optional<SpriteEvent> ParseSpriteEvent(int id, std::string_view options);
}
