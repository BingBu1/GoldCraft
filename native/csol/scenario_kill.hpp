// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace csol::scenario_kill
{
enum class ParseResult { Unrelated, Malformed, Accepted };
enum class Kind { Frags, Combo, HeadHits };
struct Message { Kind kind{}; int player{}; std::int32_t value{}; };
ParseResult DecodeFrags(std::span<const std::byte> bytes, Message& message);
ParseResult DecodeCombo(std::span<const std::byte> bytes, Message& message);

enum class Image : unsigned { Kill, Combo, Combo10, Combo20, Combo30, Digits, Perfect, Count };
struct Resource { const char* path; int width, height; };
inline constexpr std::array<Resource, unsigned(Image::Count)> Resources{{
    {"resource/Hud/ZBS/ZBSkill.tga",234,55},
    {"resource/ZBSVER2/combo.tga",224,55},
    {"resource/ZBSVER2/combo_10.tga",224,55},
    {"resource/ZBSVER2/combo_20.tga",224,55},
    {"resource/ZBSVER2/combo_30.tga",224,55},
    {"resource/Hud/ZBS/ZBSKillNumber.tga",569,52},
    {"resource/ZBSVER2/perfect.tga",177,92},
}};
struct Quad
{
    Image image{};
    int left{},top{},right{},bottom{},alpha{};
    float u0{},v0{},u1{1},v1{1};
};
struct Frame { std::array<Quad,16> quads{}; unsigned count{}; };
struct Animation { int alpha{},dy{}; };
Animation Animate(float now, float born, int screenHeight);
class State
{
public:
    void Reset();
    void ClearPresentation();
    void Apply(const Message& message);
    void Advance(int localPlayer, float now);
    bool NeedsResources() const;
    Frame Layout(int width,int height) const;
private:
    std::array<std::int32_t,33> frags_{};
    std::int32_t previous_{},combo_{};
    int localPlayer_{};
    bool comboPending_{},headPending_{};
    bool killVisible_{},comboVisible_{},perfectVisible_{};
    float time_{},killBorn_{},comboBorn_{},perfectBorn_{};
};
}
