// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/buy_menu.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
unsigned checks{};
void Check(bool value,const char* reason)
{
    ++checks;
    if(!value)throw std::runtime_error(reason);
}
std::string Read(const char* path)
{
    std::ifstream stream(path,std::ios::binary);
    Check(bool(stream),"open generated presentation");
    return {std::istreambuf_iterator<char>(stream),{}};
}
std::string Json(const rapidjson::Document& document)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    return {buffer.GetString(),buffer.GetSize()};
}
void Reject(const rapidjson::Document& document)
{
    bool rejected=false;
    try{csol::buy::ParsePresentation(Json(document));}
    catch(const std::exception&){rejected=true;}
    Check(rejected,"malformed menu rejected by production parser");
}
}

int main(int argc,char** argv)
{
    try{
        Check(argc==3,"supply actual generated purchase and character presentations");
        const auto buyJson=Read(argv[1]);
        const auto buy=csol::buy::ParsePresentation(buyJson);
        Check(buy.normalizedLayout&&buy.root.x==17&&buy.root.y==22&&buy.width==606&&buy.height==444,"current final purchase root");
        Check(buy.info.x==216&&buy.info.y==56&&buy.info.w==152&&buy.info.h==220,"code-created WeaponInfo bounds");
        Check(buy.favoritePanel.x==372&&buy.favoritePanel.y==30&&buy.favorites[0].y==26,"favorites retain actual parent-local bounds");
        Check(buy.tabs.size()==6&&buy.tabs[2][0].path!=buy.tabs[2][1].path,"six original tab groups exported");
        const auto& info=buy.weaponInfo;
        for(const auto& [id,item]:info.items){
            Check(csol::buy::Find(id)!=nullptr&&item.itemId!=0,"host identity maps to original ItemID");
            Check(!item.name.empty()&&!item.name.starts_with("CSO_Item_"),"original localized base item name");
            Check(!item.description.empty()&&!item.description.starts_with("CSO_Item_"),"original localized item description");
        }
        for(const auto [id,capacity]:{std::pair{144,"20"},{197,"150"},{96,"120"},{130,"7"},{312,"7"},{142,"35"},{112,"24"},{203,"7"},{220,"30"},{725,"80"},{726,"80"}}){
            const auto& item=info.items.at(id);
            Check(item.table==true&&item.values[9]==capacity,"Load follows actual original weapon provider");
        }
        Check(info.items.at(144).itemId==428&&info.items.at(142).itemId==427,"duplicate InGameID does not choose unrelated ItemID");
        Check(!info.grades.contains(7),"empty original grade has no invented texture");
        const auto resources=csol::buy::Resources(buy);
        Check(std::is_sorted(resources.begin(),resources.end())&&std::adjacent_find(resources.begin(),resources.end())==resources.end(),"deterministic unique download closure");
        const auto buyRoot=std::filesystem::path(argv[1]).parent_path().parent_path().parent_path();
        for(const auto& resource:resources){
            if(!std::filesystem::is_regular_file(buyRoot/resource)||std::filesystem::file_size(buyRoot/resource)==0)
                throw std::runtime_error("advertised menu resource missing: "+resource);
            ++checks;
        }
        rapidjson::Document document;
        document.Parse(buyJson.data(),buyJson.size());
        document.RemoveMember("weaponInfo");Reject(document);
        document.Parse(buyJson.data(),buyJson.size());
        document["weaponInfo"]["items"][0]["table"].SetString("unknown",document.GetAllocator());Reject(document);
        document.Parse(buyJson.data(),buyJson.size());
        document["weaponInfo"]["items"][0]["itemId"].SetInt(0);Reject(document);
        document.Parse(buyJson.data(),buyJson.size());
        document["weaponInfo"]["items"][0]["grade"][0].SetInt(9);Reject(document);
        document.Parse(buyJson.data(),buyJson.size());
        document["weaponInfo"]["background"]["path"].SetString("gfx/csol/buy/../escape.tga",document.GetAllocator());Reject(document);
        document.Parse(buyJson.data(),buyJson.size());
        const int unknownTableItem=document["weaponInfo"]["items"][0]["id"].GetInt();
        document["weaponInfo"]["items"][0]["table"].SetNull();
        const auto unknownTable=csol::buy::ParsePresentation(Json(document));
        Check(!unknownTable.weaponInfo.items.at(unknownTableItem).table.has_value(),"unknown table branch remains representable");

        const auto characterJson=Read(argv[2]);
        const auto characters=csol::ParseCharacterMenu(characterJson);
        Check(characters.normalizedLayout&&characters.root.x==17&&characters.root.y==30&&characters.width==606&&characters.height==420,"current final class root");
        Check(characters.info.x==263&&characters.info.w==400&&characters.info.x+characters.info.w>characters.width,"original overflowing child is not squeezed");
        Check(characters.scenarioDescription.x==0&&characters.scenarioDescription.y==127&&characters.scenarioDescription.w==300&&characters.scenarioDescription.h==80,"original scenario description bounds");
        Check(characters.stats.maximum==30&&!characters.stats.gauge.path.empty(),"actual original gauge and stat domain");
        std::cout<<"PASS "<<checks<<" actual generated-menu checks\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
