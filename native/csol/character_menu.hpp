// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace csol {
struct MenuRect {
    int x{},y{},w{},h{};
    bool Contains(float px,float py)const {return px>=x && py>=y && px<x+w && py<y+h;}
};
struct MenuTexture {std::string path;int width{},height{};};
struct CharacterDescription {std::string text;MenuRect image,description;};
struct CharacterMenu {
    int width{},height{};
    MenuRect info,previous,next;
    std::array<MenuRect,10> slots;
    std::array<MenuRect,2> teams;
    std::array<MenuTexture,9> frame;
    std::array<std::array<MenuTexture,3>,3> buttons;
    std::array<MenuTexture,3> close;
    MenuTexture pin;
    std::string currentLabel;
    std::array<std::string,2> teamNames;
    std::string previousLabel,nextLabel,font;
    std::unordered_map<std::string,std::array<int,4>> colors;
    std::unordered_map<unsigned,CharacterDescription> descriptions;
};
CharacterMenu ParseCharacterMenu(std::string_view json);
}
