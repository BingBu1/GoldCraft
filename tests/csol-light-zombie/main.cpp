// SPDX-License-Identifier: GPL-3.0-or-later
// Replays csol::scenario::light_zombie against original MP instructions run by
// oracle.py (A/R/B/K/J/E/O/I/C/L/W sections, see that file).
#include "../../native/csol/scenario/bosses/light_zombie.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace lz = csol::scenario::light_zombie;
using lz::Vec3;

static int checks, failures, mutants[4];
static void Check(bool ok, const std::string& what)
{
    ++checks;
    if (!ok) { ++failures; if (failures < 40) std::printf("FAIL %s\n", what.c_str()); }
}
static float F(unsigned bits) { return std::bit_cast<float>(bits); }
static unsigned B(float value) { return std::bit_cast<unsigned>(value); }
static unsigned H(const std::string& s) { return static_cast<unsigned>(std::stoul(s, nullptr, 16)); }
static std::vector<std::string> Split(const std::string& text, char sep)
{
    std::vector<std::string> parts; std::string part; std::istringstream in(text);
    while (std::getline(in, part, sep)) parts.push_back(part);
    return parts;
}
static Vec3 V(const std::string& text)
{
    const auto p = Split(text, ',');
    return {F(H(p[0])), F(H(p[1])), F(H(p[2]))};
}
static bool Same(const Vec3& v, const std::string& text)
{
    const auto p = Split(text, ',');
    return B(v[0]) == H(p[0]) && B(v[1]) == H(p[1]) && B(v[2]) == H(p[2]);
}
static std::vector<std::string> Tokens(std::istringstream& in)
{
    std::vector<std::string> t; std::string s;
    while (in >> s) t.push_back(s);
    return t;
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: %s light-zombie-oracle.txt\n", argv[0]); return 2; }
    std::ifstream input(argv[1]);
    std::string line;
    int cases[11]{};
    const std::string kinds = "ARBKJEOICLW";
    while (std::getline(input, line))
    {
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        const auto t = Tokens(in);
        const auto index = kinds.find(kind);
        if (index == std::string::npos) { Check(false, "unknown " + line); continue; }
        ++cases[index];
        if (kind == "A")
            Check(B(lz::AngleMod(F(H(t[0])))) == H(t[1]), "A " + line);
        else if (kind == "R")
        {
            const auto hit = lz::RadiusDamage(V(t[0]), V(t[1]), F(H(t[2])), F(H(t[3])));
            Check(B(hit.damage) == H(t[4]) && Same(hit.direction, t[5]), "R " + line);
        }
        else if (kind == "B")
        {
            csol::scenario::BossSkillRow row;
            row.damage = F(H(t[6])); row.damageCosine = F(H(t[7])); row.damageHeight = F(H(t[8]));
            row.damageDistance = F(H(t[9])); row.forceAttackTime = F(H(t[10]));
            const float now = F(H(t[0])), next = F(H(t[1]));
            const bool hit = lz::BombCanHit(row, now, next, V(t[2]), F(H(t[3])), F(H(t[4])), V(t[5]));
            bool ok = hit == (t[11] == "1");
            if (hit) ok = ok && H(t[12]) == B(row.damage) && H(t[13]) == B(lz::BombNextAttack(row, now));
            else ok = ok && t[12] == "-" && H(t[13]) == B(next);
            Check(ok, "B " + line);
        }
        else if (kind == "K")
        {
            const float dt = F(H(t[0])), yaw = F(H(t[1]));
            const auto steer = lz::BombTrack(dt, yaw, V(t[2]), V(t[3]), V(t[4]), F(H(t[5])), V(t[6]));
            bool ok = Same(steer.velocity, t[10]);
            if (steer.turned) ok = ok && H(t[7]) == B(steer.yaw) && H(t[8]) == 0 && H(t[9]) == 0;
            else ok = ok && H(t[7]) == B(yaw) && H(t[8]) == B(7.0f) && H(t[9]) == B(9.0f);
            Check(ok, "K " + line);
        }
        else if (kind == "J")
        {
            const auto flight = lz::JumpStep(V(t[0]), V(t[1]), F(H(t[2])), F(H(t[3])), F(H(t[4])), F(H(t[5])));
            Check(Same(flight.velocity, t[6]) && B(flight.progress) == H(t[7]) && flight.landed == (t[8] == "1"),
                "J " + line);
        }
        else if (kind == "E")
        {
            Check(t[1] == "4" && H(t[2]) == B(255.0f) && t.size() == 4 &&
                H(t[3]) == B(lz::HideStep(F(H(t[0])))), "E " + line);
        }
        else if (kind == "O")
        {
            const int mode = std::stoi(t[0]);
            const float amount = F(H(t[1])), value = F(H(t[2])), rush = F(H(t[3])), now = F(H(t[5]));
            const bool light = t[4] == "1";
            const auto state = Split(t[10], '/');
            bool ok = t[6] == "1";
            if (mode != 4)
                ok = ok && H(t[7]) == B(amount) && t[8] == "22" && H(t[9]) == B(-1.0f) &&
                    state[1] == "0" && t[11] == "-";
            else
            {
                const auto fade = lz::HideFadeOut(amount, value);
                ok = ok && H(t[7]) == B(fade.amount);
                if (fade.finished)
                    ok = ok && t[8] == "0" &&
                        H(t[9]) == B(light ? lz::ReappearTime(rush, now) : -1.0f) &&
                        H(state[0]) == B(now) && state[1] == "1" && t.size() == 12 && H(t[11]) == B(-1.0f);
                else
                    ok = ok && t[8] == "22" && H(t[9]) == B(-1.0f) && state[1] == "0" && t[11] == "-";
            }
            Check(ok, "O " + line);
        }
        else if (kind == "I")
        {
            const int mode = std::stoi(t[0]);
            const float amount = F(H(t[1])), now = F(H(t[2])), reappear = F(H(t[3]));
            bool ok;
            if (mode == 4 && lz::FadeInDue(reappear, now))
            {
                const auto fade = lz::HideFadeIn(amount);
                ok = H(t[4]) == B(fade.amount) && t[5] == "22" &&
                    H(t[6]) == B(fade.finished ? -1.0f : reappear) && t[7] == (fade.finished ? "1" : "0");
            }
            else ok = H(t[4]) == B(amount) && t[5] == "0" && H(t[6]) == B(reappear) && t[7] == "0";
            Check(ok, "I " + line);
        }
        else if (kind == "C")
        {
            const int sequence = std::stoi(t[0]);
            const float now = F(H(t[1])), interval = F(H(t[2]));
            const bool right = std::stoi(t[3]) != 0;
            const float yaw = F(H(t[4])), yawRandom = F(H(t[5])), speedRandom = F(H(t[6])), delay = F(H(t[7]));
            const bool fires = sequence == 22 && now > interval;
            bool ok = t[8] == "1";
            if (!fires) ok = ok && t.size() == 10 && t[9] == "-";
            else
            {
                const auto a = Split(t[10], ','), b = Split(t[11], ',');
                ok = ok && t.size() == 16 && std::stoi(t[9]) == lz::CrazyAttachment(right) &&
                    H(a[0]) == 0 && H(a[1]) == B(lz::CrazyFanHigh(right)) &&
                    H(b[0]) == B(lz::CrazySpeedLow) && H(b[1]) == B(lz::CrazySpeedHigh) &&
                    Same(lz::CrazyThrowDirection(yawRandom, yaw), t[12]) && H(t[13]) == B(speedRandom) &&
                    t[14][0] == 'f' && H(t[14].substr(1)) == B(lz::CrazyNextFire(delay, now)) &&
                    t[15] == (right ? "i00000000" : "i00000001");
            }
            Check(ok, "C " + line);
        }
        else if (kind == "L")
        {
            const bool jumping = t[1] == "1";
            const Vec3 origin = V(t[2]), point = V(t[3]), destination = V(t[4]);
            const float yaw = F(H(t[5])), yawRandom = F(H(t[6]));
            std::optional<lz::NavExtent> area;
            if (t[7] == "1") area = lz::NavExtent{V(t[8]), V(t[9]), V(t[10])};
            const float distance = lz::RelocationDistance(jumping, origin, destination, point);
            const auto result = lz::RelocationPoint(origin, point, distance, yawRandom, yaw, area);
            const auto r = Split(t[11], ',');
            Check(H(r[0]) == B(-90.0f) && H(r[1]) == B(90.0f) && H(t[12]) == 0x11D87580u &&
                t[13] == t[3] && H(t[14]) == B(120.0f) && Same(result, t[15]), "L " + line);
            // Mutant: min(1000, d) instead of max.
            if (!jumping)
            {
                const float d = lz::PlanarDistance(origin, point);
                const float wrong = d < lz::JumpRelocateMinimum ? d : lz::JumpRelocateMinimum;
                if (!Same(lz::RelocationPoint(origin, point, wrong, yawRandom, yaw, area), t[15])) ++mutants[0];
            }
        }
        else if (kind == "W")
        {
            const float p22 = F(H(t[0])), p23 = F(H(t[1])), p24 = F(H(t[2])), p25 = F(H(t[3])), p26 = F(H(t[4]));
            const float damage = F(H(t[5])), money = F(H(t[6]));
            const auto range = lz::BombRewardMoneyRange(p25, p26, damage);
            const auto r = Split(t[9], ',');
            const auto reward = lz::BombHitReward(p22, p23, p24, damage, money, t[7] == "1", std::stoi(t[8]));
            Check(H(r[0]) == B(range.low) && H(r[1]) == B(range.high) &&
                reward.points == std::stoi(t[10]) && reward.money == std::stoi(t[11]), "W " + line);
        }
    }
    // Static checks the oracle does not express.
    Check(lz::RowAllowed(lz::Hide, 0) && !lz::RowAllowed(lz::Hide, 4) && lz::RowAllowed(lz::CrazyBomb, 4),
        "10985420 row filter");
    Check(lz::BombSequence(20.1f, 0.0f) == 2 && lz::BombSequence(6.1f, 0.0f) == 1 && lz::BombSequence(6.0f, 0.0f) == 0,
        "10984D00 sequence thresholds");
    std::printf("cases");
    for (std::size_t i = 0; i < kinds.size(); ++i) std::printf(" %c=%d", kinds[i], cases[i]);
    std::printf("; mutants relocation-min=%d\n", mutants[0]);
    std::printf("light-zombie checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
