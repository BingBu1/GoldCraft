// SPDX-License-Identifier: GPL-3.0-or-later
// Offline resource closure using the same decoder, table reader and catalog
// as the GameDLL. Input is binary stdin so Windows path encodings are irrelevant.
#include "config_codec.hpp"
#include "scenario/config.hpp"
#include "scenario/round_settings.hpp"
#include "scenario_music.hpp"
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <iterator>
#include <set>

int main(int argc, char** argv)
{
    if(argc!=2)return 2;
    _setmode(_fileno(stdin),_O_BINARY);
    std::string input((std::istreambuf_iterator<char>(std::cin)),{});
    std::set<std::string> paths;
    const auto add=[&](std::string_view filename)
    {
        const auto path=csol::music::ResourcePath(filename);
        if(!path)return false;
        paths.insert(*path);return true;
    };
    if(std::string_view(argv[1])=="path")
    {
        if(!add(input))return 1;
    }
    else if(std::string_view(argv[1])=="round")
    {
        if(input.empty()||input.size()>8*1024*1024||
            !csol::DecodeConfiguration(std::as_writable_bytes(std::span(input.data(),input.size()))))return 1;
        while(!input.empty()&&!input.back())input.pop_back();
        csol::scenario::RoundTable table;
        csol::scenario::ConfigError error;
        if(!csol::scenario::ReadRounds(input,table,error))
        {std::cerr<<error.message<<'\n';return 1;}
        if(!add("Scenario_Ready.mp3"))return 1;
        for(std::size_t i=0;i<table.rows.size();++i)
        {
            csol::scenario::RoundSettings settings;
            if(!csol::scenario::ReadRoundSettings(table,i,settings,error))return 1;
            const auto track=csol::music::FindTrack(settings.music);
            if(!track){std::cerr<<"Unsupported music ID "<<unsigned(settings.music)<<'\n';return 1;}
            if(!track->filename.empty()&&!add(track->filename))return 1;
        }
    }
    else return 2;
    for(const auto& path:paths)std::cout<<path<<'\n';
}
