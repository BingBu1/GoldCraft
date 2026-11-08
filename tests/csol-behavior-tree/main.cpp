// SPDX-License-Identifier: GPL-3.0-or-later
// Replays csol::scenario::behavior_tree against original MP instructions run by
// oracle.py (C: composites, T: skill timeline deque, K: SkillSelector tick).
#include "../../native/csol/scenario/bosses/behavior_tree.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bt = csol::scenario::behavior_tree;

static int checks, failures;
static void Check(bool ok, const std::string& what)
{
    ++checks;
    if (!ok) { ++failures; if (failures < 40) std::printf("FAIL %s\n", what.c_str()); }
}
static float F(unsigned bits) { return std::bit_cast<float>(bits); }
static unsigned B(float value) { return std::bit_cast<unsigned>(value); }
static void RequireInput(bool ok, const char* what)
{ if (!ok) throw std::runtime_error(what); }
template<class T> static T Number(const std::string& text, int base = 10)
{
    T value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    RequireInput(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "invalid numeric field");
    return value;
}
static unsigned Hex(const std::string& text) { return Number<unsigned>(text, 16); }
static int Int(const std::string& text) { return Number<int>(text); }
static unsigned Count(const std::string& text) { return Number<unsigned>(text); }
static bool Binary(const std::string& text)
{ return text.find_first_not_of("01") == std::string::npos; }
static std::vector<std::string> Split(const std::string& text, char sep)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, sep)) parts.push_back(part);
    if (!text.empty() && text.back() == sep) parts.emplace_back();
    return parts;
}

// ---------- shared mock context ----------
struct Context
{
    bt::Blackboard board;
    bt::EventTimeline timeline;
    bool bossImprov{};
    std::vector<std::string> log;
    bt::Blackboard& Board() { return board; }
    float Now() const { return 0.0f; }
    bt::EventTimeline& Timeline() { return timeline; }
    const bt::SkillTiming* CurrentSkill() const { return nullptr; }
    bool OwnerIsBossImprov() const { return bossImprov; }
    void EndSkill() { log.push_back("endskill"); }
    void SkillSectorDamage() {}
    void Fail(const char*) { log.push_back("fail"); }
};

static std::string BoardValue(const bt::Blackboard& board, const char* key)
{
    const int value = board.GetInt(key, INT_MIN);
    return value == INT_MIN ? "u" : std::to_string(value);
}

// ---------- C ----------
struct ScriptedLeaf final : bt::Node<Context>
{
    int index; bool result; std::string* order;
    ScriptedLeaf(int i, bool r, std::string* o) : index(i), result(r), order(o) {}
    bool Tick(Context&) override { *order += char('0' + index); return result; }
};

static void Composite(std::istringstream& in, const std::string& line)
{
    std::string name, results, order;
    int count{}, expected{};
    RequireInput(bool(in >> name >> count >> results >> expected >> order), "incomplete composite");
    if (results == "-") results.clear();
    if (order == "-") order.clear();
    RequireInput((name == "seq" || name == "sel" || name == "par") && count >= 0 &&
        results.size() == static_cast<std::size_t>(count) && Binary(results) &&
        (expected == 0 || expected == 1), "invalid composite fields");
    std::unique_ptr<bt::Composite<Context>> node;
    if (name == "seq") node = std::make_unique<bt::Sequence<Context>>();
    else if (name == "sel") node = std::make_unique<bt::Selector<Context>>();
    else node = std::make_unique<bt::Parallel<Context>>();
    std::string actual;
    for (int i = 0; i < count; ++i)
        node->Add(std::make_unique<ScriptedLeaf>(i, results[static_cast<std::size_t>(i)] == '1', &actual));
    Context context;
    const bool result = node->Tick(context);
    Check(result == (expected != 0) && actual == order, "C " + line);
}

// ---------- T ----------
// Mutants: a delay-sorted queue and an inclusive due test must both diverge.
struct SortedTimeline
{
    float start{0.0f};
    std::vector<bt::EventTimeline::Event> events;
    void Push(float delay, int id)
    {
        auto at = std::upper_bound(events.begin(), events.end(), delay,
            [](float d, const bt::EventTimeline::Event& e) { return d < e.delay; });
        events.insert(at, {delay, id});
    }
    bool Advance(float now, int& id, bool inclusive)
    {
        if (0.0f > start || events.empty()) return false;
        const float due = start + events.front().delay;
        if (inclusive ? !(now >= due) : !(now > due)) return false;
        id = events.front().id; events.erase(events.begin()); return true;
    }
};
static int sortedMutant, inclusiveMutant;

