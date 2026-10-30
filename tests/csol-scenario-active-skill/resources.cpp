// SPDX-License-Identifier: GPL-3.0-or-later
// Narrow production HUD precache block. Full Session lifecycle is separate.
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../../native/csol/scenario_upgrades.hpp"
#include "../../native/csol/scenario_abilities.hpp"
namespace
{
struct Error { std::string path; struct { int line{}; std::string message; } detail; };
struct Map
{
    bool allowed{true}; int identity{1};
    bool EntityAccessAllowed() const { return allowed; }
    int EntityAccess() const { return identity; }
};
struct Files
{
    std::map<std::string,int> sizes;
    std::vector<std::string> read, generic, sounds;
    std::function<void(const char*,bool)> callback;
} files;
int Size(const char* path)
{
    files.read.emplace_back(path);
    if(files.callback) files.callback(path,false);
    auto entry=files.sizes.find(path);
    return entry==files.sizes.end() ? -1 : entry->second;
}
int Generic(const char* path)
{
    files.generic.emplace_back(path);
    if(files.callback) files.callback(path,true);
    return 1;
}
int Sound(const char* path)
{
    files.sounds.emplace_back(path);
    if(files.callback) files.callback(path,true);
    return 1;
}
struct Engine { int (*pfnGetFileSize)(const char*){Size}; } g_engfuncs;
#define PRECACHE_GENERIC(path) Generic(path)
#define PRECACHE_SOUND(path) Sound(path)
struct Session
{
    Map m_map;
    Error error;
    bool Fail(Error value) { error=std::move(value); return false; }
    bool PrecacheHud()
    {
        const auto access=m_map.EntityAccess();
#include "actual_hud_resources.inc"
        return true;
    }
};
int resourceChecks{};
void Verify(bool value,const char* text)
{
    ++resourceChecks;
    if(!value) { std::cerr<<"FAIL HUD resource "<<resourceChecks<<": "<<text<<'\n'; std::exit(1); }
}
std::set<std::string> Paths()
{
    std::set<std::string> paths{csol::upgrades::FontPath,"sprites/640hud8.spr"};
    for(const auto& item:csol::upgrades::Resources) paths.emplace(item.path);
    for(const auto* item:csol::abilities::DownloadResources) paths.emplace(item);
    return paths;
}
void Reset()
{
    files={};
    for(const auto& path:Paths()) files.sizes[path]=37;
    files.sizes[std::string("sound/")+csol::upgrades::SoundPath]=19;
}
}
int RunResourceTests()
{
    const auto paths=Paths(); const std::string sound=std::string("sound/")+csol::upgrades::SoundPath;
    Reset(); Session normal;
    Verify(normal.PrecacheHud(),"complete HUD asset closure registers");
    Verify(std::set<std::string>(files.generic.begin(),files.generic.end())==paths && files.generic.size()==paths.size(),
        "every unique UI image, font, and native digit sprite is registered once");
    Verify(files.sounds==std::vector<std::string>{csol::upgrades::SoundPath},"upgrade sound uses sound-relative registration");
    auto all=paths; all.emplace(sound);
    for(const auto& path:all) for(int absent:{-1,0})
    {
        Reset(); Session session; files.sizes[path]=absent;
        Verify(!session.PrecacheHud() && session.error.path==path && !session.error.detail.message.empty(),
            "each missing or empty UI resource fails explicitly at that exact path");
        Verify(std::find(files.generic.begin(),files.generic.end(),path)==files.generic.end() && files.sounds.empty(),
            "missing asset is never sent to engine precache and no later sound is reached");
    }
    for(const auto& path:all) for(bool duringPrecache:{false,true}) for(bool replaceIdentity:{false,true})
    {
        Reset(); Session session; std::size_t callbacks{},atRetirement{}; bool retired{};
        files.callback=[&](const char* name,bool precache)
        {
            ++callbacks;
            const auto actual=precache && std::string(name)==csol::upgrades::SoundPath ? "sound/"+std::string(name) : std::string(name);
            if(actual==path && precache==duringPrecache)
            {
                retired=true; atRetirement=callbacks;
                if(replaceIdentity) ++session.m_map.identity; else session.m_map.allowed=false;
            }
        };
        Verify(!session.PrecacheHud() && retired && session.error.detail.message.empty(),
            "engine callback retirement cancels without reporting a missing asset");
        Verify(callbacks==atRetirement,"retired session makes no later filesystem or precache callback");
    }
    return resourceChecks;
}
