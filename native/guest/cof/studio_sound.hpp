// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

namespace guest::cof
{
enum class StudioSoundKind : unsigned char { Native, LocalResource, OriginalMissing };
struct StudioSoundDependency
{
    StudioSoundKind kind{StudioSoundKind::Native};
    std::string_view resource{};
};
}
