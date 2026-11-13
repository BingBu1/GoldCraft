// SPDX-License-Identifier: GPL-3.0-or-later
// Included beside Case, after its complete definition. The test compiles with
// the old API but explicitly fails if run before the mandatory guard is added.
struct OwnerGuard
{
    bool active{true};
    static bool Current(const void* context)
    { return static_cast<const OwnerGuard*>(context)->active; }
};
OwnerGuard* rewardDrawOwner{};
float RevokeRewardDrawOwner(float low,float)
{ rewardDrawOwner->active=false;return low; }
template<class Function>
Result GuardedKick(Function function,Case& c,OwnerGuard& guard,ConfigError& error)
{
    if constexpr(std::is_invocable_r_v<Result,Function,CCSOLScenarioMonster&,CBasePlayer&,
        const std::shared_ptr<CCSOLScenarioRuntime>&,float,const Vector&,const Vector&,ConfigError&,
        bool(*)(const void*),const void*>)
        return function(*c.target,*c.actor,c.runtime,15,Vector(3,4,0),g_vecZero,error,
            OwnerGuard::Current,&guard);
    else throw std::runtime_error("human kick damage lacks mandatory synchronous owner guard");
}
void OwnerGuardCases()
{
    // Reset the action without changing profile generation, native life,
    // player handle or target lease: this is the missing cross-layer identity.
    for(int phase:{0,1,2})
    {
        Case c;OwnerGuard guard;ConfigError error;bool revoked{};
        struct RestoreDraw
        {
            decltype(g_engfuncs.pfnRandomFloat) previous=g_engfuncs.pfnRandomFloat;
            ~RestoreDraw(){rewardDrawOwner=nullptr;g_engfuncs.pfnRandomFloat=previous;}
        } restoreDraw;
        const auto state=c.runtime->players->Find(EHANDLE(c.actor));
        Require(state!=nullptr,"guard case has actual player identity");
        const auto generation=state->generation,life=state->skillLifeSerial,spawn=state->nativeSpawnSerial;
        const auto lease=c.session->Monsters().Lease(*c.target);
        const auto beforeVelocity=c.target->pev->velocity;
        if(phase==0)guard.active=false;
        if(phase==1)
        { rewardDrawOwner=&guard;g_engfuncs.pfnRandomFloat=RevokeRewardDrawOwner; }
        if(phase==2)c.e.onOffset=[&](const edict_t*)
        { if(!revoked&&c.target->pev->health<1000){revoked=true;guard.active=false;} };
        const auto result=GuardedKick(&CSOL_HumanKickMonsterDamage,c,guard,error);
        c.e.onOffset={};rewardDrawOwner=nullptr;
        g_engfuncs.pfnRandomFloat=[](float low,float){return low;};
        Check(result==Result::Cancelled,"revoked action owner cancels synchronous result");
        Check(c.target->pev->health==(phase==2?985:1000),
            "owner cancellation blocks uncommitted health but retains committed health");
        Check(c.target->pev->velocity==beforeVelocity&&!CSOL_HumanKickMonsterStaggered(*c.target,100),
            "revoked owner never applies late impulse or stagger");
        Check(state->generation==generation&&state->skillLifeSerial==life&&state->nativeSpawnSerial==spawn&&
            c.session->Monsters().Lease(*c.target)==lease,"owner cancellation does not rely on profile/life/lease changes");
    }
    // The callback context is destroyed before deferred replay. Only copied
    // action credit may outlive GuardedKick; dereferencing this guard is invalid.
    {
        Case c;c.Held(WEAPON_KNIFE);c.target->pev->health=10;ConfigError error;
        auto& requests=c.runtime->gameplay->Deaths().Requests();
        const auto scope=requests.BeginBehavior(*c.target);Require(scope.has_value(),"open real deferred owner guard death");
        {OwnerGuard guard;Require(GuardedKick(&CSOL_HumanKickMonsterDamage,c,guard,error)==Result::Killed,error.message.c_str());}
        Require(requests.EndBehavior(*scope),requests.Error().c_str());
        Check(c.target->pev->deadflag==DEAD_DEAD,"deferred lethal hit completes after synchronous guard lifetime ends");
        const auto records=c.runtime->deathServices->Records();
        Check(std::count_if(records.begin(),records.end(),[](const auto& record)
            {return record.kind==CSOLScenarioDeathRecord::Kind::HumanKickKill;})==1,
            "deferred action credit never stores or queries the expired stack guard");
    }

}
