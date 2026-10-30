// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <functional>
#include <memory>
#include "native/csol/server/scenario_money.h"
#include "native/csol/server/scenario_breakable_guard.h"
class CSOLScenarioEntityAccess { public: bool allowed=true; bool Allowed() const{return allowed;} };
class CGameRules { public: bool scenario=true; std::shared_ptr<CSOLScenarioEntityAccess> access=std::make_shared<CSOLScenarioEntityAccess>(); };
inline CGameRules rulesStorage,otherRulesStorage;
inline CGameRules* g_pGameRules=&rulesStorage;
inline bool scenarioMode{};
inline int activePins{};
struct CSOLScenarioRulesRecord {};
// Controlled SDK ownership edges, with identity/access checks and real pin lifetime.
inline CSOLScenarioSdkScope::CSOLScenarioSdkScope():CSOLScenarioSdkScope(g_pGameRules){}
inline CSOLScenarioSdkScope::CSOLScenarioSdkScope(CGameRules* rules){++activePins;Bind(rules);}
inline void CSOLScenarioSdkScope::Bind(CGameRules* rules){m_rules=rules;m_access=rules?rules->access:nullptr;if(rules&&scenarioMode)m_record=std::make_shared<CSOLScenarioRulesRecord>();}
inline CSOLScenarioSdkScope::~CSOLScenarioSdkScope(){--activePins;}
inline bool CSOLScenarioSdkScope::Current()const{return g_pGameRules==m_rules&&(!m_record||(m_access&&m_access->Allowed()));}
inline void CSOLScenarioSdkScope::RequestClose(){if(m_rules)m_rules->access->allowed=false;}
inline bool CSOL_UsesScenarioRules(){return scenarioMode;}
enum RewardType {RT_NONE,RT_INTO_GAME,RT_PLAYER_JOIN,RT_PLAYER_RESET,RT_PLAYER_SPEC_JOIN};
struct entvars_t{int flags{};};
constexpr int FL_KILLME=1;
class CBaseEntity{public: entvars_t vars{};entvars_t* pev=&vars;unsigned serial=1;bool live=true;};
class CBasePlayer:public CBaseEntity{public:int m_iAccount{};bool has_disconnected{};void AddAccount(int,RewardType,bool=true);void AddAccount_OrigFunc(int,RewardType,bool);};
class EHANDLE{CBaseEntity* entity;unsigned serial;public:explicit EHANDLE(CBaseEntity* e):entity(e),serial(e->serial){}template<class T>T* Get()const{return entity->live&&entity->serial==serial?static_cast<T*>(entity):nullptr;}};
inline struct {float value=16000;} maxmoney;
using std::clamp;
inline std::vector<std::vector<unsigned char>> messages;
inline void Begin(int,int,const float*,entvars_t*){messages.emplace_back();}
inline void Long(int value){for(int i=0;i<4;++i)messages.back().push_back(static_cast<std::uint32_t>(value)>>(8*i));}
inline void Byte(int value){messages.back().push_back(value);}
inline void End(){}
[[noreturn]] inline void Sys_Error(const char*,...){std::abort();}
#define __FUNC__ __FUNCTION__
#define Q_memset std::memset
#define Q_memmove std::memmove
#include "external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.h"
using AccountChain=IHookChainClass<void,CBasePlayer,int,RewardType,bool>;
using AccountRegistry=IHookChainRegistryClassImpl<void,CBasePlayer,int,RewardType,bool>;
struct AccountHooks{AccountRegistry m_CBasePlayer_AddAccount;};
inline AccountHooks g_ReGameHookchains;
#define EXT_FUNC
#define __API_HOOK(name) name##_OrigFunc
#define MSG_ONE 1
#define gmsgMoney 42
#define MESSAGE_BEGIN Begin
#define WRITE_LONG Long
#define WRITE_BYTE Byte
#define MESSAGE_END End
