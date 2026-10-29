// SPDX-License-Identifier: GPL-3.0-or-later
#include "feature3.hpp"
#include <utility>
#include <optional>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::scenario
{
bool Feature3::Put(std::int32_t key, Feature3Record record)
{
    const auto keepAlive = weak_from_this().lock();
    if (!keepAlive || !m_valid) return false;
    const auto next = std::make_shared<Feature3Record>(std::move(record));
    auto& slot = m_records[key];
    auto retired = std::move(slot);
    slot = next;
    retired.reset(); // Publish first: source-condition destructors may reenter.
    const auto found = m_records.find(key);
    return m_valid && found != m_records.end() && found->second == next;
}

bool Feature3::SetTiming(std::int32_t key, Feature3Timing timing)
{
    if (!m_valid) return false;
    const auto found = m_records.find(key);
    if (found == m_records.end()) return false;
    // Preserve IEEE values: unordered comparisons are part of the original
    // classifier. A/B are captured before cloning, C/D after both clones.
    found->second->timing = timing;
    return true;
}

bool Feature3::SetConditions(std::int32_t key, std::shared_ptr<const Feature3Condition> first,
    std::shared_ptr<const Feature3Condition> second)
{
    const auto keepAlive = weak_from_this().lock();
    if (!keepAlive || !m_valid) return false;
    const auto found = m_records.find(key);
    if (found == m_records.end()) return false;
    const auto node = found->second;
    auto oldFirst = std::move(node->first), oldSecond = std::move(node->second);
    node->first = std::move(first);
    node->second = std::move(second);
    oldSecond.reset();
    oldFirst.reset();
    const auto current = m_records.find(key);
    return m_valid && current != m_records.end() && current->second == node;
}

bool Feature3::Remove(std::int32_t key)
{
    const auto keepAlive = weak_from_this().lock();
    if (!keepAlive || !m_valid) return false;
    auto removed = m_records.extract(key);
    const bool existed = !removed.empty();
    // Extract before releasing conditions. No iterator is used after a user
    // destructor, and a reentrant insertion is an independent new node.
    return existed;
}

void Feature3::Invalidate()
{
    const auto keepAlive = weak_from_this().lock();
    m_valid = false;
    auto retired = std::move(m_records);
    m_records = {};
}

bool Feature3::Classify(std::int32_t key, const Feature3Query& query, int& category,
    ConfigError& error)
{
    const auto keepAlive = weak_from_this().lock();
    const auto owned = [&] { return keepAlive && query.current && query.current(query.owner) && m_valid; };
    if (!owned()) return false;
    if (m_queryDepth >= 32)
    { error.message = "feature3 nested classification limit exceeded"; return false; }
    ++m_queryDepth;
    struct QueryScope { std::size_t& depth; ~QueryScope() { --depth; } } scope{m_queryDepth};
    const auto found = m_records.find(key);
    if (found == m_records.end()) { category = 3; return true; }
    const auto node = found->second;
    const auto current = [&]
    {
        if (!owned()) return false;
        const auto match = m_records.find(key);
        return match != m_records.end() && match->second == node;
    };
    const float a = node->timing.a, b = node->timing.b;
    std::unique_ptr<Feature3Condition> first, second;
    ConfigError cloneError;
    const auto release = [&]
    {
        second.reset();
        const bool afterSecond = current();
        first.reset();
        return current() && afterSecond;
    };
    const auto clone = [&](bool firstSource, std::unique_ptr<Feature3Condition>& output)
    {
        ConfigError diagnostic;
        bool success = true;
        try
        {
            // Pin only this source at its original lookup position. The
            // first clone can replace the second source before it is read.
            const auto source = firstSource ? node->first : node->second;
            if (source) success = source->Clone(output, diagnostic);
        }
        catch (...)
        {
            success = false;
            diagnostic.message = "feature3 condition clone threw";
        }
        if (!current()) return false;
        if (!success)
        {
            cloneError = std::move(diagnostic);
            if (cloneError.message.empty()) cloneError.message = "feature3 condition clone failed";
        }
        return success;
    };
    const auto failedClone = [&]
    {
        // Cleanup callbacks can reenter with the caller's error object. Only
        // publish this operation's diagnostic after both clones are released.
        if (release() && !cloneError.message.empty()) error = std::move(cloneError);
        return false;
    };
    if (!clone(true, first)) return failedClone();
    if (!clone(false, second)) return failedClone();
    if (!query.clock)
    {
        const bool retained = release();
        if (retained) error.message = "feature3 clock is unavailable";
        return false;
    }
    const float c = node->timing.c, d = node->timing.d;
    // ADDSS precedes the per-node time read; comparisons are ordered >.
    // Float locals retain single-precision rounding, including overflow/NaN.
    const float start = c + a;
    float now{};
    if (!query.clock(query.clockOwner, now))
    {
        const bool retained = release();
        if (retained) error.message = "feature3 clock is unavailable";
        return false;
    }
    int result{};
    if (start > now) result = 0;
    else
    {
        const float ending = d - b;
        result = ending > now ? 1 : d > now ? 2 : 3;
    }
    if (!release()) return false;
    category = result;
    return true;
}

bool Feature3::Stop(std::int32_t key, bool clearFade, const Feature3Query& query,
    bool& changed, ConfigError& error)
{
    const auto keepAlive = weak_from_this().lock();
    const auto owned = [&] { return keepAlive && query.current && query.current(query.owner) && m_valid; };
    changed = false;
    int category{};
    if (!owned() || !Classify(key, query, category, error) || !owned()) return false;
    if (!error.message.empty()) return false;
    if (category != 0 && category != 1) return true;
    const auto found = m_records.find(key);
    if (found == m_records.end()) return false;
    const auto node = found->second;
    const auto current = [&]
    {
        if (!owned()) return false;
        const auto match = m_records.find(key);
        return match != m_records.end() && match->second == node;
    };
    node->timing.a = 0;
    node->intermediateDuration = 0;
    if (clearFade) node->timing.b = 0;
    const auto clock = [&](float& now)
    {
        if (!query.clock || !query.clock(query.clockOwner, now))
        {
            if (current()) error.message = "feature3 stop clock is unavailable";
            return false;
        }
        return current();
    };
    float now{};
    if (!clock(now)) return false;
    node->timing.c = now;
    const float fade = node->timing.b;
    if (!clock(now)) return false;
    node->timing.d = fade + now;
    changed = true;
    return true;
}

bool Feature3::AnyActive(const Feature3Query& query, bool& active, ConfigError& error)
{
    const auto keepAlive = weak_from_this().lock();
    const auto current = [&] { return keepAlive && query.current && query.current(query.owner) && m_valid; };
    if (!current()) return false;
    std::optional<std::int32_t> next;
    if (!m_records.empty()) next = m_records.begin()->first;
    while (next)
    {
        const auto key = *next;
        int category{};
        if (!Classify(key, query, category, error) || !current()) return false;
        if (category != 3) { active = true; return true; }
        // Resolve the live successor after clone/destructor callbacks. A new
        // later key participates now; insertion before this key does not.
        const auto successor = m_records.upper_bound(key);
        next = successor == m_records.end() ? std::nullopt :
            std::optional<std::int32_t>(successor->first);
    }
    active = false;
    return true;
}
}
