// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "characters.h"
#include "weapon_service.h"
#include "weapon_slots.h"
#include "game_rules_gate.h"
#include "../character_catalog.hpp"
#include "../character_protocol.hpp"
#include <array>
#include <sstream>
#include <set>

namespace {
struct PlayerState {
    int userid{}, selected{};
    float nextSelect{}, nextSync{};
    bool initialized{}, applied{};
    csol::CharacterState sent{};
};
std::array<PlayerState,33> states;
std::vector<csol::Character> catalog;
int message{};
bool ready{};
std::string Load(const char* file)
{
    int size=0;auto* bytes=LOAD_FILE_FOR_ME(const_cast<char*>(file),&size);
    if(!bytes)return {};
    std::string result;if(size>0 && size<=512*1024)result.assign(reinterpret_cast<char*>(bytes),size);
    FREE_FILE(bytes);return result;
}
PlayerState& State(CBasePlayer* p)
{
    auto& s=states[p->entindex()];const int userid=GETPLAYERUSERID(p->edict());
    if(s.userid!=userid){s={};s.userid=userid;}
    return s;
}
const csol::Character* Selected(CBasePlayer* p)
{
    if(!ready || !p || p->entindex()<1 || p->entindex()>32 || p->IsDormant())return nullptr;
    const auto* c=csol::FindCharacter(catalog,State(p).selected);
    if(!c || !p->IsAlive() || (CSOL_ZombieMode() && !CSOL_ZombieHuman(p)) ||
        !csol::CharacterTeamAllowed(*c,p->m_iTeam,CSOL_ZombieHuman(p)))return nullptr;
    return c;
}
void Send(CBasePlayer* recipient,const csol::CharacterState& state)
{
    if(!message || !CSOL_CanUseExtendedWeapons(recipient))return;
    MESSAGE_BEGIN(MSG_ONE,message,nullptr,recipient->pev);
    for(auto b:csol::EncodeCharacter(state))WRITE_BYTE(b);
    MESSAGE_END();
}
void Broadcast(const csol::CharacterState& state)
{
    for(int i=1;i<=gpGlobals->maxClients;++i){auto* p=static_cast<CBasePlayer*>(UTIL_PlayerByIndex(i));if(p && !p->IsDormant())Send(p,state);}
}
}
void CSOL_PrecacheCharacters()
{
    ready=false;catalog.clear();states={};message=REG_USER_MSG(csol::CharacterMessage,csol::CharacterMessageBytes);
    const auto json=Load("models/csol/characters.json");if(json.empty())return;
    try {
        auto parsed=csol::ParseCharacters(json);const auto files=Load("shader/csol/characters.txt");
        if(files.empty())throw std::runtime_error("missing character resource manifest");
        std::istringstream input(files);std::vector<std::string> names;std::set<std::string> unique;std::string name;
        while(std::getline(input,name)){
            if(!name.empty()&&name.back()=='\r')name.pop_back();
            if(name.empty())continue;
            if(name.size()>=64 || name.front()=='/' || name.find("..")!=name.npos || name.find_first_of(":\\\"\t ")!=name.npos || !unique.insert(name).second || names.size()>=4096)
                throw std::runtime_error("invalid character download manifest");
            int length=0;auto* bytes=LOAD_FILE_FOR_ME(name.data(),&length);if(!bytes)throw std::runtime_error("missing character dependency: "+name);FREE_FILE(bytes);
            names.push_back(name);
        }
        for(const auto& c:parsed)if(!unique.contains(c.ModelPath()) || !unique.contains(c.portrait))throw std::runtime_error("incomplete character manifest");
        for(auto& n:names)if(n.ends_with(".mdl"))PRECACHE_MODEL(n.data());else if(n.starts_with("sound/"))PRECACHE_SOUND(n.data()+6);else PRECACHE_GENERIC(n.data());
        catalog=std::move(parsed);ready=true;ALERT(at_console,"[CSOL] Characters ready: %u\n",unsigned(catalog.size()));
    } catch(const std::exception& e){ALERT(at_error,"[CSOL] Characters disabled: %s\n",e.what());}
}
const char* CSOL_CharacterModel(CBasePlayer* player)
{
    if(const auto* c=Selected(player))return c->model.c_str();return nullptr;
}
bool CSOL_CharacterScenarioClass(CBasePlayer* player, int& classId,
    std::shared_ptr<const CSOLScenarioEntityAccess> access)
{
    if((access && !access->Allowed()) || !ready || !player)return false;
    const EHANDLE handle(player);
    const int index=player->entindex();
    if(access && !access->Allowed())return false;
    if(!handle.Get() || index<1 || index>32)return false;
    const int userid=GETPLAYERUSERID(handle.Get());
    if(access && !access->Allowed())return false;
    auto* current=dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
    if(!ready || !current || (current->pev->flags&FL_KILLME) || states[index].userid!=userid)return false;
    const auto* character=csol::FindCharacter(catalog,states[index].selected);
    if(!character || character->scenarioClass<0 || character->team!=current->m_iTeam)return false;
    classId=character->scenarioClass;
    return true;
}
bool CSOL_EnsureScenarioBotCharacter(CBasePlayer* player,
    std::shared_ptr<const CSOLScenarioEntityAccess> access)
{
    if((access && !access->Allowed()) || !ready || !player || !(player->pev->flags&FL_FAKECLIENT))return false;
    const EHANDLE handle(player);
    int classId{};
    if(CSOL_CharacterScenarioClass(player,classId,access))return true;
    if((access && !access->Allowed()) || !ready || handle.Get<CBaseEntity>()!=player)return false;
    const int index=player->entindex();
    if(access && !access->Allowed())return false;
    if(index<1 || index>32 || handle.Get<CBaseEntity>()!=player)return false;
    const int userid=GETPLAYERUSERID(handle.Get());
    if((access && !access->Allowed()) || handle.Get<CBaseEntity>()!=player || !ready)return false;
    auto& state=states[index];
    if(state.userid!=userid){state={};state.userid=userid;}
    for(const auto& character:catalog)
        if(character.team==player->m_iTeam && character.scenarioClass>=0){
            state.selected=character.id;return true;
        }
    return false;
}
void CSOL_ApplyCharacterModel(CBasePlayer* player)
{
    const auto* c=Selected(player);if(!c)return;
    auto& s=State(player);const auto path=c->ModelPath();
    if(Q_strcmp(STRING(player->pev->model),path.c_str()))player->SetNewPlayerModel(path.c_str());
    player->pev->skin=c->skin;
    auto* info=GET_INFO_BUFFER(player->edict());
    if(Q_strcmp(GET_KEY_VALUE(info,"model"),c->model.c_str()))SET_CLIENT_KEY_VALUE(player->entindex(),info,"model",c->model.c_str());
    s.applied=true;
}
bool CSOL_CharacterCommand(edict_t* entity,const char* command)
{
    if(Q_strcmp(command,"csol_character_select") && Q_strcmp(command,"csol_character_sync") && Q_strcmp(command,"csol_character_cancel"))return false;
    auto* player=CBasePlayer::Instance(entity);if(!player || player->entindex()<1 || player->entindex()>32)return true;
    if(!CSOL_CanUseExtendedWeapons(player))return true;
    auto& state=State(player);
    if(!Q_strcmp(command,"csol_character_cancel")) {
        if(player->m_iMenu==Menu_ChooseAppearance) {
            // Initial team selection is provisional until ChooseAppearance
            // finishes joining. Cancelling it must not consume the per-life
            // team-change allowance; keep that restriction for joined players.
            if(player->m_iJoiningState==PICKINGTEAM && player->m_bJustConnected)
                player->m_bTeamChanged=false;
            player->m_iMenu=Menu_ChooseTeam;
            if(CSOL_ShowScenarioTeamMenu(*player))return true;
            if(player->m_iJoiningState==JOINED)
                ShowVGUIMenu(player,VGUI_Menu_Team,MENU_KEY_1|MENU_KEY_2|MENU_KEY_5|MENU_KEY_0,"#IG_Team_Select");
            else
                ShowVGUIMenu(player,VGUI_Menu_Team,MENU_KEY_1|MENU_KEY_2|MENU_KEY_5,"#Team_Select");
        }
        return true;
    }
    if(!Q_strcmp(command,"csol_character_sync")){
        if(gpGlobals->time>=state.nextSync){state.nextSync=gpGlobals->time+1;state.initialized=false;CSOL_UpdateCharacters(player);}
        return true;
    }
    if(gpGlobals->time<state.nextSelect)return true;
    state.nextSelect=gpGlobals->time+.35f;
    const char* argument=CMD_ARGV_(1);char* end=nullptr;const long id=strtol(argument,&end,10);
    const auto* c=id>0 && id<=65535?csol::FindCharacter(catalog,unsigned(id)):nullptr;
    const bool appearance=player->m_iMenu==Menu_ChooseAppearance &&
        (player->m_iTeam==TERRORIST || player->m_iTeam==CT);
    if(!ready || CMD_ARGC_()!=2 || !*argument || !end || *end || !c ||
        (CSOL_UsesScenarioRules() && c->scenarioClass<0) ||
        (!appearance && (!player->IsAlive() || (CSOL_ZombieMode() && !CSOL_ZombieHuman(player)))) ||
        !csol::CharacterTeamAllowed(*c,player->m_iTeam,!appearance && CSOL_ZombieHuman(player))){
        ClientPrint(player->pev,HUD_PRINTCENTER,"Character unavailable for current team or role");return true;
    }
    state.selected=int(id);
    // Keep the native ChooseAppearance -> GetIntoGame -> Spawn lifecycle.
    // ZP initializes its human class there; writing JOINED here skips it.
    if(appearance){
        const EHANDLE handle(player);
        auto* rules=g_pGameRules;
        HandleMenu_ChooseAppearance(player,1);
        if(g_pGameRules!=rules || handle.Get<CBaseEntity>()!=player)return true;
        if(player->m_iMenu!=Menu_ChooseAppearance)CSOL_ScenarioCharacterSelected(*player);
        if(g_pGameRules!=rules || handle.Get<CBaseEntity>()!=player)return true;
    }
    CSOL_ApplyCharacterModel(player);CSOL_UpdateCharacters(player);
    Send(player,state.sent);return true;
}
void CSOL_UpdateCharacters(CBasePlayer* player)
{
    if(!ready || !player || player->entindex()<1 || player->entindex()>32)return;
    auto& s=State(player);const auto* c=Selected(player);
    csol::CharacterState current{std::uint8_t(player->entindex()),std::uint8_t(c!=nullptr),std::uint16_t(s.selected),std::uint32_t(s.userid)};
    if(current!=s.sent){s.sent=current;Broadcast(current);}
    if(!s.initialized && CSOL_CanUseExtendedWeapons(player)){
        Send(player,{});for(int i=1;i<=gpGlobals->maxClients;++i)if(states[i].sent.slot)Send(player,states[i].sent);s.initialized=true;
    }
    if(c)CSOL_ApplyCharacterModel(player);
    else if(s.applied && player->IsAlive()){
        s.applied=false;player->pev->skin=0;
        player->SetPlayerModel(player->m_bHasC4);
        const char* model=GET_KEY_VALUE(GET_INFO_BUFFER(player->edict()),"model");
        char path[64];Q_snprintf(path,sizeof(path),"models/player/%s/%s.mdl",model,model);
        player->SetNewPlayerModel(path);
    }
}
void CSOL_CharacterRoleChanged(CBasePlayer* player){CSOL_UpdateCharacters(player);}
void CSOL_CharacterDisconnected(CBasePlayer* player)
{
    if(!player || player->entindex()<1 || player->entindex()>32)return;
    const int slot=player->entindex();states[slot]={};Broadcast({std::uint8_t(slot),0,0,0});
}
