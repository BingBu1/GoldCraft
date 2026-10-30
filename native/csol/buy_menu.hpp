// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "character_menu.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace csol::buy {
inline constexpr char Message[]="CSOBuy";
inline constexpr std::size_t MessageBytes=16;
enum class Kind:std::uint8_t {Capability,Begin,Item,End,Result};
enum class Result:std::uint8_t {Available,Unavailable,Insufficient,Success,Failed};
struct Packet {Kind kind{};std::uint16_t request{},id{};std::int32_t price{},balance{};Result result{};std::uint8_t team{};};
std::array<std::uint8_t,MessageBytes> Encode(const Packet& packet);
bool Decode(std::span<const std::uint8_t> bytes,Packet& packet);
struct Item {std::uint16_t id;int category;const char* alias;const char* name;const char* image;int extendedPrice;};
std::span<const Item> Catalog();
const Item* Find(int id);
struct Presentation {
    int width{},height{};
    std::string title,font;
    MenuRect info,previous,next,quit,account;
    MenuRect search{56,22,90,12},rebuy{219,281,150,25};
    std::array<MenuRect,2> teams{{{14,37,68,15},{82,37,70,15}}};
    std::array<MenuRect,8> favorites;
    std::array<MenuTexture,2> slotBackground;
    MenuTexture keyboard,favoriteBackground;
    std::array<MenuRect,10> slots;
    std::array<MenuTexture,9> frame;
    std::array<std::array<MenuTexture,3>,3> buttons;
    std::array<MenuTexture,3> close;
    std::unordered_map<std::string,std::array<int,4>> colors;
    std::unordered_map<int,MenuTexture> images;
};
using Favorites=std::array<std::uint16_t,8>;
Favorites ParseFavorites(std::string_view text);
std::string SerializeFavorites(const Favorites& favorites);
bool InTeamList(const Item& item,int team);
Presentation ParsePresentation(std::string_view json);
std::vector<std::string> Resources(const Presentation& presentation);
}
