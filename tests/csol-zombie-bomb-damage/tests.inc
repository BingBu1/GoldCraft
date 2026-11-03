// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.inc"
namespace
{
using namespace bomb_damage_test;
void OriginalVectors()
{
    for(const auto& row:golden)
    {
        Fixture f;f.player->pev->origin=row.position;f.player->pev->velocity=row.old;
        f.player->pev->flags=FL_CLIENT|row.flags;
        Require(f.Run(0),"original x86 zero-damage vector accepted");
        Check(Same(f.player->pev->velocity,row.result),"velocity bits match original player-hit x86");
        Check(f.prepared==0&&f.player->hits.empty(),"damage0 skips protection and damage");
        Check(f.shake==int(row.hit)&&f.impact==int(row.hit),"radius and both messages match original x86");
    }
}
void DamageAndOrder()
{
    for(float distance:{0.f,1.f,100.f,200.f,249.f,250.f,251.f})
    {
        Fixture f;f.player->pev->origin=Vector(distance,0,0);f.player->onDamage=[&]{f.order.push_back(2);};
        Require(f.Run(123.5f),"radius damage accepted");
        Check(f.player->hits.size()==std::size_t(distance<250),"strict radius250 excludes equality");
        if(distance<250)
        {
            const auto& hit=f.player->hits.front();
            Check(hit.amount==123.5f&&hit.inflictor==f.bomb->pev&&hit.attacker==f.creator->pev&&hit.bits==DMG_BLAST,
                "full damage at every admitted distance and monster attribution");
            Check(f.order==std::vector<int>({1,2,3,4}),"prepare damage shake impact order");
        }
    }
    for(int mode=0;mode<4;++mode)
    {
        Fixture f;f.damageFactor=.25f;f.forceFactor=.5f;
        if(mode==0)f.immune=true;
        if(mode==1)f.prepareResult=false;
        if(mode==2)f.player->damageResult=FALSE;
        if(mode==3)f.creator->pev->flags|=FL_CLIENT;
        const bool ok=f.Run(80);
        Check(ok==(mode!=1),"protection failure versus handled immunity");
        if(mode<2)Check(f.player->hits.empty()&&f.shake==0&&f.impact==0&&Same(f.player->pev->velocity,Vector()),"immune/failure suppress later effects");
        else
        {
            Check(f.player->hits.size()==1&&f.player->hits[0].amount==20&&f.shake==1&&f.impact==1,
                "modified full damage and rejected TakeDamage still proceed");
            Check(f.player->hits[0].bits==(mode==3?DMG_EXPLOSION:DMG_BLAST),"native attacker flag controls damage bits");
        }
    }
    for(int mode=0;mode<3;++mode)
    {
        Fixture f;const auto state=f.State();
        f.player->onDamage=[&]{f.player->pev->health=0;f.player->pev->deadflag=DEAD_DEAD;
            if(mode!=1)++state->skillLifeSerial;if(mode!=0)++state->monsterLifeSerial;};
        Require(f.Run(),"TakeDamage killing player is still handled");
        Check(f.player->pev->velocity.x>0&&f.shake==1&&f.impact==1,"death serial changes preserve impulse and both messages");
    }
    for(int mode=0;mode<3;++mode)
    {
        Fixture f;if(mode==0)f.creatorLease=false;if(mode==1)++f.edicts[3].serialnumber;
        if(mode==2){f.creator->pev->health=0;f.creator->pev->deadflag=DEAD_DEAD;}
        Require(f.Run(),"creator lifecycle attribution accepted");
        Check(f.player->hits.front().attacker==(mode==2?f.creator->pev:f.bomb->pev),"dead same owner retained, gone/recycled lease falls back to bomb");
    }
    {
        Fixture f;f.onPrepare=[&]{f.creatorLease=false;};Require(f.Run(),"owner lease can retire during protection");
        Check(f.player->hits.front().attacker==f.bomb->pev,"attribution reread after preparation");
    }
}
void ImpulseContract()
{
    const Vector old(1e20f,-1e20f,9.5f),requested(1,-1,-100);
    for(const auto& row:impulseGolden)
    {
        Fixture f;f.player->pev->velocity=old;
        if(!row.alive){f.player->pev->health=0;f.player->pev->deadflag=DEAD_DEAD;}
        CSOLScenarioStatBuff buff;buff.group=1;buff.knockbackPriority=1;buff.knockbackResistance=1-row.factor;
        f.State()->buffs.push_back(buff);Require(f.Impulse(requested,row.use),"original impulse matrix accepted");
        Check(f.lastApplied&&Same(f.player->pev->velocity,row.result),"direct-copy and modifier bits match original x86");
    }
    for(int priority:{-1,0,1,2})
    {
        Fixture f;CSOLScenarioStatBuff buff;buff.group=1;buff.knockbackPriority=priority;buff.knockbackResistance=.5f;
        buff.blocksKnockback=true;f.State()->buffs.push_back(buff);f.State()->actionKnockbackBlocked=true;
        Require(f.Impulse(Vector(100,0,0)),"mode15 bypasses action and StatBuff hard gates");
        Check(f.lastApplied&&f.player->pev->velocity.x==(priority>=1?50.f:100.f),"zombie bomb minimum priority1");
        f.player->pev->velocity=Vector();Require(f.Impulse(Vector(100,0,0),true,true),"ordinary gate returns handled");
        Check(!f.lastApplied&&f.player->pev->velocity.x==0,"ordinary entry still enforces action block");
        f.State()->actionKnockbackBlocked=false;Require(f.Impulse(Vector(100,0,0),true,true),"ordinary StatBuff gate");
        Check(!f.lastApplied,"ordinary active blocksKnockback retained");
    }
    for(int stage=0;stage<2;++stage)for(int kind=0;kind<8;++kind)
    {
        Fixture f;auto cb=[&]{f.Cancel(kind);};if(stage==0)f.player->onAlive=cb;else f.player->onPlayer=cb;
        const bool ok=f.Impulse(Vector(100,0,0));
        Check(!ok&&!f.lastApplied&&Same(f.player->pev->velocity,Vector()),"reentrant virtual cannot apply impulse to cancelled player");
        if(ok)std::cerr<<"impulse cancellation stage="<<stage<<" kind="<<kind<<'\n';
    }
    for(int stage=0;stage<2;++stage)
    {
        Fixture f;auto cb=[&]{authority.reset();};if(stage==0)f.player->onAlive=cb;else f.player->onPlayer=cb;
        Check(!f.Impulse(Vector(100,0,0))&&!f.lastApplied,"authority replacement during virtual cancels impulse");
    }
    {
        Fixture f;f.player->pev->flags|=FL_SPECTATOR;f.player->pev->velocity=old;
        Require(f.Impulse(requested),"nonplayer predicate bypasses modifier arithmetic");
        Check(Same(f.player->pev->velocity,requested),"IsPlayer false is exact direct copy");
    }
}
void Cancellation()
{
    for(int stage=0;stage<3;++stage)for(int kind=0;kind<14;++kind)
    {
        Fixture f;auto cb=[&]{f.Cancel(kind);};
        if(stage==0)f.onPrepare=cb;if(stage==1)f.player->onDamage=cb;if(stage==2)f.onShake=cb;
        const bool ok=f.Run();
        if(kind>=6)Check(!ok,"whole operation cancellation propagates");
        if(stage<2)
        {
            Check(f.shake==0&&f.impact==0,"cancel before impulse suppresses messages");
            Check(Same(f.player->pev->velocity,Vector()),"cancelled victim never receives velocity");
            Check(f.player->hits.size()==std::size_t(stage==1),"damage callback count at cancel boundary");
        }
        else Check(f.shake==1&&f.impact==0,"shake callback cancellation suppresses later view kick");
    }
    for(int kind=6;kind<14;++kind)
    {
        Fixture f;auto* target=f.NewTarget(4);f.sphere.push_back(target->edict());
        f.onImpact=[&]{f.Cancel(kind);};Check(!f.Run(),"view kick callback owner cancellation propagates");
        Check(f.shake==1&&f.impact==1&&target->hits.empty()&&f.sphereCalls==0,"view kick cancellation stops sphere traversal");
    }
    for(int stage=0;stage<2;++stage)
    {
        Fixture f;if(stage==0)f.shakeResult=false;else f.impactResult=false;
        Check(!f.Run()&&!f.lastError.empty(),"message transport failure fails operation");
        Check(f.shake==1&&f.impact==stage,"failed shake does not send view kick");
    }
}
void NonPlayers()
{
    {
    Fixture f;f.player->pev->origin=Vector(500,0,0);
    for(int i=0;i<6;++i){auto* t=f.NewTarget(i+4);t->classification=std::array<int,6>{17,45,28,0,16,99}[i];
        t->pev->origin=Vector(900,900,900);f.sphere.push_back(t->edict());}
    auto* glass=f.targets[5];glass->pev->rendermode=kRenderTransTexture;glass->damageResult=FALSE;
    f.targets[7]->pev->takedamage=DAMAGE_NO;
    f.sphere.insert(f.sphere.begin(),f.bomb->edict());
    f.sphere.insert(f.sphere.begin()+1,f.player->edict());
    Require(f.Run(90),"engine sphere nonplayers accepted");
    for(int i=0;i<6;++i)
    {
        auto* t=f.targets[i+2];Check(t->classified==(i<3?i+1:3),"classification17 then45 then28 is original order");
        Check(t->hits.size()==std::size_t(i==3||i==4),"only eligible native sphere objects take damage");
        if(!t->hits.empty())Check(t->hits[0].amount==90&&t->hits[0].bits==DMG_EXPLOSION&&t->hits[0].attacker==f.creator->pev,
            "transparent breakable/wall full damage has no origin-distance falloff");
    }
    Check(f.bomb->hits.empty(),"inflictor excluded from its own sphere");
    }
    for(int stage=0;stage<3;++stage)
    {
        Fixture g;g.player->pev->origin=Vector(500,0,0);auto* a=g.NewTarget(4);auto* b=g.NewTarget(5);
        g.sphere={a->edict(),b->edict()};auto cb=[&]{g.session->RevokeEntityAccess();};
        if(stage==0)a->onPlayer=cb;if(stage==1)a->onClassify=cb;if(stage==2)a->onDamage=cb;
        Check(!g.Run(),"nonplayer virtual cancellation propagates");
        Check(a->hits.size()==std::size_t(stage==2)&&b->hits.empty(),"cancelled nonplayer traversal cannot reach next entity");
    }
}
}
int main()
{
    try{OriginalVectors();DamageAndOrder();ImpulseContract();Cancellation();NonPlayers();}
    catch(const std::exception& e){++bomb_damage_test::failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"zombie_bomb_damage checks="<<bomb_damage_test::checks<<" failures="<<bomb_damage_test::failures<<'\n';
    return bomb_damage_test::failures?1:0;
}
