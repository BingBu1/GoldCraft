// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the exact Session resource statements with the real SDK callbacks.
#include "precompiled.h"
#include "../../native/csol/scenario_respawn_resources.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
namespace fixture
{
unsigned checks{},callbackCount{},cancelAt{};
bool replaceMap{};
std::string missing;
std::vector<std::string> sizes,generics,sounds;
void Check(bool condition,const char* label){++checks;if(!condition)throw std::runtime_error(label);}
struct Map
{
    bool allowed{true};std::shared_ptr<int> token=std::make_shared<int>(1);
    bool EntityAccessAllowed()const{return allowed;}
    auto EntityAccess()const{return token;}
};
struct Error { std::string path; struct Detail {unsigned line;std::string text;} detail; };
struct Session
{
    Map m_map;Error error;
    bool Fail(Error value){error=std::move(value);return false;}
    bool Precache()
    {
        const auto access=m_map.EntityAccess();
        std::set<std::string> hudPaths;
#include "actual_precache.inc"
        return true;
    }
};
Session* active{};
void Boundary()
{
    if(++callbackCount==cancelAt)
    {
        if(replaceMap)active->m_map.token=std::make_shared<int>(2);
        else active->m_map.allowed=false;
    }
}
int FileSize(const char* path){sizes.emplace_back(path);Boundary();return missing==path?0:128;}
int Generic(const char* path){generics.emplace_back(path);Boundary();return int(generics.size());}
int Sound(const char* path){sounds.emplace_back(path);Boundary();return int(sounds.size());}
void Reset(Session& session)
{
    session={};active=&session;sizes.clear();generics.clear();sounds.clear();missing.clear();
    callbackCount=cancelAt=0;replaceMap=false;
    g_engfuncs={};g_engfuncs.pfnGetFileSize=FileSize;g_engfuncs.pfnPrecacheGeneric=Generic;g_engfuncs.pfnPrecacheSound=Sound;
}
}
int main()
{
    try
    {
        using namespace fixture;namespace ui=csol::scenario_respawn_ui;Session session;
        Reset(session);Check(session.Precache(),"actual Session revival resources precache successfully");
        std::set<std::string> expectedImages{ui::ItemSprite,ui::LocalizationPath,ui::CenterBackground.path};
        for(const auto& image:ui::Outcomes)expectedImages.insert(image.path);
        for(const auto& image:ui::RetryImages)expectedImages.insert(image.path);
        Check(expectedImages.size()==16&&std::set(generics.begin(),generics.end())==expectedImages&&generics.size()==16,
            "actual Session registers every revival/retry generic once");
        Check(sounds.size()==11&&sizes.size()==27,"actual Session performs filesystem checks for all 27 resources");
        for(unsigned i=0;i<ui::RetrySounds.size();++i)
            Check(sounds[i]==std::string(ui::RetrySounds[i]).substr(6)&&sizes[16+i]==ui::RetrySounds[i],
                "filesystem uses sound root and actual sound precache strips exactly that prefix");
        const auto totalCallbacks=callbackCount;
        for(bool replacement:{false,true})for(unsigned cancel=1;cancel<=totalCallbacks;++cancel)
        {
            Reset(session);cancelAt=cancel;replaceMap=replacement;
            Check(!session.Precache(),"map retirement/replacement aborts resource pass");
            Check(callbackCount==cancel,"no further filesystem/precache callback occurs after lost ownership");
            Check(session.error.path.empty(),"map retirement does not mark a replacement map failed");
        }
        for(const auto& name:expectedImages)
        {
            Reset(session);missing=name;
            Check(!session.Precache()&&session.error.path==name,"missing generic fails with its exact resource path");
            Check(std::find(generics.begin(),generics.end(),name)==generics.end()&&sounds.empty(),
                "missing generic never reaches engine registration or later audio");
        }
        for(unsigned i=0;i<ui::RetrySounds.size();++i)
        {
            Reset(session);missing=ui::RetrySounds[i];
            Check(!session.Precache()&&session.error.path==missing&&sounds.size()==i,
                "missing retry sound fails before its precache and later sounds");
        }
        std::cout<<"scenario retry precache checks="<<checks<<"\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<fixture::checks<<": "<<error.what()<<'\n';return 1;}
}