static void Timeline(std::istringstream& in, const std::string& line)
{
    bt::EventTimeline timeline;
    SortedTimeline sorted, inclusive;
    bool sortedDiverged{}, inclusiveDiverged{};
    std::string op;
    int operations{};
    while (in >> op)
    {
        ++operations;
        const auto fields = Split(op.substr(1), '/');
        RequireInput(fields.size() == 3, "incomplete timeline operation");
        const char kind = op[0];
        RequireInput(kind == 'P' || kind == 'X' || kind == 'B' || kind == 'A', "unknown timeline operation");
        const std::string head = fields[0];
        std::string fired = "-";
        if (kind == 'P')
        {
            const auto parts = Split(head, ':');
            RequireInput(parts.size() == 2, "incomplete timeline push");
            const float delay = F(Hex(parts[0]));
            const int id = Int(parts[1]);
            timeline.Push(delay, id); sorted.Push(delay, id); inclusive.events.push_back({delay, id});
        }
        else if (kind == 'X')
        {
            RequireInput(head.empty(), "invalid timeline clear");
            timeline.Clear();
            sorted.start = inclusive.start = -1.0f; sorted.events.clear(); inclusive.events.clear();
        }
        else if (kind == 'B')
        {
            const float now = F(Hex(head));
            timeline.Begin(now); sorted.start = inclusive.start = now;
        }
        else
        {
            const auto parts = Split(head, ':');
            RequireInput(parts.size() == 2, "incomplete timeline advance");
            if (parts[1] != "n") Int(parts[1]);
            const float now = F(Hex(parts[0]));
            int id{};
            const bool ok = timeline.Advance(now, id);
            const std::string actual = ok ? std::to_string(id) : "n";
            Check(actual == parts[1], "T fire " + op + " in " + line.substr(0, 80));
            int mid{};
            const bool sok = sorted.Advance(now, mid, false);
            if ((sok ? std::to_string(mid) : "n") != parts[1]) sortedDiverged = true;
            const bool iok = inclusive.Advance(now, mid, true);
            if ((iok ? std::to_string(mid) : "n") != parts[1]) inclusiveDiverged = true;
        }
        const unsigned start = Hex(fields[1]);
        const std::size_t count = Count(fields[2]);
        Check(B(timeline.Start()) == start && timeline.Events().size() == count, "T state " + op);
    }
    RequireInput(operations > 0, "empty timeline case");
    sortedMutant += sortedDiverged; inclusiveMutant += inclusiveDiverged;
}

// ---------- K ----------
struct ScriptedSkill final : bt::SkillState<Context>
{
    int index;
    std::deque<bool> updates, ticks;
    ScriptedSkill(std::int16_t type, int i) : SkillState(type), index(i) {}
    void Record(Context& c, const char* method)
    {
        c.log.push_back(std::to_string(index) + method + "@" + BoardValue(c.board, "cur_skill") + "," +
            BoardValue(c.board, "next_skill"));
    }
    void Bind(Context& c) override { Record(c, "bind"); }
    void Start(Context& c) override { Record(c, "start"); }
    void End(Context& c) override { Record(c, "end"); }
    bool Update(Context& c) override
    {
        Record(c, "update");
        if (updates.empty()) { c.log.push_back("underflow"); return false; }
        const bool v = updates.front(); updates.pop_front(); return v;
    }
    bool Tick(Context& c) override
    {
        Record(c, "tick");
        if (ticks.empty()) { c.log.push_back("underflow"); return false; }
        const bool v = ticks.front(); ticks.pop_front(); return v;
    }
};
static int rebindMutant;

