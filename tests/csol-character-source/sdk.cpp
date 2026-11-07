// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK map/smoke/grenade factories, source resolver and damage callback. Only the
// engine services and the enclosing damage host are controlled boundaries.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario/core/scenario_player_runtime.h"
#include "../../native/csol/server/scenario/core/scenario_session.h"
#include "../../native/csol/server/scenario_character_skills/damage.hpp"
#include "../../native/csol/server/weapons/weapons.h"

using csol::scenario::ConfigError;
using namespace csol::scenario::skills;
using sdk_test::Check;
// Explicit template instantiation may name the existing private method.
// This adds no production friend, test entry point or replacement body.
struct SourceTag
{
    using type = bool (CCSOLScenarioPlayerRuntime::*)(CBaseEntity&, int&, ConfigError&);
    friend type SourceMethod(SourceTag);
};
template<class Tag, typename Tag::type Pointer> struct MemberAccess
{ friend typename Tag::type SourceMethod(Tag) { return Pointer; } };
template struct MemberAccess<SourceTag, &CCSOLScenarioPlayerRuntime::ReadEntitySource>;

struct Host final : DamageHost
{
    CCSOLScenarioPlayerRuntime& runtime;
    CBaseEntity* source{};
    bool owner{}, current{true}, nonzeroOffset{true};
    int sourceReads{}, stopped{}, key{};
    explicit Host(CCSOLScenarioPlayerRuntime& value) : runtime(value) {}
    bool Current() const override { return current; }
    bool SourceHasNonzeroOffset(bool& value, ConfigError&) override
    { value=nonzeroOffset; return true; }
    bool SourceIsOwner(bool& value, ConfigError&) override { value=owner; return true; }
    bool SourceWeapon(int& value, ConfigError& error) override
    { ++sourceReads; return (runtime.*SourceMethod(SourceTag{}))(*source,value,error); }
    bool StopCloaking(int value, ConfigError&) override { ++stopped; key=value; return true; }
    bool Active(bool&,ConfigError&) override { throw std::runtime_error("unexpected active gate"); }
    bool Alive(bool&,ConfigError&) override { throw std::runtime_error("unexpected alive gate"); }
    bool InstalledBuff(int,bool&,ConfigError&) override { throw std::runtime_error("unexpected buff gate"); }
    bool Parameter(DamageParameter,float&,ConfigError&) override { throw std::runtime_error("unexpected parameter"); }
    bool ReadHealth(float&,float&,ConfigError&) override { throw std::runtime_error("unexpected health read"); }
    bool WriteHealth(float,ConfigError&) override { throw std::runtime_error("unexpected health write"); }
    bool RecoverSelf(float,ConfigError&) override { throw std::runtime_error("unexpected recovery"); }
};
class WeaponSource final : public CBaseEntity, public CSOLWeaponDamageSource
{
public:
    int weapon{144};
    int DamageWeaponId() const override { return weapon; }
};

int main()
try
{
    auto engine=std::make_unique<sdk_test::Engine>();
    auto session=std::make_shared<CCSOLScenarioSession>();
    CCSOLScenarioPlayerRuntime runtime(session);
    const auto read=[&](CBaseEntity& entity,int& value,ConfigError& error)
    { return (runtime.*SourceMethod(SourceTag{}))(entity,value,error); };
    WeaponSource weapon;
    for (int repetition=0;repetition<4;++repetition)
    {
        auto* ed=sdk_test::Engine::Create(sdk_test::Engine::String("smokezone"));
        auto* smoke=CBaseEntity::Instance(ed);
        sdk_test::Require(smoke!=nullptr,"real smokezone export creates the SDK object");
        Check(dynamic_cast<const CSOLWeaponDamageSource*>(smoke)==nullptr,
            "actual smokezone keeps the original base source contract");
        int value=-1;ConfigError error;
        Check(read(*smoke,value,error)&&value==0&&error.message.empty(),
            "actual source resolver accepts smokezone zero source");
        for(int subtype:{22,23})for(int flag:{0,1})
        {
            const auto* skill=FindSkill(subtype);
            sdk_test::Require(skill&&skill->damageCallback==DamageCallback::StopCloaking11,
                "actual original subtype uses key11 callback");
            Host host(runtime);host.source=smoke;
            DamageState state;std::array<std::uint8_t,32> context{};context[20]=flag;error={};
            Check(ApplyDamageCallback(*skill,state,150,context,host,error)&&error.message.empty(),
                "smoke damage callback succeeds without disabling character skill");
            Check(host.sourceReads==1&&host.stopped==0,
                "smoke zero source keeps cloak independently of context20");
            host.owner=true;error={};
            Check(ApplyDamageCallback(*skill,state,150,context,host,error)&&host.stopped==1&&host.key==11,
                "owner-originated damage still stops cloak11");
            host.owner=false;host.source=&weapon;error={};
            Check(ApplyDamageCallback(*skill,state,150,context,host,error)&&host.stopped==2,
                "nonzero explicit weapon source still stops cloak11");
            host.current=false;error={};
            Check(!ApplyDamageCallback(*skill,state,150,context,host,error)&&host.stopped==2,
                "cancelled host cannot perform another cloak transition");
        }
        EHANDLE before(smoke);
        sdk_test::Engine::Remove(ed);
        Check(before.Get()==nullptr,"real SDK removal invalidates smoke handle");
    }
    // Renaming a base map entity does not turn it into a weapon source.
    auto* fakeEd=sdk_test::Engine::Create(sdk_test::Engine::String("info_target"));
    fakeEd->v.classname=sdk_test::Engine::String("smokezone");
    int value=-1;ConfigError error;
    Check(read(*CBaseEntity::Instance(fakeEd),value,error)&&value==0&&error.message.empty(),
        "map entity classname does not invent a weapon source");
#include "map_source_cases.inc"
    for(int id:{0,144,197,726})
    {weapon.weapon=id;value=-1;error={};Check(read(weapon,value,error)&&value==id&&error.message.empty(),
        "explicit source preserves raw ID without active weapon inference");}
    engine.reset();session.reset();
    std::cout<<"entity source SDK checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
catch(const std::exception& error)
{std::cerr<<"SDK exception: "<<error.what()<<'\n';return 2;}
