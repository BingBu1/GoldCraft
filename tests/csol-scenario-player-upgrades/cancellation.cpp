// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace upgrade_test
{
void CancellationCases()
{
    for(const char* edge:{"experience","bonus","jump","message-begin"})
    {
        Fixture f;f.Configure();auto registry=std::make_shared<CCSOLScenarioPlayers>();
        Require(registry->Bind(f.state),"old generation binds real registry");
        auto nextServices=std::make_shared<Services>();
        auto replacement=CCSOLScenarioPlayerUpgrades::Create(f.player,f.tables,nextServices,f.messages,1,100);
        Require(replacement&&replacement->Configure(45,0,false),"replacement is prepared before callback");
        f.engine.messages.clear();auto oldWall=f.state->WallState();bool switched=false;
        const auto replace=[&](std::string_view at){if(at==edge&&!switched){switched=true;Require(registry->Bind(replacement),"callback atomically replaces player registry slot");f.engine.movement=replacement;}};
        f.services->callback=replace;f.engine.boundary=replace;
        Check(!f.state->AddExperience(2000),"generation change cancels original XP operation at every consumer edge");
        Check(switched&&!f.state->Current()&&!f.state->State()&&oldWall->generation==2,"replacement invalidates old progress and wall generation");
        Check(registry->Upgrades(EHANDLE(&f.player))==replacement&&replacement->State()->level==1&&replacement->State()->experience==0,
            "old call cannot alter replacement XP or registry identity");
        Check(f.player.pev->health==40&&f.services->full==0,"cancellation performs no late heal or full notification");
        Check(!f.engine.open,"callback replacement never leaves a partial message");
        if(std::string_view(edge)=="message-begin")Check(f.engine.messages.size()==1&&f.engine.messages[0].writes=="BLE"&&
            f.engine.messages[0].wire==std::vector<std::uint8_t>{5,2,0,0,0},"already-open scalar total completes then old publication stops");
        f.engine.boundary={};f.services->callback={};
    }
    {
        Fixture f;f.Configure();f.services->callback=[&](std::string_view label){if(label=="experience")f.engine.Destroy(f.player);};
        Check(!f.state->AddExperience(2000)&&!f.state->Current(),"edict serial invalidation cancels actual player XP consumer");
        Check(f.engine.messages.empty(),"no packet to freed player's slot");
    }
    {
        Fixture f;f.Configure();auto registry=std::make_shared<CCSOLScenarioPlayers>();Require(registry->Bind(f.state),"registry clear fixture");
        registry->Clear();Check(!f.state->Current()&&!f.state->State()&&!registry->Find(EHANDLE(&f.player)),"map clear invalidates retained state before edict reuse");
        int amount=5;Check(!f.state->Publish()&&!f.state->AddExperience(1)&&!f.state->ModifyMoney(amount)&&amount==5,"retained stale owner cannot publish or reward after map clear");
    }
}
}
