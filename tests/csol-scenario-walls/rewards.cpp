// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace wall_test
{
void RewardCases()
{
    {
        Engine e;auto& p=e.New<Player>("player");p.m_iAccount=1000;RewardFixture f(p,1.25f,2);
        auto& w=e.Wall();w.BindRules(f.rewards);std::vector<int> balances,amountAtMoney,amountAtXp;
        e.boundary=[&](std::string_view at,CBaseEntity*){if(at=="message-end"&&e.messages.back().type==gmsgMoney){
            f.players->order.emplace_back("money");balances.push_back(p.m_iAccount);amountAtMoney.push_back(f.players->state->accumulatedDamage);
            Check(w.pev->health==1000,"all awards precede base health mutation");}};
        f.players->callback=[&](std::string_view at){if(at=="xp")amountAtXp.push_back(f.players->state->accumulatedDamage);};
        Check(w.TakeDamage(p.pev,p.pev,260,DMG_CLUB)&&w.pev->health==480,"actual base club damage follows reward executor");
        Check(f.players->order==std::vector<std::string>({"modify","money","xp","modify","money","xp"})&&balances==std::vector<int>({1250,1500}),"actual SDK account update ordered before each XP call");
        Check(amountAtMoney==std::vector<int>({260,135})&&amountAtXp==std::vector<int>({135,10})&&f.players->xp==std::vector<int>({100,100}),"threshold125 subtraction is between account and scaled XP50");
        Check(f.players->state->accumulatedDamage==10&&!f.players->state->crediting,"remainder and reentrancy latch restored");
        for(const auto& message:e.messages)if(message.type==gmsgMoney)Check(message.integers.size()==2&&message.integers[1]==0,"native Money is emitted with change tracking disabled");
    }
    {
        Engine e;auto& p=e.New<Player>("player");p.m_iAccount=15950;RewardFixture f(p);
        auto& w=e.Wall("1","0",1);w.BindRules(f.rewards);w.TakeDamage(p.pev,p.pev,375,DMG_POISON);
        Check(p.m_iAccount==16000&&f.players->xp.size()==3&&f.players->state->accumulatedDamage==0,"native account cap does not suppress overkill awards or XP stages");
    }
    for(const char* stage:{"modify","money","xp"})
    {
        Engine e;auto& p=e.New<Player>("player");p.m_iAccount=1000;RewardFixture f(p);auto& w=e.Wall();w.BindRules(f.rewards);
        f.players->callback=[&](std::string_view at){if(at==stage)f.rewards->Invalidate();};
        e.boundary=[&](std::string_view at,CBaseEntity*){if(at=="message-end"&&std::string_view(stage)=="money")f.rewards->Invalidate();};
        Check(!w.TakeDamage(p.pev,p.pev,250,DMG_BULLET)&&w.pev->health==1000,"award callback invalidation stops old damage and later rewards");
        if(std::string_view(stage)=="modify")Check(p.m_iAccount==1000&&f.players->xp.empty()&&f.players->state->accumulatedDamage==250,"cancel before account leaves accumulated amount");
        if(std::string_view(stage)=="money")Check(p.m_iAccount==1200&&f.players->xp.empty()&&f.players->state->accumulatedDamage==250,"account callback cancel prevents threshold subtraction and XP");
        if(std::string_view(stage)=="xp")Check(p.m_iAccount==1200&&f.players->xp.size()==1&&f.players->state->accumulatedDamage==125,"XP callback cancel preserves performed stages only");
    }
    {
        Engine e;auto& p=e.New<Player>("player");RewardFixture f(p);auto& w=e.Wall();w.BindRules(f.rewards);
        f.players->moneyOk=false;Check(!w.TakeDamage(p.pev,p.pev,125,DMG_BULLET)&&p.m_iAccount==0&&f.players->xp.empty()&&!f.rewards->Error().message.empty(),"failed character money provider prevents fake award");
    }
    {
        Engine e;auto& p=e.New<Player>("player");RewardFixture f(p);auto& w=e.Wall();w.BindRules(f.rewards);
        f.players->xpOk=false;Check(!w.TakeDamage(p.pev,p.pev,125,DMG_BULLET)&&p.m_iAccount==200&&w.pev->health==1000&&f.players->state->accumulatedDamage==0&&!f.rewards->Error().message.empty(),"failed XP provider does not replay previous money or claim damage success");
    }
    {
        Engine e;auto& p=e.New<Player>("player");RewardFixture f(p);auto& w=e.Wall();w.BindRules(f.rewards);f.players->missing=true;
        Check(!w.TakeDamage(p.pev,p.pev,125,DMG_BULLET)&&w.pev->health==1000&&!f.rewards->Error().message.empty(),"missing authoritative upgrade state fails explicitly");
    }
    {
        Engine e;auto& p=e.New<Player>("player");RewardFixture f(p);auto& w=e.Wall();w.BindRules(f.rewards);
        f.players->callback=[&](std::string_view at){if(at=="xp")Check(!f.rewards->CreditPlayerDamage(EHANDLE(&p),500),"shared player credit latch prevents nested other-wall award");};
        Check(w.TakeDamage(p.pev,p.pev,125,DMG_BULLET)&&p.m_iAccount==200&&f.players->xp.size()==1,"rejected reentrant credit leaves one real account award");
    }
}
}
