// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "character_menu.hpp"
#include <cstdint>
#include <vector>

namespace csol::scenario::map_vote {
inline constexpr char PresentationResource[]="models/csol/mapvote_v1.json";
struct MapArtwork {
    std::uint16_t id{};
    std::string mapName,name,description;
    MenuTexture thumbnail,detail;
};
struct Presentation {
    int width{},height{},fontSize{},fontWeight{},columns{},rows{},strideX{},strideY{};
    std::string font,fontResource;
    MenuRect list,info,preview,confirm,close,scrollbar;
    MenuRect card,cardImage,cardLabel;
    std::array<std::array<MenuTexture,9>,3> cards;
    std::array<MenuTexture,9> frame,infoBackground;
    std::array<std::array<MenuTexture,3>,3> buttons;
    std::array<MenuTexture,3> closeImages;
    std::array<MenuTexture,3> scrollThumb;
    std::vector<MapArtwork> maps;
    const MapArtwork* Find(std::uint16_t id) const;
};
Presentation ParsePresentation(std::string_view json);
std::vector<std::string> Resources(const Presentation& presentation);
}
