// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace wall_test
{
void EntityCases()
{
    Engine e;auto& actor=e.New<Actor>("test_monster");auto& p=e.New<Player>("player");
    auto& w=e.Wall(nullptr,nullptr,1,SF_BREAK_TRIGGER_ONLY,matUnbreakableGlass);
    Check(w.Classify()==0&&w.m_pfnTouch&&(w.ObjectCaps()&FCAP_MUST_RESET),"calloc defaults zero, class zero, special touch survives trigger-only spawnflag");
    auto& target=e.Target("broken");w.pev->target=Engine::String("broken");
    auto& master=e.New<Master>("test_master");master.pev->targetname=Engine::String("lock");Engine::KV(w,"master","lock");
    w.Touch(&p);Check(target.calls==0&&w.pev->solid==SOLID_BSP,"player touch never matches class17");
    actor.classification=16;w.Touch(&actor);Check(target.calls==0,"class16 is not class17");
    actor.classification=17;w.Touch(&actor);
    Check(target.calls==1&&w.pev->solid==SOLID_NOT&&w.pev->deadflag==DEAD_DEAD&&w.pev->effects==EF_NODRAW&&w.pev->takedamage==DAMAGE_NO&&!w.m_pfnTouch,"special monster touch bypasses health master and unbreakable material");
    Check(master.calls==0&&!w.edict()->free,"touch does not query master or remove reusable edict");
    w.pev->team=7;w.Restart();Check(w.pev->health==1&&w.pev->solid==SOLID_BSP&&w.pev->deadflag==DEAD_NO&&!(w.pev->effects&EF_NODRAW)&&w.m_pfnTouch&&w.pev->team==7,"Restart restores real base state and special touch without reassigning team");
    w.Touch(&actor);Check(target.calls==2,"touch can break again after Restart");
    auto& hm=e.Wall("1","0");auto& zm=e.Wall("0","1");auto& both=e.Wall("1","1");
    Check(hm.Classify()==16&&hm.pev->team==1&&!hm.m_pfnTouch,"human wall team one and no touch");
    Check(zm.Classify()==16&&zm.pev->team==2&&!zm.m_pfnTouch,"monster wall team two and no touch");
    Check(both.Classify()==16&&both.pev->team==0&&!both.m_pfnTouch,"dual ratio leaves team untouched");
    auto& invalid=e.Wall("nan","0");Check(!invalid.ValidConfiguration(),"nonfinite config marked invalid");
    CCSOLScenarioWalls collection;csol::scenario::ConfigError error;
    Check(!collection.Collect(error)&&!error.message.empty(),"wall collector rejects actual invalid entity");
    e.Destroy(invalid.edict());Check(collection.Collect(error),"collector finds real zbsbreak entities after invalid removed");
    auto rules=std::make_shared<Rules>();Check(collection.BindRules(rules),"collection binds one actual rule owner");
    const auto generation=rules->Generation();collection.CancelPending();Check(rules->Generation()!=generation,"round cancellation invalidates outstanding rule generation");
    collection.Reset();Check(!hm.TakeDamage(p.pev,p.pev,10,DMG_BULLET),"reset unbinds authoritative rules");
}
void DamageCases()
{
    Engine e;auto& p=e.New<Player>("player");p.pev->flags|=FL_CLIENT;
    auto& actor=e.New<Actor>("test_monster");auto rules=std::make_shared<Rules>();
    auto& w=e.Wall(".5",".25",1000);w.BindRules(rules);
    Check(w.TakeDamage(p.pev,p.pev,100,DMG_CLUB)&&w.pev->health==900&&rules->credited.back()==50,"reward uses scaled damage before base club doubles health loss");
    Check(w.TakeDamage(p.pev,p.pev,100,DMG_POISON)&&w.pev->health==895&&rules->credited.back()==50,"poison material multiplier does not reduce reward");
    Check(w.TakeDamage(actor.pev,actor.pev,100,DMG_BULLET)&&w.pev->health==870&&rules->credited.size()==2,"monster uses own ratio and earns no player credit");
    auto& master=e.New<Master>("test_master");master.pev->targetname=Engine::String("locked");Engine::KV(w,"master","locked");
    const auto count=rules->calls.size();Check(!w.TakeDamage(p.pev,p.pev,10,DMG_BULLET)&&master.calls==1&&rules->calls.size()==count&&w.pev->health==870,"master gate before scaling/reward");
    master.master=false;Check(w.TakeDamage(p.pev,p.pev,10,DMG_BULLET)&&w.pev->health==865,"nonmaster named entity follows real permissive SDK path");
    auto& glass=e.Wall("1","0",100,0,matUnbreakableGlass);glass.BindRules(rules);
    Check(!glass.TakeDamage(p.pev,p.pev,200,DMG_BULLET)&&glass.pev->health==100&&rules->credited.back()==200,"credit occurs before base rejects unbreakable material");
    auto& small=e.Wall("1","0",10);small.BindRules(rules);auto& sink=e.Target("small");small.pev->target=Engine::String("small");
    Check(!small.TakeDamage(p.pev,p.pev,300,DMG_BULLET)&&small.pev->health==-290&&rules->credited.back()==300&&sink.calls==1,"overkill fully credited and target fired once by real Die");
    Check(small.m_flDelay==0&&e.Delays().empty(),"base default-delay mutation restored without replay");
    small.Restart();small.TakeDamage(p.pev,p.pev,300,DMG_BULLET);
    Check(sink.calls==2&&small.m_flDelay==0&&e.Delays().empty(),"second round break remains immediate with one target");
    auto& use=e.Wall("0","0",100);use.pev->target=Engine::String("small");Engine::KV(use,"master","locked");master.master=true;
    use.Use(nullptr,nullptr,USE_OFF,0);Check(sink.calls==3&&use.pev->solid==SOLID_NOT,"script Use bypasses ratio master rules");
    glass.Use(nullptr,nullptr,USE_TOGGLE,0);Check(glass.pev->solid==SOLID_BSP,"base Use respects unbreakable material");
    auto& noRules=e.Wall();Check(!noRules.TakeDamage(p.pev,p.pev,10,DMG_BULLET)&&noRules.pev->health==1000,"unbound wall damage does not pretend rules ready");
    Check(!w.TakeDamage(nullptr,p.pev,10,0)&&!w.TakeDamage(p.pev,nullptr,10,0),"invalid actor/inflictor rejected before base dereference");
}
}
