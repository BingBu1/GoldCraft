// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/hud_style.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    try
    {
        std::ifstream input(argv[1],std::ios::binary);
        if(!input)throw std::runtime_error("cannot open HUD catalog");
        const std::string json{std::istreambuf_iterator<char>{input},{}};
        for(const auto& path:csol::hud_style::ResourcePaths(csol::hud_style::ParseCatalog(json)))
            std::puts(path.c_str());
    }
    catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
