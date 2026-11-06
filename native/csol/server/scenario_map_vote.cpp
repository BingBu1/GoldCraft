// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_map_vote.h"
#include "scenario_gameplay.h"
#include "../scenario/map_catalog.hpp"
#include "../map_vote_presentation.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>

namespace {
using namespace csol::scenario;
using namespace csol::scenario::map_vote;
Vote vote;
// Neither token is reset by map activation, so delayed commands cannot become
// valid again after a map change or same-map challenge replay.
std::uint32_t generation{};
std::uint64_t epoch{1};
int message{};
bool busy{},sending{},resourcesReady{};
struct Recipient { std::uint32_t token{},revision{};std::uint16_t seconds{};double sentAt{-1}; };
std::array<Recipient,MaxPlayers> recipients{};
constexpr std::array<std::pair<std::uint16_t,const char*>,7> seasonOne{{
    {74,"zs_lostcity"},{75,"zs_2gate"},{76,"zs_trap"},{77,"zs_lastclue"},
    {85,"zs_chaos"},{98,"zs_deadend"},{101,"zs_nightmare"}}};

std::uint32_t Identity(edict_t* entity)
{
    if (!entity || entity->free || !g_engfuncs.pfnGetPlayerUserId ||
        !(entity->v.flags&FL_CLIENT) || (entity->v.flags&(FL_FAKECLIENT|FL_PROXY|FL_KILLME))) return 0;
    auto* player=dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(entity));
    if (!player || player->has_disconnected) return 0;
    const auto id=GETPLAYERUSERID(entity);
    return id>0 ? std::uint32_t(id) : 0;
}
template<class T> bool Number(const char* text,T& value)
{
    if (!text || !*text) return false;
    const auto length=std::strlen(text);
    if (length>10) return false;
    const auto [end,error]=std::from_chars(text,text+length,value);
    return error==std::errc{} && end==text+length && value!=0;
}
bool Send(edict_t* entity,const Snapshot& snapshot)
{
    if (sending || message<64 || message>255 || !entity || entity->free ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd || !g_engfuncs.pfnWriteByte) return false;
    const auto bytes=Encode(snapshot);
    if (bytes.empty()) return false;
    // The complete packet is copied before callbacks. A hook may reset the
    // map's vote during Begin; still complete exactly this packet's End.
    sending=true;
    MESSAGE_BEGIN(MSG_ONE,message,nullptr,entity);
    for (const auto byte:bytes) WRITE_BYTE(std::to_integer<unsigned char>(byte));
    MESSAGE_END();
    sending=false;
    return true;
}
}

bool CSOL_PrecacheScenarioMapVote()
{
    if(busy||!g_engfuncs.pfnLoadFileForMe||!g_engfuncs.pfnFreeFile||!g_engfuncs.pfnPrecacheGeneric||!g_engfuncs.pfnRegUserMsg)return false;
    const auto startEpoch=epoch;
    struct Guard { Guard(){busy=true;}~Guard(){busy=false;} }guard;
    resourcesReady=false;
    const auto release=g_engfuncs.pfnFreeFile;int length{};
    std::unique_ptr<unsigned char,decltype(release)> bytes(
        g_engfuncs.pfnLoadFileForMe(PresentationResource,&length),release);
    if(epoch!=startEpoch||!bytes||length<=0||length>1024*1024)return false;
    std::vector<std::string> resources;
    try{resources=Resources(ParsePresentation({reinterpret_cast<const char*>(bytes.get()),std::size_t(length)}));}
    catch(const std::exception&){return false;}
    // Validate the complete closure before adding it to the download list.
    for(const auto& path:resources){
        int size{};std::unique_ptr<unsigned char,decltype(release)> item(
            g_engfuncs.pfnLoadFileForMe(path.c_str(),&size),release);
        if(epoch!=startEpoch||!item||size<=0)return false;
    }
    for(const auto& path:resources){
        PRECACHE_GENERIC(const_cast<char*>(path.c_str()));if(epoch!=startEpoch)return false;
    }
    const int registered=REG_USER_MSG(MessageName,-1);
    if(epoch!=startEpoch||registered<64||registered>255)return false;
    message=registered;resourcesReady=true;return true;
}
bool CSOL_StartScenarioMapVote(unsigned seconds)
{
    if (busy || !resourcesReady || vote.State()==Phase::Open || !gpGlobals || generation==UINT32_MAX ||
        !g_engfuncs.pfnLoadFileForMe || !g_engfuncs.pfnFreeFile ||
        !g_engfuncs.pfnIsMapValid || message<64 || message>255) return false;
    const std::string currentMap=STRING(gpGlobals->mapname);
    if (!ValidMapName(currentMap)) return false;
    const auto startEpoch=epoch;
    struct Guard { Guard(){busy=true;}~Guard(){busy=false;} } guard;
    const auto release=g_engfuncs.pfnFreeFile;
    int length{};
    std::unique_ptr<unsigned char,decltype(release)> bytes(
        g_engfuncs.pfnLoadFileForMe("resource/maplist.csv",&length),release);
    if (epoch!=startEpoch || !bytes || length<=0 || length>8*1024*1024) return false;
    MapCatalog catalog;ConfigError error;
    if (!ReadMapCatalog({reinterpret_cast<const char*>(bytes.get()),std::size_t(length)},catalog,error)) return false;
    std::vector<Choice> candidates;
    for (const auto& [id,name]:seasonOne)
    {
        const auto* entry=catalog.Find(id);
        int subtype{};
        if (!entry || entry->fileName!=name || catalog.UniqueMapId(name)!=id ||
            !catalog.ResolveScenarioType(name,subtype,error) ||
            !CCSOLScenarioGameplay::SupportsMap(name,subtype)) continue;
        const bool exists=IS_MAP_VALID(const_cast<char*>(name))!=FALSE;
        if (epoch!=startEpoch) return false;
        if (!exists) continue;
        MapResources map;ResourceError loadError;
        const bool loaded=map.BeginMapFromCatalog(name,
            {g_engfuncs.pfnLoadFileForMe,g_engfuncs.pfnFreeFile},loadError);
        if (epoch!=startEpoch) return false;
        if (loaded) candidates.push_back({id,name});
    }
    if (candidates.empty()) return false;
    if (!vote.Begin(generation+1,candidates,currentMap,gpGlobals->time,seconds)) return false;
    ++generation;recipients={};
    return true;
}

