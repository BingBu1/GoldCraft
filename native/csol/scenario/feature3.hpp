// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"
#include <memory>

namespace csol::scenario
{
// Original v0 clones a possibly empty callable; this query never evaluates it.
// A successful clone may be null. Clone destruction is observable and must
// occur in reverse order before publishing the classification result.
// Concrete original callable types/producers remain separate implementations.
class Feature3Condition
{
public:
    virtual ~Feature3Condition() = default;
    virtual bool Clone(std::unique_ptr<Feature3Condition>& clone, ConfigError& error) const = 0;
};

struct Feature3Timing
{
    float a{}, b{}, c{}, d{};
};

struct Feature3Record
{
    Feature3Timing timing;
    std::shared_ptr<const Feature3Condition> first, second;
    // Entry+4 is cleared by Stop but is not B (the classifier uses entry+8).
    float intermediateDuration{};
};

struct Feature3Query
{
    // Pure, synchronous reads. Neither callback may mutate state. Re-resolve
    // the parent lifetime first; clock reads the current time for each node.
    bool (*current)(const void*){};
    const void* owner{};
    bool (*clock)(const void*, float&){};
    const void* clockOwner{};
};

// Live signed32-key records. Inserts/removals of other nodes affect traversal
// immediately. Replacing/removing the node currently being classified cancels
// its query, avoiding the original raw-node lifetime hazard. No key snapshot
// or cached categories substitute for per-node time/clone side effects.
class Feature3 final : public std::enable_shared_from_this<Feature3>
{
public:
    ~Feature3() { Invalidate(); }
    bool Put(std::int32_t key, Feature3Record record);
    bool SetTiming(std::int32_t key, Feature3Timing timing);
    bool SetConditions(std::int32_t key, std::shared_ptr<const Feature3Condition> first,
        std::shared_ptr<const Feature3Condition> second);
    bool Remove(std::int32_t key);
    void Invalidate();
    bool Valid() const { return m_valid; }
    bool Classify(std::int32_t key, const Feature3Query& query, int& category, ConfigError& error);
    bool AnyActive(const Feature3Query& query, bool& active, ConfigError& error);
    // Original 1083DA10 timing prefix. Only categories0/1 change; absent keys
    // are not inserted. The host must broadcast the module change afterwards.
    bool Stop(std::int32_t key, bool clearFade, const Feature3Query& query,
        bool& changed, ConfigError& error);
private:
    std::map<std::int32_t, std::shared_ptr<Feature3Record>> m_records;
    std::size_t m_queryDepth{};
    bool m_valid{true};
};
}
