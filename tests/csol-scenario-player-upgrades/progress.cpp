// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace upgrade_test
{
void ProgressCases()
{
    const auto tables=RealTables();ConfigError error;
    {
        UpgradeProgress progress;auto events=std::make_shared<Events>();auto attributes=Attributes();
        const auto* type=tables->classes.Find(45);for(std::size_t i=0;i<5;++i)attributes[i].cap=type->increases[i]+1;
        Require(progress.Initialize(tables,45,attributes,0,error),"class45 state initialization");
        // Original thresholds for total levels1..9 sum to18600. Nine awards
        // must traverse repeated attributes, not a unique five-stat sequence.
        Require(progress.AddAdjustedExperience(18600,events,error),"class45 nine-level award");
        Check(events->increases==std::vector<UpgradeStat>{UpgradeStat::Health,UpgradeStat::Damage,UpgradeStat::Armor,
            UpgradeStat::Health,UpgradeStat::Damage,UpgradeStat::Armor,UpgradeStat::Ammo,UpgradeStat::Ammo,UpgradeStat::Speed},
            "actual XP traversal preserves fixed original hdahdamms repetition");
        const auto* state=progress.State();
        Check(state->level==10&&state->experience==0&&state->cursor==0&&events->heals==9&&events->full==0,"class45 exact thresholds consumed once and cycle wraps");
        Check(state->attributes[0].level==3&&state->attributes[1].level==3&&state->attributes[2].level==2&&
            state->attributes[3].level==3&&state->attributes[4].level==3,"nine awards update five independent base stats");
    }
    {
        UpgradeProgress progress;auto events=std::make_shared<Events>();
        Require(progress.Initialize(tables,0,Attributes(),0,error),"ordered bonus fixture");
        events->bonus[0]=1;
        events->callback=[&](std::string_view step,UpgradeStat stat){
            if(step=="changed"&&stat==UpgradeStat::Health){events->bonus[1]=2;events->bonus[3]=3;events->bonus[2]=4;}
            if(step=="read"&&stat==UpgradeStat::Damage)Check(progress.State()->attributes[0].bonus==1,"later ability observes applied HP bonus");
            if(step=="changed"&&stat==UpgradeStat::Speed){Check(progress.State()->attributes[3].bonus==3,"speed consumer sees prior armor query");events->bonus[4]=5;}
        };
        Require(progress.RefreshBonuses(events,error),"sequential bonus refresh");
        Check(events->order==std::vector<std::string>{"read:0","changed:0","read:1","read:3","read:2","changed:2","read:4"},
            "queries and actual HP/speed callback boundaries interleave in original order");
        Check(progress.State()->bonus==15&&progress.State()->attributes[4].bonus==5,"ammo query observes preceding speed callback mutation");
    }
    {
        UpgradeProgress progress;auto events=std::make_shared<Events>();auto attributes=Attributes(1);
        attributes[0].cap=2;attributes[1].cap=2;events->bonus[1]=1;
        Require(progress.Initialize(tables,0,attributes,0,error),"effective-full fixture");
        Require(progress.AddAdjustedExperience(2005,events,error),"threshold uses base plus current bonuses");
        const auto* state=progress.State();
        Check(state->level==2&&state->bonus==1&&state->cap==3&&state->experience==0,"bonus reaches full effective level before base cap");
        Check(events->full==1&&events->heals==0,"full effective level calls original full event and never heals");
        Check(state->cursor==1&&state->attributes[1].level==1,"next-stat search uses base below cap even when bonus fills it");
    }
    {
        UpgradeProgress progress;auto events=std::make_shared<Events>();auto attributes=Attributes();
        attributes[0]={25,19,2};
        Require(progress.Initialize(tables,45,attributes,123,error),"character switch permits retained base above new cap");
        Check(progress.State()->attributes[0].level==25&&progress.State()->attributes[0].bonus==2&&
            progress.State()->experience==123&&progress.State()->bonus==0,"initialization preserves base XP and per-stat bonus but clears aggregate");
        float health{};Require(progress.StatValue(UpgradeStat::Health,health),"effective stat table value after lower cap");
        Check(health==820,"stat lookup clamps effective HP at the new cap without destroying base");
        Require(progress.RefreshBonuses(events,error),"refresh above-cap character");
        Check(progress.State()->bonus==-6&&progress.State()->attributes[0].level==25,"aggregate upper clamp may be negative and never truncates retained base");
    }
    {
        UpgradeProgress progress;auto events=std::make_shared<Events>();
        Require(progress.Initialize(tables,0,Attributes(),0,error),"replacement cancellation fixture");bool replaced=false;
        events->callback=[&](std::string_view step,UpgradeStat){if(step=="read"&&!replaced){replaced=true;Require(progress.Initialize(tables,45,Attributes(),777,error),"callback installs independent new state");}};
        Check(!progress.AddAdjustedExperience(2000,events,error),"state replacement cancels old award");
        Check(progress.State()->experience==777&&progress.State()->level==1&&events->increases.empty()&&events->heals==0,"old callback cannot deduct XP or heal replacement state");
    }
}
}
