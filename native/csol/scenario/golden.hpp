// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "map_resources.hpp"

namespace csol::scenario
{
enum class GoldenConfiguration { Unloaded, Missing, Empty, ParseError, Unsupported };

// Fresh-owner loading only. CSO leaves its initial difficulty/plan/member
// containers empty when this optional file is missing or has no parsed root.
// Nonempty Golden settings require the separate plan/replacement implementation.
class GoldenResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool EmptyPlan() const;
    GoldenConfiguration State() const { return state_; }
    const std::string& Diagnostic() const { return diagnostic_; }
private:
    GoldenConfiguration state_{GoldenConfiguration::Unloaded};
    std::string diagnostic_;
    bool loading_{};
};
}
