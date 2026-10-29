// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

// Ordinary global Bot notification is outside this component fixture; actual
// SDK C++ NAV members are released, including in the reset trace callbacks.
CNavArea::~CNavArea() {}
void CBaseAnimating::ResetSequenceInfo()
{ g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.callChain(&CBaseAnimating::ResetSequenceInfo_OrigFunc,this); }
int SENTENCEG_Lookup(const char*,char (&)[32]) { throw std::runtime_error("unexpected sentence lookup"); }
void Sys_Error(const char*,...) { throw std::runtime_error("unexpected SDK fatal error"); }

namespace actions_test
{
bool Equal(const Vector& a,const Vector& b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
bool Clock(const CSOLMonsterInterval& c,float i,float d,float s) { return c.interval==i && c.deadline==d && c.stamp==s; }
BOOL Victim::IsPlayer()
{ ++isPlayerCalls;const auto callback=onIsPlayer;const bool answer=player;if(callback)callback();return answer; }
BOOL Victim::TakeDamage(entvars_t* inflictor,entvars_t* attacker,float amount,int flags)
{ damage.push_back({inflictor,attacker,amount,flags});const auto callback=onDamage;if(callback)callback();return TRUE; }
Fixture::Fixture()
{
    Require(active==nullptr,"single actions engine");active=this;
    engine.globals.time=10;
    monster=&Monster(0,40);auto& type=types.back();type.propensity=0;type.attackDamage=12;type.attackRange=60;type.attackInterval=.8f;
    monster->pev->origin=Vector(0,0,0);monster->pev->mins=Vector(-16,-16,0);monster->pev->maxs=Vector(16,16,72);
    monster->pev->health=100;monster->pev->deadflag=DEAD_NO;monster->pev->maxspeed=200;monster->pev->angles=Vector(0,0,0);
    call.emplace(map,pool,requests,*monster);
    state.lease=pool.Lease(*monster);state.attributes={100,0,12,60,.8f,200,0};
    using S=csol::scenario::MonsterSequence;state.sequences[S::AttackWalk]=2;state.sequences[S::AttackRun1]=3;state.sequences[S::AttackRun2]=4;
    monster->Animation().SynchronizeSequence(1);monster->pev->sequence=1;
    model.header.numseq=8;model.header.seqindex=static_cast<int>(reinterpret_cast<char*>(model.sequence.data())-reinterpret_cast<char*>(&model));
    for(auto& seq:model.sequence){seq.numframes=11;seq.fps=10;seq.flags=STUDIO_LOOPING;seq.linearmovement[0]=2;}
    g_engfuncs.pfnGetModelPtr=ModelQuery;g_engfuncs.pfnTraceLine=Trace;
    appearance.feetZ=0;appearance.centroidZ=36;appearance.eyesZ=64;
}
Fixture::~Fixture()
{
    if(hooked)g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.unregisterHook(AnimationHook);
    onModel={};onAnimation={};traceResponse={};ClearGraph();active=nullptr;
}
Victim& Fixture::Target(int slot)
{
    auto* ed=engine.Raw("actions_damage_receiver",slot);auto* victim=new(&ed->v) Victim;
    victim->pev=&ed->v;victim->m_pEntity=nullptr;victim->pev->health=100;victim->pev->origin=Vector(50,0,0);
    victim->pev->mins=Vector(-16,-16,-36);victim->pev->maxs=Vector(16,16,36);
    engine.privateOwners[ed]=[](void* object){static_cast<Victim*>(object)->~Victim();std::free(object);};
    return *victim;
}
CNavArea* Fixture::Flat(float x,float y,float z,float half)
{
    const Vector lo(x-half,y-half,z),hi(x+half,y+half,z),ne(x+half,y-half,z),sw(x-half,y+half,z);
    void* storage=::operator new(sizeof(CNavArea));std::memset(storage,0,sizeof(CNavArea));
    auto* area=new(storage) CNavArea(&lo,&ne,&hi,&sw);areaStorage.emplace_back(area);areas.push_back(area);return area;
}
void Fixture::Grid(){Require(world.Initialize(areas),"actions NAV initialized");}
void Fixture::ClearGraph(){world.Reset();CNavArea::ClearSearchLists();areas.clear();areaStorage.clear();}
void Fixture::ReborrowSameSlot(){Reborrow(*monster);monster->pev->deadflag=DEAD_NO;monster->pev->health=100;}
void Fixture::HookAnimation(){hooked=true;g_ReGameHookchains.m_CBaseAnimating_ResetSequenceInfo.registerHook(AnimationHook,150);}
void Fixture::ClearTraces()
{
    traces.clear();traceResponse=[](const TraceCall& call,TraceResult& result)
    { result={};result.flFraction=1;result.vecEndPos=call.to;result.vecPlaneNormal=Vector(0,0,1); };
}
void* Fixture::ModelQuery(edict_t*)
{ ++active->queries;const auto callback=active->onModel;if(callback)callback();return &active->model; }
void Fixture::AnimationHook(Hook* chain,CBaseAnimating* monster)
{ const auto callback=active->onAnimation;if(callback)callback(chain,monster);else chain->callNext(monster); }
void Fixture::Trace(const float* from,const float* to,int flags,edict_t* ignore,TraceResult* output)
{
    const TraceCall record{Vector(from),Vector(to),flags,ignore};active->traces.push_back(record);
    const auto callback=active->traceResponse;Require(bool(callback),"Trace response explicitly configured");callback(record,*output);
}
CSOLMonsterMeleeResult Fixture::Attack(const EHANDLE& target,float dx,float dy)
{ return CSOL_TryOrdinaryMonsterMelee(*call,state,target,boostUntil,suppressed,dx,dy,clocks); }
}
