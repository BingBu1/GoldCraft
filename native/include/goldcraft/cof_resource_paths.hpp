// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>
namespace goldcraft::cof
{
// Caller first enforces ordinary relative-path/device-name validation and case.
// Foreign-game cold parsers consume only these data types in this namespace.
constexpr bool DownloadDataPath(std::string_view canonical) noexcept
{
    return (canonical.starts_with("inventoryitems/guest/cof/weapons/")&&canonical.ends_with(".txt"))||
        (canonical.starts_with("aurora/guest/cof/")&&canonical.ends_with(".aur"));
}
}
