// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>

namespace csol
{
enum class PartsBehavior { Standard, AmmoGauge, AmmoState };
struct PartsDefinition
{
	std::vector<std::string> PrimaryActions, Actions;
	double OpenSeconds = 0, CloseSeconds = 0;
	bool FollowSkin = false, ActiveSkin = false;
	bool InclusiveTimer = true;
	PartsBehavior Behavior = PartsBehavior::Standard;
};

inline PartsDefinition PartsProfile(std::string_view source, int slot = 0)
{
	const auto slash = source.find_last_of("/\\");
    std::string name(source.substr(slash == source.npos ? 0 : slash + 1));
    const auto dot = name.find_last_of('.'); if (dot != name.npos) name.resize(dot);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
	if ((name == "v_alterationammogun" || name == "v_alterationammogunex") && slot >= 0 && slot < 2)
		return {{"idle", "shootA", "shootB", "shootC", "draw"},
			slot == 0 ? std::vector<std::string>{"idle", "shootC"} : std::vector<std::string>{"idle", "change_start", "change_idle", "change_end", "shootC"},
			.5, 1., false, false, false, slot == 0 ? PartsBehavior::AmmoGauge : PartsBehavior::AmmoState};
	if (slot != 0) return {};
	if (name == "v_leapstrikegun" || name == "v_leapstrikegunex")
		return {{"idle", "shoot1", "shoot2", "draw", "reload"}, {"idle", "change_start", "change", "change_end"}};
	// Verified client registrations and transition timers. Names alone are not a pairing rule.
	if (name == "v_linkattacksg")
		return {{"idle", "shoot", "shoot", "reload_insert", "reload_after", "reload_start", "draw", "shootB"},
			{"idle", "change_start", "change", "change_end"}, .7, .7, false, true, false};
	if (name == "v_roboarm" || name == "v_roboarm3")
		return {{"idle", "reload", "draw", "shoot_A", "shoot_A_passive", "shoot_B_start", "shoot_B_loop", "shoot_B_passive", "shoot_B_end", "shoot_C"},
			{"idle", "change_start", "change_loop", "change_end"}, double(.33f), double(.67f)};
	if (name == "v_sfboomerang" || name == "v_sfboomerang1" || name == "v_sfboomerang6")
		return {{"idle", "shootA", "shootA_end", "shootB", "shootB_end", "catch", "draw"},
			{"idleA", "changeAB", "idleB", "changeBA"}, double(.7f), double(.53f), true};
	return {};
}

}
