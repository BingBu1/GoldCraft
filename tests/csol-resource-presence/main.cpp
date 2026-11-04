// SPDX-License-Identifier: GPL-3.0-or-later
// Calls unchanged production functions against the real SDK ABI; complete TUs
// are compiled separately before the focused entrypoint test is linked.
// The controlled VFS tracks payload reads separately from size/open checks.
#include "precompiled.h"
#include "../../native/csol/server/characters.h"
#include "../../native/csol/server/buy_menu.h"
#include "../../native/csol/hud_style.hpp"
#include "../../native/csol/buy_menu.hpp"
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
bool CSOL_UsesScenarioRules() { return true; }
namespace {
struct File { std::string text; int size; bool document; };
std::map<std::string,File> files;
std::map<std::string,int> loads, sizes, advertisements;
std::set<unsigned char*> buffers;
std::string alerts;
unsigned checks{}, failures{};
void Check(bool ok,const char* label)
{ ++checks; if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';} }
int Size(const char* path)
{ ++sizes[path];auto it=files.find(path);return it==files.end()?-1:it->second.size; }
unsigned char* Load(const char* path,int* length)
{
    ++loads[path];auto it=files.find(path);
    if(it==files.end()){*length=0;return nullptr;}
    *length=it->second.size;
    const auto& text=it->second.text;
    auto* bytes=new unsigned char[text.size()+1];
    std::memcpy(bytes,text.data(),text.size());bytes[text.size()]=0;
    buffers.insert(bytes);return bytes;
}
void Free(void* pointer)
{ Check(buffers.erase(static_cast<unsigned char*>(pointer))==1,"owned file buffer freed once");delete[] static_cast<unsigned char*>(pointer); }
void Alert(ALERT_TYPE,const char* format,...)
{ char text[2048];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);alerts+=text; }
int Generic(const char* path) { ++advertisements[path];return int(advertisements.size()); }
int Sound(const char* path) { return Generic((std::string("sound/")+path).c_str()); }
void ObserveReset()
{ Check(buffers.empty(),"no retained full-file allocations");loads.clear();sizes.clear();advertisements.clear();alerts.clear(); }
void NoAssetReads()
{ for(const auto& [path,count]:loads)Check(!count||!files.contains(path)||files.at(path).document,"presence validation never reads asset body"); }
void Put(std::string path,std::string body,bool document=false)
{ const auto size=int(body.size());files[std::move(path)]={std::move(body),size,document}; }
std::string Read(const std::filesystem::path& path)
{std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("fixture missing");std::ostringstream text;text<<stream.rdbuf();return text.str();}
void CharacterFixture()
{
    files.clear();ObserveReset();
    Put("models/csol/characters.json",R"({"version":1,"characters":[{"id":1,"team":1,"skin":0,"male":true,"allowPlayerStyle":true,"handStyle":0,"name":"fixture","model":"test","hand":"","portrait":"gfx/csol/p/1.tga"}]})",true);
    Put("shader/csol/characters.txt","models/player/test/test.mdl\ngfx/csol/p/1.tga\nsound/test.wav\nshader/test.txt\n",true);
    for(const char* path:{"models/player/test/test.mdl","gfx/csol/p/1.tga","sound/test.wav","shader/test.txt"})Put(path,"resource");
}
void Characters()
{
    CharacterFixture();CSOL_PrecacheCharacters();
    Check(alerts.find("Characters ready: 1")!=alerts.npos,"valid actual character catalog accepted");
    Check(advertisements.size()==4,"all character resource types advertised");NoAssetReads();
    ObserveReset();files.erase("models/player/test/test.mdl");CSOL_PrecacheCharacters();
    Check(alerts.find("missing character dependency")!=alerts.npos&&advertisements.empty(),"missing model on subsequent map fails before any manifest publication");
    CharacterFixture();files.at("shader/test.txt").size=0;CSOL_PrecacheCharacters();
    Check(alerts.find("Characters ready: 1")!=alerts.npos,"empty character manifest asset retains legacy existence semantics");NoAssetReads();
    CharacterFixture();Put("shader/csol/characters.txt","../escape.mdl\n",true);CSOL_PrecacheCharacters();
    Check(alerts.find("invalid character download manifest")!=alerts.npos&&advertisements.empty()&&!sizes.contains("../escape.mdl"),"invalid manifest path rejected before VFS query");
}
void Hud(const std::string& document)
{
    const auto paths=csol::hud_style::ResourcePaths(csol::hud_style::ParseCatalog(document));
    for(int mode=0;mode<3;++mode){
        CharacterFixture();Put(csol::hud_style::CatalogPath,document,true);
        for(const auto& path:paths)if(path!=csol::hud_style::CatalogPath)Put(path,"image");
        const auto bad=*std::find_if(paths.begin(),paths.end(),[](const auto& path){return path!=csol::hud_style::CatalogPath;});
        if(mode==1)files.erase(bad);else if(mode==2)files.at(bad).size=0;
        CSOL_PrecacheCharacters();
        Check(alerts.find("Characters ready: 1")!=alerts.npos,"optional HUD failure preserves characters");
        if(mode==0)for(const auto& path:paths)Check(advertisements.contains(path),"complete HUD path retained in downloads");
        else Check(alerts.find("missing HUD dependency")!=alerts.npos&&!advertisements.contains(bad),"missing and empty HUD assets reject optional style");
        NoAssetReads();
    }
}
void Menu(const std::string& document)
{
    const auto paths=csol::buy::Resources(csol::buy::ParsePresentation(document));
    const auto bad=*std::find_if(paths.begin(),paths.end(),[](const auto& path){return path!=csol::buy::PresentationResource;});
    for(int mode=0;mode<4;++mode){
        files.clear();ObserveReset();Put(csol::buy::PresentationResource,document,true);
        for(const auto& path:paths)if(path!=csol::buy::PresentationResource)Put(path,"image");
        if(mode==1)files.erase(bad);else if(mode==2)files.at(bad).size=0;
        else if(mode==3)files.erase(csol::buy::PresentationResource);
        const bool ready=CSOL_PrecacheBuyMenu();Check(ready==(mode==0),"menu accepts only complete nonempty closure");
        if(mode==0)for(const auto& path:paths)Check(advertisements[path]==1,"each menu resource advertised once");
        else Check(advertisements.empty(),"failed closure never publishes partial menu");
        NoAssetReads();
    }
}
}
int main(int argc,char** argv)
{
    try{
        if(argc!=3)throw std::runtime_error("HUD and menu fixture arguments required");
        g_engfuncs.pfnLoadFileForMe=Load;g_engfuncs.pfnFreeFile=Free;g_engfuncs.pfnGetFileSize=Size;
        g_engfuncs.pfnPrecacheModel=Generic;g_engfuncs.pfnPrecacheSound=Sound;g_engfuncs.pfnPrecacheGeneric=Generic;
        g_engfuncs.pfnAlertMessage=Alert;g_engfuncs.pfnRegUserMsg=[](const char*,int){return 230;};
        g_engfuncs.pfnMessageBegin=[](int,int,const float*,edict_t*){};
        g_engfuncs.pfnWriteByte=[](int){};g_engfuncs.pfnMessageEnd=[](){};
        Characters();Hud(Read(argv[1]));Menu(Read(argv[2]));ObserveReset();
    }catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"resource-presence checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
