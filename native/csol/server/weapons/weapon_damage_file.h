// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "weapon_damage.h"
#include <filesystem>

// Call during server initialization, map setup or an administrator reload,
// on the game thread. All I/O and parsing finish before replacing config.
// The caller supplies its own server game-directory path, never client input.
bool CSOL_LoadBeamDamageFile(const std::filesystem::path& path,
    CCSOLBeamDamageConfig& config, std::string& error);
