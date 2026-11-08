// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Behavior-tree layer of CSO CBaseZombieBossImprov (MP 39f962f3..., base ctor
// 10A26590, vtable 1181BD68), the framework below CLightZombieBoss (31500).
// Evidence: docs/2026-11-08_分析-僵尸少女31500原版取证-report.md section 2 and
// docs/2026-11-08_实现-行为树纯逻辑层-report.md.
//
// The original trees are built in code (TreeBuilder, BehaviorTreeMgr 10777B40),
// not loaded from files. Every node Tick receives (owner, context); here both
// are folded into one Context type supplied by the host. Required Context API:
//   Blackboard& Board();                 context+4 property object
//   float Now() const;                   gpGlobals->time (122788F4)
//   EventTimeline& Timeline();           improv+0x1E74 (shared by all skills)
//   const SkillTiming* CurrentSkill() const;   monster+4904 row, or null
//   bool OwnerIsBossImprov() const;      RTTI checks 12228E60 / 11F0A554
//   void EndSkill();                     10A27670
//   void SkillSectorDamage();            113E6670(1, 0, 0, 0, 0, -1.0)
//   void Fail(const char* reason);       host-visible contract violation

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#pragma clang fp contract(off)

namespace csol::scenario::behavior_tree
{
// Property object created through engine interface 12278844 vt1076("property").
// Observed calls: vt40 GetInt(name, fallback), vt44 GetFloat(name, fallback),
// vt76 SetInt, vt80 SetFloat. Its storage was not read, so cross-type access is
// not modelled: every original key is used with one type only. A mismatched
// access returns the fallback and clears Consistent() for the host to reject.
class Blackboard
{
public:
    int GetInt(std::string_view key, int fallback) const { return Get<int>(key, fallback, Kind::Int); }
    float GetFloat(std::string_view key, float fallback) const { return Get<float>(key, fallback, Kind::Float); }
    void SetInt(std::string_view key, int value) { Set(key, Kind::Int, Value{.i = value}); }
    void SetFloat(std::string_view key, float value) { Set(key, Kind::Float, Value{.f = value}); }
    bool Consistent() const { return consistent_; }

private:
    enum class Kind : std::uint8_t { Int, Float };
    union Value { int i; float f; };
    struct Entry { Kind kind; Value value; };

    template <class T>
    T Get(std::string_view key, T fallback, Kind kind) const
    {
        const auto found = entries_.find(key);
        if (found == entries_.end()) return fallback;
        if (found->second.kind != kind) { consistent_ = false; return fallback; }
        if constexpr (std::is_same_v<T, int>) return found->second.value.i;
        else return found->second.value.f;
    }
    void Set(std::string_view key, Kind kind, Value value)
    {
        const auto found = entries_.find(key);
        if (found == entries_.end()) { entries_.emplace(std::string(key), Entry{kind, value}); return; }
        if (found->second.kind != kind) { consistent_ = false; return; }
        found->second.value = value;
    }

    std::map<std::string, Entry, std::less<>> entries_;
    mutable bool consistent_{true};
};

// Skill event timeline: a std::deque of {float delay, int id} plus a start time.
// The base ctor 10A26590 zero-fills it, so start begins at 0.0.
class EventTimeline
{
public:
    struct Event { float delay; int id; };

    // 10A27640: start = -1.0, then pop every event.
    void Clear() { start_ = -1.0f; events_.clear(); }
    // 10A26FC0: push_back. Events fire in push order, not sorted by delay.
    void Push(float delay, int id) { events_.push_back({delay, id}); }
    // 10A26FB0: start = now.
    void Begin(float now) { start_ = now; }

    // 10A29580: at most one event per call. Skipped while 0 > start (a NaN
    // start does not skip); the front fires when now > start + delay.
    bool Advance(float now, int& id)
    {
        if (0.0f > start_ || events_.empty()) return false;
        const float due = start_ + events_.front().delay;
        if (!(now > due)) return false;
        id = events_.front().id;
        events_.pop_front();
        return true;
    }

    float Start() const { return start_; }
    const std::deque<Event>& Events() const { return events_; }

private:
    float start_{0.0f};
    std::deque<Event> events_;
};

// SK row fields read by the skill states (row+8 aniTime, row+12 damageDelay).
struct SkillTiming
{
    float aniTime{};
    float damageDelay{};
};

template <class Context>
class Node
{
public:
    virtual ~Node() = default;
    virtual bool Tick(Context& context) = 0;
};

template <class Context>
class Composite : public Node<Context>
{
public:
    Node<Context>& Add(std::unique_ptr<Node<Context>> child)
    {
        children_.push_back(std::move(child));
        return *children_.back();
    }
    std::size_t Size() const { return children_.size(); }

protected:
    std::vector<std::unique_ptr<Node<Context>>> children_;
};

// 10625040: stops at the first child returning 0; empty returns 1.
template <class Context>
class Sequence final : public Composite<Context>
{
public:
    bool Tick(Context& context) override
    {
        for (auto& child : this->children_)
            if (!child->Tick(context)) return false;
        return true;
    }
};

// 10625090: stops at the first child returning 1; empty returns 0.
template <class Context>
class Selector final : public Composite<Context>
{
public:
    bool Tick(Context& context) override
    {
        for (auto& child : this->children_)
            if (child->Tick(context)) return true;
        return false;
    }
};

// 10624F80: ticks every child and returns 1.
template <class Context>
class Parallel final : public Composite<Context>
{
public:
    bool Tick(Context& context) override
    {
        for (auto& child : this->children_) child->Tick(context);
        return true;
    }
};

// 10625130: GetInt(name, 0) != 0.
template <class Context>
class If final : public Node<Context>
{
public:
    explicit If(std::string name) : name_(std::move(name)) {}
    bool Tick(Context& context) override { return context.Board().GetInt(name_, 0) != 0; }

private:
    std::string name_;
};

// 10625170: GetInt(name, 0) == value.
template <class Context>
class IfInt final : public Node<Context>
{
public:
    IfInt(std::string name, int value) : name_(std::move(name)), value_(value) {}
    bool Tick(Context& context) override { return context.Board().GetInt(name_, 0) == value_; }

private:
    std::string name_;
    int value_;
};

// ZombieBoss_TimerSkill (ctor 10A20DC0, vtable 1181ABE8 / 1181AC0C).
// Virtual slots used by the selector: +10 Bind, +14 Start, +18 End, +1C Update,
// +08 Tick; secondary vtable +0 OnEvent, +4 OnJumpEnd.
template <class Context>
class SkillState
{
public:
    static constexpr int DamageEvent = 1000;
    static constexpr int FinishEvent = 1001;