static void Selector(std::istringstream& in, const std::string& line)
{
    std::string typeText, flags, next, current, scripts;
    int tickCount{};
    RequireInput(bool(in >> typeText >> flags >> next >> current >> tickCount >> scripts), "incomplete selector");
    RequireInput(flags.size() == 4 && Binary(flags) && tickCount > 0, "invalid selector fields");
    bt::SkillSelector<Context> selector;
    std::vector<ScriptedSkill*> skills;
    const auto types = typeText == "-" ? std::vector<std::string>{} : Split(typeText, ',');
    const auto scriptParts = scripts == "-" ? std::vector<std::string>{} : Split(scripts, ';');
    RequireInput(scriptParts.size() == types.size(), "selector script count differs from type count");
    for (std::size_t i = 0; i < types.size(); ++i)
    {
        auto skill = std::make_unique<ScriptedSkill>(static_cast<std::int16_t>(Int(types[i])), static_cast<int>(i));
        const auto halves = Split(scriptParts[i], '/');
        RequireInput(halves.size() == 2 && halves[0].size() == static_cast<std::size_t>(tickCount) &&
            halves[1].size() == static_cast<std::size_t>(tickCount) && Binary(halves[0]) && Binary(halves[1]),
            "incomplete selector script");
        for (char c : halves[0]) skill->updates.push_back(c == '1');
        for (char c : halves[1]) skill->ticks.push_back(c == '1');
        skills.push_back(skill.get());
        Check(selector.Add(std::move(skill)), "K add " + line);
    }
    Context context;
    context.bossImprov = flags == "1111";
    if (next != "u") context.board.SetInt("next_skill", Int(next));
    if (current != "u") context.board.SetInt("cur_skill", Int(current));
    bool rebindSeen{};
    for (int t = 0; t < tickCount; ++t)
    {
        std::string token;
        RequireInput(bool(in >> token), "missing selector tick");
        const auto parts = Split(token, '|');
        RequireInput(parts.size() >= 3 && (parts[0] == "0" || parts[0] == "1"), "incomplete selector tick");
        context.log.clear();
        const bool result = selector.Tick(context);
        std::vector<std::string> expected(parts.begin() + 1, parts.end() - 1);
        if (expected.size() == 1 && expected[0] == "-") expected.clear();
        std::vector<std::string> injects;
        while (!expected.empty() && expected.back().rfind("inject", 0) == 0)
        { injects.insert(injects.begin(), expected.back()); expected.pop_back(); }
        for (const auto& entry : context.log)
            if (entry.find("bind") != std::string::npos &&
                std::find_if(context.log.begin(), context.log.end(), [](const std::string& e)
                    { return e.find("start") != std::string::npos; }) == context.log.end())
                rebindSeen = true;
        Check(result == (parts[0] == "1"), "K result tick " + std::to_string(t) + " " + line);
        Check(context.log == expected, "K log tick " + std::to_string(t) + " " + line);
        for (const auto& inject : injects) context.board.SetInt("next_skill", Int(inject.substr(6)));
        const auto final = Split(parts.back(), ',');
        RequireInput(final.size() == 2, "incomplete selector board");
        for (const auto& value : final) if (value != "u") Int(value);
        Check(BoardValue(context.board, "next_skill") == final[0] && BoardValue(context.board, "cur_skill") == final[1],
            "K board tick " + std::to_string(t) + " " + line);
    }
    Check(context.board.Consistent(), "K board types " + line);
    rebindMutant += rebindSeen;
}

// ---------- U ----------
struct TimerContext
{
    bt::Blackboard board;
    bt::EventTimeline timeline;
    bt::SkillTiming row{};
    bool hasRow{true};
    float now{};
    std::vector<std::string> log;
    bt::Blackboard& Board() { return board; }
    float Now() const { return now; }
    bt::EventTimeline& Timeline() { return timeline; }
    const bt::SkillTiming* CurrentSkill() const { return hasRow ? &row : nullptr; }
    bool OwnerIsBossImprov() const { return true; }
    void EndSkill() { log.push_back("endskill"); }
    void SkillSectorDamage() { log.push_back("sector"); }
    void Fail(const char*) { log.push_back("fail"); }
};
struct DefaultSkill final : bt::SkillState<TimerContext>
{
    DefaultSkill() : SkillState(321) {}
    void OnEvent(TimerContext& c, int id) override { c.log.push_back("ev" + std::to_string(id)); }
    void OnJumpEnd(TimerContext& c) override { c.log.push_back("jump@" + BoardValue(c.board, "IsJumpEnd")); }
};
struct BaseEventSkill final : bt::SkillState<TimerContext>
{
    BaseEventSkill() : SkillState(322) {}
};

