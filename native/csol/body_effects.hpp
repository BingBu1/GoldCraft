// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <cctype>

namespace csol
{
enum class BodyEffectProfile { None, Gungnir, Halo, Wing, WingEX, Sword, Janus, Asus };

inline BodyEffectProfile FindBodyEffects(std::string_view file)
{
	auto start = file.find_last_of("/\\");
    std::string name(file.substr(start == file.npos ? 0 : start + 1));
    const auto dot = name.find_last_of('.');
    if (dot != name.npos) name.resize(dot);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
	if (name == "v_gungnir" || name == "v_gungnirex") return BodyEffectProfile::Gungnir;
	if (name == "v_halogun" || name == "v_halogunex") return BodyEffectProfile::Halo;
	if (name == "v_winggun") return BodyEffectProfile::Wing;
	if (name == "v_winggunex") return BodyEffectProfile::WingEX;
	if (name == "v_swordbombard") return BodyEffectProfile::Sword;
	if (name == "v_asusas12") return BodyEffectProfile::Asus;
	if (name == "v_y21s4janusa" || name == "v_y21s4janusb" || name == "v_y21s4janusc" || name == "v_y21s4janusd") return BodyEffectProfile::Janus;
	return BodyEffectProfile::None;
}

// Client viewmodel overrides are per bodypart, independent of the saved MDL body.
// Body is the engine's encoded body value, not a submodel index. -1 means inherit.
struct BodyEffectOverride
{
	int Body = -1, Alpha = -1;
	int ScrollS = 0, ScrollT = 0;
};

struct BodyEffects
{
	BodyEffectProfile Profile = BodyEffectProfile::None;
	std::array<BodyEffectOverride, 15> Groups{};
	int Mode = 0;
	float Deadline = -1;
	double Time = 0;

	void Reset(BodyEffectProfile profile)
	{
		Profile = profile; Groups = {}; Mode = 0; Deadline = -1; Time = 0;
		switch (profile)
		{
		case BodyEffectProfile::Gungnir: Groups[3] = {0, 0}; Groups[4] = {0, 0}; break;
		case BodyEffectProfile::Halo: case BodyEffectProfile::Janus: Groups[2].ScrollS = 255; break;
		case BodyEffectProfile::Wing: Groups[2].ScrollS = 55; Groups[3].ScrollS = 255; break;
		case BodyEffectProfile::WingEX: Groups[2].ScrollS = Groups[3].ScrollS = 255; break;
		case BodyEffectProfile::Sword: Groups[3].ScrollT = 255; break;
		case BodyEffectProfile::Asus: Groups[1].ScrollS = Groups[2].ScrollS = 765; break;
		default: break;
		}
	}

	void Update(int sequence, float now)
	{
		if (Profile != BodyEffectProfile::Gungnir || !std::isfinite(now)) return;
		const int previous = Mode;
		Mode = sequence == 0 ? 1 : sequence >= 3 && sequence <= 5 ? 3 : sequence == 9 ? 2 : 0;
		if (!Mode) { Groups[3] = {0, 0}; Groups[4] = {0, 0}; return; }
		if (previous != Mode) Deadline = now;
		if (Deadline > now) return;
		const int active = Mode == 2 ? 4 : 3, selector = Mode == 2 ? 4 : 2;
		Groups[7 - active] = {0, 0};
		auto& group = Groups[active];
		if (Deadline == -1) group.Body = 0;
		if (group.Alpha > 0)
		{
			group.Alpha = (std::max)(0, group.Alpha - 20);
			if (!group.Alpha) group.Body = selector - group.Body;
			Deadline = now + (group.Alpha ? .02f : Mode == 3 ? .2f : 1.1f);
		}
		else { group.Alpha = 255; Deadline = now; }
	}

	void Advance(int sequence, double seconds)
	{
		if (!std::isfinite(seconds) || seconds < 0) return;
		Time += seconds; Update(sequence, float(Time));
	}

	void Seek(int sequence, double seconds)
	{
		Reset(Profile);
		if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
		// Scrubbing has no game callback history. Replay at the viewer's 100 Hz
		// reference cadence; normal playback uses exactly one callback per Tick.
		if (Profile == BodyEffectProfile::Gungnir)
			for (double t = 0; t < seconds; )
			{
				Update(sequence, float(t));
				// Skip callbacks which cannot change state, including inactive sequences.
				if (!Mode) break;
				t = (std::max)(t + .01, std::ceil(double(Deadline) * 100) / 100);
			}
		Time = seconds; Update(sequence, float(Time));
	}

	std::array<float, 2> Scroll(int group) const
	{
		if (group < 0 || group >= int(Groups.size())) return {};
		const float fraction = float(Time - std::trunc(Time));
		return {float(Groups[group].ScrollS) / 255.f * fraction, float(Groups[group].ScrollT) / 255.f * fraction};
	}
};
}