    explicit SkillState(std::int16_t type) : type_(type) {}
    virtual ~SkillState() = default;
    std::int16_t Type() const { return type_; }

    // 10A217D0 records the owner improv and its timeline; the host Context
    // already exposes both.
    virtual void Bind(Context&) {}

    // 10A20F10: clear; push (damageDelay, 1000), (aniTime, 1001); begin now.
    virtual void Start(Context& context)
    {
        const SkillTiming* row = context.CurrentSkill();
        if (!row) { context.Fail("timer skill started without a current SK row"); return; }
        auto& timeline = context.Timeline();
        timeline.Clear();
        timeline.Push(row->damageDelay, DamageEvent);
        timeline.Push(row->aniTime, FinishEvent);
        timeline.Begin(context.Now());
    }

    // 109D9FB0: clear the timeline.
    virtual void End(Context& context) { context.Timeline().Clear(); }

    // 10A21030: advance one event, then the IsJumpEnd latch (10A29120), then
    // report completion once the owner's current row is cleared.
    virtual bool Update(Context& context)
    {
        int id{};
        if (context.Timeline().Advance(context.Now(), id)) OnEvent(context, id);
        auto& board = context.Board();
        if (board.GetInt("IsJumpEnd", 0) != 0)
        {
            OnJumpEnd(context);
            board.SetInt("IsJumpEnd", 0);
        }
        return context.CurrentSkill() == nullptr;
    }

    // 106FFA00: return 1.
    virtual bool Tick(Context&) { return true; }

    // 10A21080: 1000 sector damage, 1001 end skill, others ignored.
    virtual void OnEvent(Context& context, int id)
    {
        if (id == DamageEvent) context.SkillSectorDamage();
        else if (id == FinishEvent) context.EndSkill();
    }

    // 10611F70: no-op.
    virtual void OnJumpEnd(Context&) {}

private:
    std::int16_t type_;
};

// ZombieBoss_SkillSelector (builder 10940AF0, vtable 11800478, Tick 10A212D0).
// Children are keyed by their int16 skillType (10A20E60 reads the property;
// 10A20F90 compares cx with -1 and FNV-hashes the two low bytes, so the key is
// truncated to int16 and -1 or a miss returns null).
// Call order (10A212D0 disassembly): incoming found -> End(old) if any,
// cur_skill = next, next_skill = -1, Bind(owner), Start(ctx); else active found
// -> Bind(owner); then Update(ctx): 0 -> return Tick(owner, ctx); nonzero ->
// End(ctx), cur_skill = -1, return 0.
template <class Context>
class SkillSelector final : public Node<Context>
{
public:
    // The original insertion helper 10A20B30 was not read, so duplicate keys
    // are rejected instead of choosing between first/last wins.
    bool Add(std::unique_ptr<SkillState<Context>> state)
    {
        if (!state || state->Type() == -1 || index_.contains(state->Type())) return false;
        index_.emplace(state->Type(), states_.size());
        states_.push_back(std::move(state));
        return true;
    }

    SkillState<Context>* Find(int key) const
    {
        const auto type = static_cast<std::int16_t>(key);
        if (type == -1) return nullptr;
        const auto found = index_.find(type);
        return found == index_.end() ? nullptr : states_[found->second].get();
    }

    bool Tick(Context& context) override
    {
        auto& board = context.Board();
        const int next = board.GetInt("next_skill", 0);
        const int current = board.GetInt("cur_skill", 0);
        SkillState<Context>* incoming = Find(next);
        SkillState<Context>* active = Find(current);
        if (incoming)
        {
            if (active) active->End(context);
            board.SetInt("cur_skill", next);
            board.SetInt("next_skill", -1);
            incoming->Bind(context);
            incoming->Start(context);
            active = incoming;
        }
        else if (!active)
        {
            // 10A213B5..10A2141B: an unknown non -1 current skill ends the
            // owner's skill when the owner passes both RTTI checks.
            if (current != -1 && context.OwnerIsBossImprov()) context.EndSkill();
            return false;
        }
        else
        {
            // 10A21361..10A2136C: a running skill is re-bound every tick, then
            // updated without Start.
            active->Bind(context);
        }
        if (!active->Update(context)) return active->Tick(context);
        active->End(context);
        board.SetInt("cur_skill", -1);
        return false;
    }

private:
    std::vector<std::unique_ptr<SkillState<Context>>> states_;
    std::map<std::int16_t, std::size_t> index_;
};
}
