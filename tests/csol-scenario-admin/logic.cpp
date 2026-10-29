// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/map_catalog.hpp"
#include "../../native/csol/scenario/map_resources.hpp"
#include "../csol-scenario-rules-session/resources.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace csol::scenario;
namespace {
int checks{}, failures{};
void Check(bool ok, const char* text) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
void Require(bool ok, const char* text) { Check(ok,text); if (!ok) throw std::runtime_error(text); }
std::string Join(const std::vector<std::string>& fields)
{ std::string out; for (const auto& f:fields) { if (!out.empty()) out+=','; out+=f; } return out+"\r\n"; }
std::string Catalog(const char* cap, const char* column="ZSmaxDifficulty", bool duplicate=false, bool ambiguous=false)
{
    std::vector<std::string> header(103), row(103);
    header[0]=";map_id";header[1]="map_name";header[2]="map_filename";header[58]="SubType";header[102]=column;
    row[0]="98";row[1]="fixture";row[2]="map_session_test";row[58]="1";row[102]=cap;
    std::string out=Join(header)+Join(row);
    if(duplicate){row[102]="2";out+=Join(row);}
    if(ambiguous){row[0]="99";row[102]="3";out+=Join(row);}
    return out;
}
struct Files {
    static inline Files* self{};
    std::map<std::string,std::string> data=session_test::Resources();
    std::map<void*,std::size_t> live;
    std::vector<std::string> loads;
    Files(){Require(!self,"one resource owner");self=this;}
    ~Files(){Check(live.empty(),"all engine file allocations released");self=nullptr;}
    static unsigned char* Load(const char* path,int* size){auto& f=*self;f.loads.emplace_back(path);auto it=f.data.find(path);if(it==f.data.end()){*size=0;return nullptr;}*size=static_cast<int>(it->second.size());auto* p=new unsigned char[*size+1];std::memcpy(p,it->second.data(),*size);p[*size]=0;f.live.emplace(p,*size);return p;}
    static void Free(void* p){Require(self->live.erase(p)==1,"file release exactly once");delete[] static_cast<unsigned char*>(p);}
    ResourceFileApi Api(){return {Load,Free};}
};
void CatalogCases(const char* common)
{
    MapCatalog catalog;ConfigError error;
    struct Case{const char* text;int limit;};
    for(const auto& c:{Case{"",6},{"1",1},{"6",6},{"7",7},{"0",7},{"-8",7},{" +6 ",6}})
    {Require(ReadMapCatalog(Catalog(c.text),catalog,error),"authored catalog parses");int value=33;Check(catalog.DifficultyLimit(98,value,error)&&value==c.limit&&error.message.empty(),"original limit/default/service fallback semantics");}
    for(const char* text:{"8","2147483648","1x","1.5","+","+-1","--1"})
    {Require(ReadMapCatalog(Catalog(text),catalog,error),"bad difficulty remains a catalog field");int value=33;Check(!catalog.DifficultyLimit(98,value,error)&&value==33&&!error.message.empty(),"invalid cap rejected without output mutation");}
    Require(ReadMapCatalog(Catalog("6","wrong"),catalog,error),"non-difficulty columns still parse");int value=33;
    Check(!catalog.DifficultyLimit(98,value,error)&&value==33,"nonempty limit requires original column identity");
    Require(ReadMapCatalog(Catalog("6","ZSmaxDifficulty",true),catalog,error),"duplicate map IDs parse");
    Check(catalog.DifficultyLimit(98,value,error)&&value==6,"first reachable numeric map row owns cap");
    std::ifstream stream(common,std::ios::binary);Require(bool(stream),"original extracted common catalog available");
    const std::string original{std::istreambuf_iterator<char>(stream),{}};
    Require(ReadMapCatalog(original,catalog,error),"original common maplist parses");
    Check(catalog.UniqueMapId("zs_deadend")==98&&catalog.DifficultyLimit(98,value,error)&&value==6,
        "actual common zs_deadend numeric map98 permits six grades");
}
void ResourceCases()
{
    Files files;MapResources map;ResourceError error;files.data["resource/maplist.csv"]=Catalog("6");
    Require(map.BeginMapFromCatalog("map_session_test",files.Api(),error),"catalog session initializes");
    Check(map.Tables()->mapId==98&&map.Tables()->difficultyLimit==6,"identity and cap captured together");
    files.data["resource/maplist.csv"]=Catalog("3");files.loads.clear();
    Require(map.Reload(files.Api(),error),"table reload succeeds");
    Check(map.Tables()->difficultyLimit==6&&std::find(files.loads.begin(),files.loads.end(),"resource/maplist.csv")==files.loads.end(),"reload retains cap snapshot without rereading changed catalog");
    const auto* previous=map.Tables();files.data.erase("maps/zbs_map_session_test.cso");
    Check(!map.Reload(files.Api(),error)&&map.Tables()==previous&&map.Tables()->difficultyLimit==6,"failed table reload preserves working snapshot and cap");
    files.data["maps/zbs_map_session_test.cso"]=session_test::Unhex(fixtures::rounds);
    Require(map.BeginMapFromCatalog("map_session_test",files.Api(),error),"complete new map reload succeeds");
    Check(map.Tables()->difficultyLimit==3,"new complete map session adopts changed catalog cap");
    map.Clear();Check(!map.Tables()&&map.MapName().empty()&&!map.Reload(files.Api(),error),"clear removes map and cap authority");
    Require(map.BeginMap("map_session_test",files.Api(),error,1,98),"explicit subtype session still supported");
    Check(map.Tables()->mapId==98&&!map.Tables()->difficultyLimit,"numeric ID alone does not invent catalog authority");
    files.data["resource/maplist.csv"]=Catalog("6","ZSmaxDifficulty",false,true);
    Require(map.BeginMapFromCatalog("map_session_test",files.Api(),error),"matching subtype ambiguous IDs allow resource load");
    Check(!map.Tables()->mapId&&!map.Tables()->difficultyLimit,"ambiguous map IDs expose no admin cap");
    files.data["resource/maplist.csv"]=Catalog("8");
    Check(!map.BeginMapFromCatalog("map_session_test",files.Api(),error)&&!map.Tables()&&error.path=="resource/maplist.csv","invalid new cap cannot retain old session");
}
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    try{CatalogCases(argv[1]);ResourceCases();}
    catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"scenario-admin logic "<<checks<<" checks "<<failures<<" failures\n";
    return failures?1:0;
}
