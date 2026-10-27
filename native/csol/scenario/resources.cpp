// SPDX-License-Identifier: GPL-3.0-or-later
#include "resources.hpp"
#include "monster_types.hpp"
#include "trigger_rules.hpp"
#include "upgrade_tables.hpp"

#include "../config_codec.hpp"

namespace csol::scenario
{
namespace
{
bool DecodeResource(std::span<const std::byte> encoded, std::string& plain, ConfigError& error)
{
    error = {};
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    {
        error.message = "invalid encrypted scenario configuration size";
        return false;
    }
    plain.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    const auto embedded = plain.find('\0');
    if (embedded != plain.npos)
    {
        error = {embedded, "embedded NUL in scenario configuration"};
        return false;
    }
    return true;
}
}

bool ReadWaveResource(std::span<const std::byte> encoded, WaveTable& output, ConfigError& error)
{
    std::string plain;
    return DecodeResource(encoded, plain, error) && ReadWaves(plain, output, error);
}

bool ReadRoundResource(std::span<const std::byte> encoded, RoundTable& output, ConfigError& error)
{
    std::string plain;
    return DecodeResource(encoded, plain, error) && ReadRounds(plain, output, error);
}

bool ReadMonsterTypeResource(std::span<const std::byte> encoded, MonsterTypeTable& output, ConfigError& error)
{
    std::string plain;
    return DecodeResource(encoded, plain, error) && ReadMonsterTypes(plain, output, error);
}

bool ReadTriggerRuleResource(std::span<const std::byte> encoded, TriggerRuleTable& output, ConfigError& error)
{
    std::string plain;
    return DecodeResource(encoded, plain, error) && ReadTriggerRules(plain, output, error);
}

bool ReadUpgradeLevelResource(std::span<const std::byte> encoded, UpgradeLevels& output, ConfigError& error)
{
    std::string plain;
    return DecodeResource(encoded, plain, error) && ReadUpgradeLevels(plain, output, error);
}

bool ReadUpgradeClassResource(std::span<const std::byte> encoded, UpgradeClasses& output, ConfigError& error)
{
    std::string plain;
    return DecodeResource(encoded, plain, error) && ReadUpgradeClasses(plain, output, error);
}
}
