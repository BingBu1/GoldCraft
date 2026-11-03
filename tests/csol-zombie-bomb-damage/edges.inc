// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit owner/registry/protection/message boundaries. SDK layout, RTTI,
// edicts, EHANDLE and the entire damage/impulse translation units stay real.
CCSOLScenarioRuntime::CCSOLScenarioRuntime()=default;
void CCSOLScenarioRuntime::RequestClose(){m_closing=true;}
CCSOLScenarioSession::~CCSOLScenarioSession()=default;
CCSOLMonsterPool::~CCSOLMonsterPool()=default;
bool CCSOLScenarioMap::BeginMap(std::string_view){++m_roundGeneration;return true;}
CSOLScenarioSdkScope::CSOLScenarioSdkScope(CGameRules* rules):m_rules(rules){}
CSOLScenarioSdkScope::~CSOLScenarioSdkScope()=default;
bool CSOLScenarioSdkScope::Current()const{return bomb_damage_test::sdkCurrent;}
CGameRules* CSOL_ScenarioOwnerRules(const CCSOLScenarioSession*){return nullptr;}
std::shared_ptr<CCSOLScenarioPlayerRuntime> CSOL_ScenarioPlayerAuthority(const CSOLScenarioEntityAccess*)
{return bomb_damage_test::authority;}
bool CCSOLScenarioPlayerRuntime::Active()const{return m_active&&bomb_damage_test::active;}
bool CCSOLScenarioPlayerRuntime::Activate(){m_active=true;return true;}
bool CCSOLScenarioPlayerRuntime::SetProfile(CBasePlayer& player,CSOLScenarioPlayerProfile profile)
{
    auto state=std::make_shared<CSOLScenarioPlayerRuntimeState>();state->player=EHANDLE(&player);
    state->generation=1;state->profile=std::move(profile);m_players[1]=state;return true;
}
bool CCSOLScenarioPlayerRuntime::PrepareZombieBombHit(const EHANDLE&,CSOLMonsterSkillGuard guard,
    float& damage,float& force,bool& immune,csol::scenario::ConfigError&,std::uint32_t(*)())
{
    auto& f=*bomb_damage_test::Fixture::current;++f.prepared;f.order.push_back(1);
    damage*=f.damageFactor;force*=f.forceFactor;immune=f.immune;
    auto cb=f.onPrepare;if(cb)cb();return guard.Current()&&f.prepareResult;
}
bool CCSOLScenarioMessages::SendMonsterSkillShake(edict_t* recipient)const
{
    auto& f=*bomb_damage_test::Fixture::current;++f.shake;f.order.push_back(3);
    bomb_damage_test::Check(recipient==f.player->edict(),"shake targets original player");
    auto cb=f.onShake;if(cb)cb();return f.shakeResult;
}
bool CCSOLScenarioMessages::SendOberonImpact(edict_t* recipient,bool stun)const
{
    auto& f=*bomb_damage_test::Fixture::current;++f.impact;f.order.push_back(4);
    bomb_damage_test::Check(recipient==f.player->edict()&&!stun,"view kick targets player without stun");
    auto cb=f.onImpact;if(cb)cb();return f.impactResult;
}
