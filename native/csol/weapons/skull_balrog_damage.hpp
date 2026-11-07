// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace csol::skull_balrog
{
// Each weapon supplies its verified property names and owns the resulting
// values. Parse/reload is atomic: an invalid file never changes live damage.
bool ParseDamage(std::string_view json, std::span<const std::string_view> fields,
    std::span<float> values, std::string& error);
}
