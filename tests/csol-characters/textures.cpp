// SPDX-License-Identifier: GPL-3.0-or-later
// Independent production DXT decoder versus the offline Pillow conversion.
#include "../../native/csol/sprite_codec.cpp"
#include "../../native/csol/image_codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rapidjson/document.h>
std::vector<std::byte> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(p.string());auto n=f.tellg();std::vector<std::byte>b(size_t(n),std::byte{});f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);return b;}
int main(int argc,char**argv){try{
    if(argc!=4)return 1;auto bytes=Read(argv[1]);rapidjson::Document d;d.Parse(reinterpret_cast<char*>(bytes.data()),bytes.size());
    unsigned count=0;size_t pixels=0;unsigned maxDifference=0;
    for(const auto& row:d["conversions"].GetArray()){
        auto original=Read(std::filesystem::path(argv[2])/row["source"].GetString());csol::Reader reader{original};
        auto expected=csol::DDS(reader,row["width"].GetUint(),row["height"].GetUint(),true);
        auto actual=csol::DecodeTGA(Read(std::filesystem::path(argv[3])/row["target"].GetString()));
        if(expected.RGBA.size()!=actual.RGBA.size())throw std::runtime_error("image size mismatch");
        for(size_t i=0;i<actual.RGBA.size();++i){const auto delta=unsigned(std::abs(int(expected.RGBA[i])-int(actual.RGBA[i])));maxDifference=std::max(delta,maxDifference);if(delta)throw std::runtime_error("decoded DDS pixel mismatch");}
        ++count;pixels+=actual.RGBA.size()/4;
    }
    std::cout<<"DDS conversion: "<<count<<" images, "<<pixels<<" pixels, maximum difference "<<maxDifference<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
