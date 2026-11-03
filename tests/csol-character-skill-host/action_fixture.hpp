// SPDX-License-Identifier: GPL-3.0-or-later
// Only engine/lifetime boundaries are mocked. Action is taken verbatim from host.cpp.
#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include "../../native/csol/server/scenario_character_skills/host_actions.hpp"
constexpr int DEAD_NO=0,FALSE=0,WEAPON_KNIFE=29,MSG_ONE=1,SVC_WEAPONANIM=35;
constexpr int PLAYER_DIE=4,HUD_PRINTCENTER=4;
enum _Menu { FixtureMenuNone=0 };
using csol::scenario::ConfigError;
using namespace csol::scenario::skills;
inline std::function<void(const char*)> actionCallback;
inline std::vector<std::string> actionEvents;
inline void Event(const char* name){actionEvents.emplace_back(name);const auto saved=actionCallback;if(saved)saved(name);}
struct Vars{int deadflag{},button{},sequence{},weaponanim{},viewmodel{},weaponmodel{},body{};float health=100,frame{};};
struct CBasePlayerItem {int serial=1;bool live=true;int m_iId=WEAPON_KNIFE;float m_flStartThrow{},m_flReleaseThrow{};virtual~CBasePlayerItem()=default;bool CanDeploy(){Event("can-deploy");return true;}void Holster(int){Event("holster");}bool Deploy(){Event("deploy");return true;}};
struct CBasePlayerWeapon:CBasePlayerItem{int m_fInReload=1;};
struct CBasePlayer {int serial=1,index=1;bool live=true;Vars vars;Vars*pev=&vars;CBasePlayerItem* m_pActiveItem{};int m_iFOV=90,m_fSequenceFinished{};_Menu m_iMenu=FixtureMenuNone;float m_flFrameRate{},m_flNextAttack{};CBasePlayer*edict(){return this;}int entindex(){Event("index");return index;}void ResetSequenceInfo(){Event("sequence");}void ResetMaxSpeed(){Event("speed");}};
struct EHANDLE {void* value{};const int* serial{};int expected{};const bool* live{};EHANDLE()=default;template<class T>EHANDLE(T*p):value(p),serial(p?&p->serial:nullptr),expected(p?p->serial:0),live(p?&p->live:nullptr){}void*Get()const{return value&&*live&&*serial==expected?value:nullptr;}template<class T>T*Get()const{return static_cast<T*>(Get());}};
struct CSOLScenarioActiveSkill{};
struct CSOLScenarioEntityAccess{bool allowed=true;bool Allowed()const{return allowed;}};
struct CCSOLScenarioSession{struct MapType{std::shared_ptr<CSOLScenarioEntityAccess> access=std::make_shared<CSOLScenarioEntityAccess>();auto EntityAccess()const{return access;}}map;MapType&Map(){return map;}};
struct CSOLScenarioPlayerRuntimeState{EHANDLE player;std::uint64_t generation=1,skillLifeSerial=1;std::shared_ptr<CSOLScenarioActiveSkill> activeSkill=std::make_shared<CSOLScenarioActiveSkill>();};
struct CCSOLScenarioPlayerRuntime{bool active=true;std::shared_ptr<CSOLScenarioPlayerRuntimeState> state;bool Active()const{return active;}auto Find(const EHANDLE& handle)const{return handle.Get()==state->player.Get()?state:nullptr;}};
inline struct{float time=10;} actionGlobals,*gpGlobals=&actionGlobals;
inline struct{bool pfnMessageBegin=true,pfnMessageEnd=true,pfnWriteByte=true;}g_engfuncs;
struct studiohdr_t{int numseq=400;};inline studiohdr_t actionHeader;
inline void* GET_MODEL_PTR(CBasePlayer*){Event("model");return &actionHeader;}
inline bool SkillCharacterModelId(CBasePlayer&,int&v){v=101;return true;}
inline void MESSAGE_BEGIN(int,int,const void*,Vars*){Event("message-begin");}
inline void WRITE_BYTE(int){Event("message-byte");}
inline void MESSAGE_END(){Event("message-end");}
inline void ClientPrint(Vars*,int,const char*){Event("print");}
inline int shownMenuKeys{};
inline std::string shownMenuText;
inline void ShowMenu(CBasePlayer*,int keys,int,int,char* text){shownMenuKeys=keys;shownMenuText=text;Event("menu");}
struct Resources{CharacterActionTable actions;std::array<int,2>viewmodels{};std::shared_ptr<const CSOLScenarioEntityAccess> access;std::weak_ptr<CCSOLScenarioSession> session;std::map<std::string,std::string>emotionMenus;};
inline std::shared_ptr<Resources>resources;
