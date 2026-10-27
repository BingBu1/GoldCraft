// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/character_catalog.hpp"
#include "../../native/csol/character_protocol.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
int checks{};
void Check(bool ok){++checks;if(!ok)throw std::runtime_error("character check "+std::to_string(checks));}
std::string Serialize(const rapidjson::Document& d){rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer>w(b);d.Accept(w);return {b.GetString(),b.GetSize()};}
int main(int argc,char** argv)
{
    try {
        Check(argc==2);std::ifstream f(argv[1]);const std::string json{std::istreambuf_iterator<char>(f),{}};
        const auto characters=csol::ParseCharacters(json);Check(characters.size()==151);
        std::set<std::string> models;
        for(const auto& c:characters){
            Check(c.ModelPath().size()<64);models.insert(c.model);
            for(int team=0;team<=3;++team)for(bool human:{false,true})
                Check(csol::CharacterTeamAllowed(c,team,human)==((team==1||team==2)&&(human||team==c.team)));
            for(unsigned slot=1;slot<=32;++slot){
                csol::CharacterState original{std::uint8_t(slot),1,c.id,0x87654321},decoded;
                const auto bytes=csol::EncodeCharacter(original);
                Check(csol::DecodeCharacter(bytes,decoded)&&decoded==original);
            }
        }
        Check(models.size()==150);
        auto* red=csol::FindCharacter(characters,39);auto* blue=csol::FindCharacter(characters,40);
        Check(red&&blue&&red->model==blue->model&&red->skin==0&&blue->skin==1);
        Check(!csol::FindCharacter(characters,0)&&!csol::FindCharacter(characters,65535));
        for(int invalid=0;invalid<8;++invalid){
            rapidjson::Document d;d.Parse(json.data(),json.size());auto& r=d["characters"][0];
            switch(invalid){
            case 0:r["id"].SetInt(0);break;
            case 1:d["characters"][1]["id"].SetInt(r["id"].GetInt());break;
            case 2:r["team"].SetInt(3);break;
            case 3:r["model"].SetString("../urban");break;
            case 4:r["portrait"].SetString("gfx/csol/p/../../a.tga");break;
            case 5:r["portrait"].SetString("gfx/csol/p/a\0.tga",sizeof("gfx/csol/p/a\0.tga")-1);break;
            case 6:r["handStyle"].SetInt(-75);r["hand"].SetString("");break;
            case 7:d["version"].SetInt(2);break;
            }
            bool rejected=false;try{csol::ParseCharacters(Serialize(d));}catch(const std::exception&){rejected=true;}Check(rejected);
        }
        csol::CharacterState decoded;auto bytes=csol::EncodeCharacter({32,1,168,22});
        for(int length=0;length<10;++length)Check(!csol::DecodeCharacter({bytes.data(),size_t(length)},decoded));
        for(auto [offset,value]:{std::pair{0,2},std::pair{1,33},std::pair{2,2},std::pair{3,1}}){auto bad=bytes;bad[offset]=value;Check(!csol::DecodeCharacter(bad,decoded));}
        Check(csol::DecodeCharacter(csol::EncodeCharacter({}),decoded));
        Check(!csol::DecodeCharacter(csol::EncodeCharacter({0,1,168,22}),decoded));
        Check(!csol::DecodeCharacter(csol::EncodeCharacter({1,1,168,0}),decoded));
        std::cout<<"Character catalog/protocol checks: "<<checks<<" passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
