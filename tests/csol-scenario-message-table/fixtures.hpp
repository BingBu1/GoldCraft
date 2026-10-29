// SPDX-License-Identifier: GPL-3.0-or-later
// Independently encoded synthetic tables; see prepare_expected.py.
#pragma once
#include <string_view>
namespace fixtures {
inline constexpr std::string_view valid = "2b46a57d1ca572bec54b3cba8845d3b807e69ac8c805b47f";
inline constexpr std::string_view replacement = "2b46a57d1ca572be377b46f69345af74c782ebce39f48e87";
inline constexpr std::string_view empty = "f15d9c83a14c4e63";
inline constexpr std::string_view embeddedNul = "2b46a57d1ca572beac79ffb57fdadb60";
inline constexpr std::string_view invalidField = "2b46a57d1ca572bea60795cd5f503c1f1802b1cd07646cad";
inline constexpr std::string_view invalidCsv = "2b46a57d1ca572be97bb8a043cd913cfb064d9d71f0df18d";
}
