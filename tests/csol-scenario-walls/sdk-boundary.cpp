// SPDX-License-Identifier: GPL-3.0-or-later
// Engine/resource boundaries and forbidden unrelated gameplay only. The
// extraction plan supplies actual breakable/target/relay/render/account code.
#include "fixture.hpp"
#include <cstdlib>

Vector g_vecAttackDir{};
CBotManager* TheBots{};
int gmsgMoney=77;
cvar_t maxmoney={const_cast<char*>("mp_maxmoney"),const_cast<char*>("16000"),0,16000};
CGlobalState gGlobalState;
CReGameHookchains g_ReGameHookchains;

namespace
{
[[noreturn]] void Forbidden(const char* name)
{std::cerr<<"UNEXPECTED WALL SDK BOUNDARY "<<name<<'\n';std::abort();}
}
CGlobalState::CGlobalState() {}
void Sys_Error(const char*,...){Forbidden("SDK fatal error");}
void CGlobalState::EntitySetState(string_t,GLOBALESTATE){Forbidden("global world state");}
int CSave::WriteFields(const char*,void*,TYPEDESCRIPTION*,int){Forbidden("save file serializer");}
int CRestore::ReadFields(const char*,void*,TYPEDESCRIPTION*,int){Forbidden("restore file serializer");}
void UTIL_PrecacheOther(const char*){Forbidden("first-map spawnobject is zero");}
CBaseEntity* CBaseEntity::Create(const char*,const Vector&,const Vector&,edict_t*){Forbidden("first-map spawned item is absent");}
void ExplosionCreate(const Vector&,Vector&,edict_t*,int,BOOL){Forbidden("first-map wall explosion magnitude is zero");}
void UTIL_Sparks(const Vector&){Forbidden("TraceAttack spark presentation outside wall hit acceptance");}
void UTIL_Ricochet(const Vector&,float){Forbidden("TraceAttack ricochet presentation outside wall hit acceptance");}
void EMIT_SOUND_DYN(edict_t* entity,int,const char*,float,float,int,int)
{wall_test::Engine::active->Step("sound",CBaseEntity::Instance(entity));}

// The actual engine/entity hash service is controlled; all SDK target-loop,
// killtarget and cancellation decisions remain in extracted production code.
void AddEntityHashValue(entvars_t* pev,const char*,hash_types_e)
{wall_test::Engine::active->Step("make-string-class",CBaseEntity::Instance(pev));}
void RemoveEntityHashValue(entvars_t*,const char*,hash_types_e){}
CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity* previous,const char* name)
{auto* ed=wall_test::Engine::Find(previous?previous->edict():nullptr,"classname",name);return ed?CBaseEntity::Instance(ed):nullptr;}
