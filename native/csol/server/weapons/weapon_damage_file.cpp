// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_damage_file.h"

#include <fstream>

bool CSOL_LoadBeamDamageFile(const std::filesystem::path& path,
    CCSOLBeamDamageConfig& config, std::string& error)
{
    error.clear();
    std::error_code statusError;
    if (!std::filesystem::is_regular_file(path, statusError))
    {
        error = "damage configuration file is missing, inaccessible or not a regular file";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "cannot open damage configuration file";
        return false;
    }

    // Read at most one byte beyond the parser's limit, without relying on a
    // separately queried file size that might change while an editor saves.
    constexpr std::size_t MaxBytes = 64 * 1024;
    std::string bytes(MaxBytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = file.gcount();
    if (file.bad() || (file.fail() && !file.eof()))
    {
        error = "cannot read damage configuration file";
        return false;
    }
    if (count > static_cast<std::streamsize>(MaxBytes))
    {
        error = "damage configuration exceeds 65536 bytes";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(count));
    return config.Parse(bytes, error);
}
