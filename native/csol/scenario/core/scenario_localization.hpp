// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace csol::scenario_hud
{
using LocalizedTokens = std::map<std::u16string, std::u16string, std::less<>>;
// Read only the requested Tokens from Valve's localization text. Keeping a
// private catalog avoids replacing native CS menu translations globally.
bool ReadLocalizedTokens(std::u16string_view text, const std::set<std::u16string>& wanted,
    LocalizedTokens& output, std::string& error);
}
