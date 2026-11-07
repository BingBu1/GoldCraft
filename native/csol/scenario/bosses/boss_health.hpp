// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace csol::boss_health
{
enum class ParseResult { Unrelated, Accepted, Malformed };
struct Message { int appearance{}, entity{}, maximum{}; float ratio{}; bool initialize{}; };
ParseResult Decode(std::span<const std::byte>, Message&);
struct AbilityMessage { int appearance{}; std::vector<int> abilities; };
ParseResult DecodeAbilities(std::span<const std::byte>, AbilityMessage&);
struct Entry { int entity{}, maximum{}; float ratio{}; };
struct Rectangle { int x{}, y{}, width{}, height{}; };
struct Layout
{
    std::array<Rectangle, 3> background;
    Rectangle gauge;
    std::array<int, 4> color;
};
Layout MakeLayout(int width, int height, float ratio);
inline constexpr std::array Resources{
    "resource/helperHud/bosshp_bg_left_oberon.tga",
    "resource/helperHud/bosshp_bg_center.tga",
    "resource/helperHud/bosshp_bg_right.tga",
    "resource/helperHud/bosshp_bg_left.tga",
    "resource/helperHud/BossHP_bg_left_jun00.tga"};
inline constexpr std::array<std::array<int, 2>, 5> Dimensions{{{134,106},{1,106},{13,106},{134,106},{134,106}}};
inline constexpr std::array AbilityResources{
    "resource/Hud/ZBS/bossreinforce_bg_left.tga", "resource/Hud/ZBS/bossreinforce_bg_center.tga",
    "resource/Hud/ZBS/bossreinforce_bg_right.tga"};
// Original cso_chn labels, in the original ability-table order.
inline constexpr std::array AbilityNames{L"迅速",L"分身",L"强力",L"火球",L"冰环",L"瞬移",L"火墙",
    L"雷击",L"无敌",L"重甲",L"再生",L"抗性",L"黑洞",L"蜘网"};

class State
{
public:
    void Reset() { entries_.clear(); selected_ = abilityAppearance_ = -1; abilities_.clear(); }
    void Apply(const Message&);
    void ApplyAbilities(const AbilityMessage&);
    std::span<const int> Abilities() const
    { return selected_ == abilityAppearance_ ? std::span<const int>(abilities_) : std::span<const int>{}; }
    const Entry* Current() const;
    int Appearance() const { return selected_; }
private:
    std::map<int, Entry> entries_;
    int selected_{-1};
    int abilityAppearance_{-1};
    std::vector<int> abilities_;
};
}