static void Timer(std::istringstream& in, const std::string& line)
{
    int junk{};
    std::string ani, dd, now, state;
    RequireInput(bool(in >> junk >> ani >> dd >> now >> state) && junk >= 0, "incomplete timer header");
    TimerContext context;
    for (int j = 0; j < junk; ++j) context.timeline.Push(1.0f, 50 + j);
    context.row = {F(Hex(ani)), F(Hex(dd))};
    context.now = F(Hex(now));
    DefaultSkill skill;
    skill.Start(context);
    const auto parts = Split(state, '/');
    RequireInput(parts.size() == 2, "incomplete timer initial state");
    Check(B(context.timeline.Start()) == Hex(parts[0]) &&
        context.timeline.Events().size() == Count(parts[1]), "U start " + line.substr(0, 80));
    std::string token;
    int ticks{};
    while (in >> token)
    {
        ++ticks;
        const auto f = Split(token, ':');
        RequireInput(f.size() == 7 && (f[2] == "0" || f[2] == "1") &&
            (f[3] == "0" || f[3] == "1"), "incomplete timer tick");
        if (f[5] != "u") Int(f[5]);
        context.now = F(Hex(f[0]));
        // "u": the oracle removed the key. Blackboard has no erase; once the
        // key exists, 0 is observably identical for GetInt(name, 0).
        if (f[1] != "u") context.board.SetInt("IsJumpEnd", Int(f[1]));
        else if (BoardValue(context.board, "IsJumpEnd") != "u") context.board.SetInt("IsJumpEnd", 0);
        if (f[2] == "1") context.hasRow = false;
        context.log.clear();
        const bool result = skill.Update(context);
        std::vector<std::string> expected = f[4] == "-" ? std::vector<std::string>{} : Split(f[4], '|');
        Check(result == (f[3] == "1"), "U result " + token);
        Check(context.log == expected, "U log " + token);
        const std::string jump = BoardValue(context.board, "IsJumpEnd");
        Check(jump == f[5] || (f[5] == "u" && jump == "0"), "U latch " + token);
        Check(context.timeline.Events().size() == Count(f[6]), "U count " + token);
    }
    RequireInput(ticks > 0, "empty timer case");
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: %s behavior-tree-oracle.txt\n", argv[0]); return 2; }
    std::ifstream input(argv[1]);
    if (!input.is_open()) { std::printf("FAIL cannot open oracle: %s\n", argv[1]); return 2; }
    std::string line;
    int cases[4]{};
    while (std::getline(input, line))
    {
        try
        {
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "C") { ++cases[0]; Composite(in, line); }
        else if (kind == "T") { ++cases[1]; Timeline(in, line); }
        else if (kind == "K") { ++cases[2]; Selector(in, line); }
        else if (kind == "U") { ++cases[3]; Timer(in, line); }
        else throw std::runtime_error("unknown or empty case kind");
        RequireInput(in.eof() || (in >> std::ws).eof(), "unexpected trailing fields");
        }
        catch (const std::exception& error) { Check(false, std::string("invalid oracle row: ") + error.what()); }
    }
    Check(input.eof() && !input.bad(), "oracle read completed");
    for (int count : cases) Check(count > 0, "every oracle section is present");
    Check(sortedMutant > 0 && inclusiveMutant > 0 && rebindMutant > 0, "all recorded mutants diverge");
    // Pure checks that the oracle cannot express.
    {
        bt::Blackboard board;
        board.SetFloat("hide_value", -1.0f);
        Check(board.GetInt("hide_value", 7) == 7 && !board.Consistent(), "mixed-type access is reported");
        bt::SkillSelector<Context> selector;
        Check(selector.Add(std::make_unique<ScriptedSkill>(321, 0)), "first type 321");
        Check(!selector.Add(std::make_unique<ScriptedSkill>(321, 1)), "duplicate type rejected");
        Check(!selector.Add(std::make_unique<ScriptedSkill>(-1, 2)), "type -1 rejected");
        Check(selector.Find(0x10000 + 321) != nullptr && selector.Find(0xFFFF) == nullptr, "int16 key truncation");
        // 10A21080 defaults: 1000 sector damage, 1001 end skill, others ignored.
        TimerContext timer;
        BaseEventSkill base;
        base.OnEvent(timer, 1000); base.OnEvent(timer, 1001); base.OnEvent(timer, 7);
        Check(timer.log == std::vector<std::string>{"sector", "endskill"}, "default OnEvent");
        timer.log.clear(); timer.hasRow = false; base.Start(timer);
        Check(timer.log == std::vector<std::string>{"fail"}, "Start without row is a reported contract failure");
    }
    std::printf("cases C=%d T=%d K=%d U=%d; mutants sorted-queue=%d inclusive-due=%d rebind-ticks=%d\n",
        cases[0], cases[1], cases[2], cases[3], sortedMutant, inclusiveMutant, rebindMutant);
    std::printf("behavior-tree checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
