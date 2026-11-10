// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace csol {
struct MenuColorHash {
    using is_transparent=void;
    std::size_t operator()(std::string_view name)const noexcept
    { return std::hash<std::string_view>{}(name); }
};
using MenuColors=std::unordered_map<std::string,std::array<int,4>,MenuColorHash,std::equal_to<>>;
inline const std::array<int,4>& MenuColor(const MenuColors& colors,std::string_view name)
{
    const auto found=colors.find(name);
    if(found==colors.end())throw std::out_of_range("Unknown menu color");
    return found->second;
}
inline constexpr char MenuFontResource[]="resource/font/df_gb_y9.ttf";
inline constexpr char MenuTabClickSound[]="events/tab_1.wav";
inline constexpr char MenuTabClickResource[]="sound/events/tab_1.wav";
struct MenuRect {
    int x{},y{},w{},h{};
    bool Contains(float px,float py)const {return px>=x && py>=y && px<x+w && py<y+h;}
};
struct MenuTexture {std::string path;int width{},height{};};
struct CharacterDescription {
    std::string text;
    MenuRect image,description;
    // Default ClassMod profile caps, already expressed as levels (no +1).
    std::optional<std::array<int,5>> defaultCaps;
};
struct CharacterMenuStats {
    MenuRect heading;
    std::array<MenuRect,5> labels,bars,values;
    std::array<std::string,5> names;
    std::string headingText;
    int maximum{};
    MenuTexture gauge;
};
struct CharacterMenu {
    int width{},height{};
    bool normalizedLayout{};
    MenuRect root;
    MenuRect info,previous,next;
    std::array<MenuRect,10> slots;
    std::array<MenuRect,2> teams;
    std::array<MenuTexture,9> frame;
    std::array<std::array<MenuTexture,3>,3> buttons;
    std::array<std::array<MenuTexture,3>,6> tabs;
    std::array<MenuTexture,3> close;
    MenuTexture pin;
    std::array<MenuTexture,2> slotBackground;
    MenuTexture keyboard;
    std::string currentLabel;
    std::array<std::string,2> teamNames;
    std::string previousLabel,nextLabel,font;
    MenuColors colors;
    std::unordered_map<unsigned,CharacterDescription> descriptions;
    bool fullScreenFrame{};
    MenuRect title,pageLabel,closeButton;
    MenuRect scenarioImage,scenarioDescription;
    CharacterMenuStats stats;
    int fontSize{9},fontWeight{400},titleSize{11},titleWeight{800};
};
CharacterMenu ParseCharacterMenu(std::string_view json);
}