std::optional<Choice> CSOL_UpdateScenarioMapVote()
{
    if (busy || sending || !gpGlobals || vote.State()==Phase::Idle) return {};
    const auto activeEpoch=epoch;
    const double now=gpGlobals->time;
    if (!std::isfinite(now) || now<0) return {};
    struct Guard { Guard(){busy=true;}~Guard(){busy=false;} } guard;
    for (unsigned slot=1;slot<=MaxPlayers;++slot)
    {
        auto* entity=INDEXENT(slot);
        const auto token=Identity(entity);
        if (epoch!=activeEpoch) return {};
        auto& recipient=recipients[slot-1];
        if (recipient.token!=token)
        {
            if (recipient.token) vote.Disconnect(slot,recipient.token);
            recipient={};recipient.token=token;
            if (token) vote.Connect(slot,token);
        }
    }
    std::optional<Choice> winner;
    if (vote.State()==Phase::Open && vote.View(0,0,now).seconds==0)
    {
        if (!g_engfuncs.pfnRandomLong) return {};
        const auto tie=std::uint32_t(RANDOM_LONG(0,INT_MAX));
        if (epoch!=activeEpoch) return {};
        winner=vote.Finish(now,tie);
    }
    for (unsigned slot=1;slot<=MaxPlayers;++slot)
    {
        auto& recipient=recipients[slot-1];
        if (!recipient.token) continue;
        const auto snapshot=vote.View(slot,recipient.token,now);
        if (snapshot.revision==recipient.revision && snapshot.seconds==recipient.seconds) continue;
        // At most four reliable snapshots per second during the ballot. Phase
        // transitions bypass the throttle. User command spam cannot flood it.
        if (snapshot.phase==Phase::Open && recipient.sentAt>=0 && now-recipient.sentAt<0.25) continue;
        auto* entity=INDEXENT(slot);const auto token=Identity(entity);
        if (epoch!=activeEpoch) return {};
        if (token!=recipient.token) continue;
        if (Send(entity,snapshot))
        {
            if (epoch!=activeEpoch) return {};
            recipient.revision=snapshot.revision;recipient.seconds=snapshot.seconds;recipient.sentAt=now;
        }
    }
    return epoch==activeEpoch ? winner : std::nullopt;
}

bool CSOL_ScenarioMapVoteCommand(edict_t* entity,const char* command)
{
    if (!command || std::strcmp(command,CommandName)) return false;
    if (busy || !gpGlobals || CMD_ARGC()!=3) return true;
    const auto activeEpoch=epoch;
    std::uint32_t nonce{};std::uint16_t id{};
    if (!Number(CMD_ARGV(1),nonce) || !Number(CMD_ARGV(2),id)) return true;
    const auto token=Identity(entity);
    if (!token || epoch!=activeEpoch) return true;
    const int slot=ENTINDEX(entity);
    if (slot<1 || slot>int(MaxPlayers)) return true;
    vote.Connect(unsigned(slot),token);
    vote.Cast(unsigned(slot),token,nonce,id,gpGlobals->time);
    return true;
}
bool CSOL_ScenarioMapVoteActive(){return vote.State()==Phase::Open;}
void CSOL_CancelScenarioMapVote(){vote.Cancel();}
void CSOL_ResetScenarioMapVote(){++epoch;vote.Reset();recipients={};message=0;resourcesReady=false;}
