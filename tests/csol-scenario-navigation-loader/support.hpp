// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-difficulty/support.hpp"
#include "../../native/csol/scenario/navigation.hpp"

namespace nav_loader_test
{
using difficulty_test::Check;
using difficulty_test::Require;
using difficulty_test::Read;
using difficulty_test::Bytes;
using difficulty_test::Files;
using csol::scenario::NavAreaRecord;
using csol::scenario::NavigationFile;
using csol::scenario::ConfigError;
using csol::scenario::ReadNavigationV5;

// A fixture encoder for the documented v5 wire layout. Expected behavior is
// asserted from authored records and the separately inventoried real map.
struct Binary
{
    std::string bytes;
    void U8(unsigned value) { bytes.push_back(static_cast<char>(value)); }
    void U16(unsigned value) { U8(value); U8(value >> 8); }
    void U32(std::uint32_t value) { U16(value); U16(value >> 16); }
    void Float(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void Point(const csol::scenario::NavPoint& p) { Float(p.x); Float(p.y); Float(p.z); }
};
inline NavAreaRecord Area(std::uint32_t id, float x=0, float y=0, float z=0, float half=100)
{
    NavAreaRecord result;
    result.id=id; result.low={x-half,y-half,z}; result.high={x+half,y+half,z};
    result.northEastZ=result.southWestZ=z; return result;
}
inline std::string Encode(const NavigationFile& file)
{
    Binary b; b.U32(0xfeedfaceu); b.U32(5); b.U32(file.bspSize);
    b.U16(static_cast<unsigned>(file.places.size()));
    for(const auto& place:file.places) { b.U16(static_cast<unsigned>(place.size()+1)); b.bytes+=place; b.U8(0); }
    b.U32(static_cast<std::uint32_t>(file.areas.size()));
    for(const auto& area:file.areas)
    {
        b.U32(area.id); b.U8(area.attributes); b.Point(area.low); b.Point(area.high);
        b.Float(area.northEastZ); b.Float(area.southWestZ);
        for(const auto& links:area.connections) { b.U32(static_cast<std::uint32_t>(links.size())); for(auto id:links)b.U32(id); }
        b.U8(static_cast<unsigned>(area.hidingSpots.size()));
        for(const auto& spot:area.hidingSpots) { b.U32(spot.id); b.Point(spot.position); b.U8(spot.flags); }
        b.U8(static_cast<unsigned>(area.approaches.size()));
        for(const auto& a:area.approaches) { b.U32(a.here); b.U32(a.previous); b.U8(a.previousHow); b.U32(a.next); b.U8(a.nextHow); }
        b.U32(static_cast<std::uint32_t>(area.encounters.size()));
        for(const auto& e:area.encounters)
        {
            b.U32(e.from); b.U8(e.fromDirection); b.U32(e.to); b.U8(e.toDirection); b.U8(static_cast<unsigned>(e.spots.size()));
            for(const auto& spot:e.spots) { b.U32(spot.id); b.U8(spot.position); }
        }
        b.U16(area.place);
    }
    return b.bytes;
}
inline NavigationFile RichFile()
{
    NavigationFile file; file.bspSize=321; file.places={"known","unknown","duplicate"};
    file.areas={Area(41),Area(42,150),Area(43,350)};
    auto& a=file.areas[0]; a.attributes=2; a.place=1;
    a.connections[0]={42,42,0,999};
    a.hidingSpots={{71,{1,2,3},5},{72,{4,5,6},2}};
    a.approaches={{41,0,42,7,1},{42,41,999,0,6}};
    a.encounters={{41,42,0,1,{{71,0},{72,255},{0,128}}}};
    file.areas[1].place=2; file.areas[2].place=3;
    return file;
}
inline void RejectPreserving(const std::string& bytes,const char* label)
{
    NavigationFile output; output.bspSize=0xabcdef12; output.places={"unchanged"}; output.areas={Area(987)}; output.trailingBytes=73;
    ConfigError error;
    Check(!ReadNavigationV5(Bytes(bytes),output,error),label);
    Check(output.bspSize==0xabcdef12 && output.places==std::vector<std::string>{"unchanged"} && output.areas.size()==1 && output.areas[0].id==987 && output.trailingBytes==73,"failure does not publish partial navigation");
    Check(!error.message.empty() && error.byteOffset<=bytes.size(),"failure includes bounded byte offset and reason");
}
void ParserCases(const char* actualNav);
void OwnerCases(const char* actualNav);
void LifetimeCases();
void LadderCases();
void LadderCancellationCases();
void SessionCases();
}
