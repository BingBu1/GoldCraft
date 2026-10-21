// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace csol
{
struct SpriteRule
{
	int Sequence = -1;
	bool Loop = false, Sync = false;
	int Offset = 0, Start = 0, End = 0;
	double Rate = 1;
};
struct SpriteBinding
{
	int Texture = -1;
	std::string Sprite;
	double FPS = 30;
	std::vector<SpriteRule> Rules;
};
struct FadeRule
{
	int Sequence = -1, Offset = 0;
	float In = 0, Out = 0, Hold = -1, Minimum = 0, Maximum = 1;
	bool Once = false;
};
struct FadeBinding
{
	int Texture = -1;
	FadeRule Default;
	bool Restricted = false;
	std::vector<FadeRule> Rules;
	const FadeRule* Rule(int sequence) const;
};
struct FadeResult
{
	bool Active = false, MarkCompleted = false;
	float Alpha = 1;
};
struct FadeState
{
	bool Completed = false, CurrentActive = false, PreviousActive = false;
	// Engine frame cleanup tests the OLD previous set before rotating the sets.
	void BeginFrame();
	void Observe(bool markCompleted);
};
// Keep float evaluation order: the engine uses SSE scalar arithmetic and a 0.01 floor.
FadeResult EvaluateFade(const FadeRule& rule, float frame, float fps, int frames,
	float rate = 1, bool completed = false);
struct ModelJSON
{
	struct TextureBinding { int Texture = -1; std::string Name; };
	std::vector<TextureBinding> Textures;
	std::vector<SpriteBinding> Sprites;
	std::vector<FadeBinding> Fades;
	std::vector<std::string> Notices;
};
ModelJSON ParseModelJSON(std::string_view data, int textures, int sequences);
// Engine semantics: non-sync always cycles; sync ignores sprFPS/frameRate.
// End defaults to the last frame index, and wrapping uses int(frame) % end + start.
double AdvanceSpriteFrame(const SpriteRule& rule, double fps, int frames,
	double previous, double seconds, double modelFrame);
int SpriteFrameIndex(double frame, int frames);
}
