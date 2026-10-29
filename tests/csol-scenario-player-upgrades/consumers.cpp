// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace upgrade_test
{
void ConsumerCases()
{
    {
        Fixture f(1.5f);f.Configure();f.services->xpFactor=1.25f;
        Require(f.state->AddExperience(7),"separate EXP_RATIO and difficulty award");
        Check(f.services->xpInputs==std::vector<int>{7}&&f.state->State()->experience==12,"7 times1.25 truncates8 before difficulty1.5 gives12, not13");
        Require(f.state->AddExperience(-1),"negative raw XP ignored");
        f.player.pev->deadflag=DEAD_DEAD;Require(f.state->AddExperience(2000),"dead player raw XP ignored");
        Check(f.services->xpCalls==1&&f.state->State()->experience==12,"raw negative/dead gates precede character modifiers");
    }
    {
        // Wall0x14 -> EXP_RATIO -> generic0x12 have three distinct truncation
        // boundaries. This uses real wall rewards, registry and AddAccount.
        Fixture f;f.Configure();difficulty_test::Files files;
        std::string csv="header\r\n9,arbitrary";
        for(int c=2;c<=21;++c)csv+=","+std::to_string(c==18||c==20?1.5f:c==21?1.0f:99.0f);
        files.contents[difficulty_test::DefaultPath]=difficulty_test::Encrypt(csv+"\r\n");
        DifficultyResources difficulty;ResourceError resource;
        Require(difficulty.BeginMap("upgrade_wall",1,files.Api(),resource),"real difficulty resource for wall XP chain");
        float ratio{};ConfigError error;Require(difficulty.Parameter(15,0x12,ratio,error),"generic XP difficulty uses column0x12");
        auto state=CCSOLScenarioPlayerUpgrades::Create(f.player,f.tables,f.services,f.messages,ratio,2);
        Require(state&&state->Configure(0,0,false),"wall consumer binds current generic difficulty");
        auto players=std::make_shared<CCSOLScenarioPlayers>();Require(players->Bind(state),"real player registry for wall reward");
        f.engine.movement=state;f.services->xpFactor=1.25f;f.engine.messages.clear();
        RoundTable rounds;rounds.source.records.resize(5);
        rounds.source.records[2].fields={"threshold","125"};rounds.source.records[3].fields={"money","200"};rounds.source.records[4].fields={"xp","5"};
        auto rewards=CCSOLScenarioWallRewards::Create(rounds,difficulty,players,error);Require(bool(rewards),"real reward settings use wall difficulty0x14");
        f.services->callback=[&](std::string_view label){if(label=="experience")Check(f.player.m_iAccount==200&&state->WallState()->accumulatedDamage==0,"actual account then signed threshold subtraction precede XP service");};
        Require(rewards->CreditPlayerDamage(EHANDLE(&f.player),125),"wall amount reaches real upgrade provider");
        Check(f.services->xpInputs==std::vector<int>{7}&&state->State()->experience==12,"wall5*1.5->7, EXP7*1.25->8, generic8*1.5->12, not merged14");
        Check(f.engine.messages.size()==1&&f.engine.messages[0].type==77&&f.engine.messages[0].writes=="LBE"&&
            f.engine.messages[0].wire==std::vector<std::uint8_t>{200,0,0,0,0},"real SDK AddAccount emits actual money packet before XP");
        Check(state->WallState()->attackLevel==1&&state->State()->level==1,"legacy wall attack remains separate from modern XP level");
    }
    {
        Fixture f;bool first=true;
        f.services->initialize=[&](int,std::array<UpgradeAttribute,5>& a){if(first){a[0].level=25;first=false;}};
        f.Configure();Require(f.state->AddExperience(123),"retain a nonzero experience remainder");
        f.state->WallState()->accumulatedDamage=77;
        Require(f.state->Configure(45,0x44,true),"switch to lower-cap real class45");
        const auto* state=f.state->State();
        Check(state->attributes[0].level==25&&state->attributes[0].cap==19&&state->experience==123&&state->bonus==0,
            "character Configure preserves over-cap base and XP without early clamping or reset");
        Check(f.state->WallState()->accumulatedDamage==77&&f.state->WallState()->attackLevel==1&&
            f.state->WallState()->characterFeature==0x44&&f.state->WallState()->elevenLevelLimit,"character switch preserves legacy reward accumulation and updates wall feature metadata");
        Check(f.player.pev->health==40,"configuration never heals");
    }
    {
        Fixture f;f.Configure();f.services->jumpOk=false;
        Check(!f.state->AddExperience(2000),"actual void SDK ResetMaxSpeed failure propagates to XP caller");
        Check(f.engine.resetHooks==1&&f.engine.movementCalls==1&&f.services->jumpCalls==1&&f.engine.rejected,
            "actual hook chain and SDK body reach movement and failed required JUMP service");
        Check(f.state->State()->level==2&&f.state->State()->experience==2000&&f.player.pev->health==40,
            "failed speed update stops before heal and threshold subtraction");
        Check(!f.state->Error().message.empty(),"failed upgrade has a diagnostic");
    }
    {
        Fixture f;f.services->initialize=[](int,std::array<UpgradeAttribute,5>& a){for(auto& v:a)v.cap=1;a[0].cap=2;a[1].cap=2;};
        f.Configure();f.services->bonus[1]=1;
        Require(f.state->AddExperience(2005),"real player reaches effective cap with bonus");
        Check(f.services->full==1&&f.player.pev->health==40&&f.player.pev->max_health==140,
            "actual SDK consumer applies max health but full event never heals");
        Check(f.state->State()->level==2&&f.state->State()->bonus==1&&f.state->State()->cursor==1,"effective-full next-stat uses retained base levels");
    }
    {
        Fixture f;f.Configure();f.services->bonus[2]=4;
        Require(f.state->RefreshBonuses(),"speed bonus invokes SDK reset");
        Check(f.player.pev->maxspeed==244&&f.player.pev->gravity==0.75f,"table speed adds to native default and real gravity field is updated");
        f.player.m_bIsVIP=true;f.player.ResetMaxSpeed();Check(f.player.pev->maxspeed==231,"actual native VIP speed remains base227");
        f.player.pev->iuser1=OBS_CHASE_LOCKED;const int calls=f.engine.movementCalls;f.player.ResetMaxSpeed();
        Check(f.player.pev->maxspeed==900&&f.engine.movementCalls==calls,"observer SDK path never adds scenario speed");
        f.player.pev->iuser1=OBS_NONE;f.engine.rules->m_bFreezePeriod=TRUE;f.player.ResetMaxSpeed();
        Check(f.player.pev->maxspeed==1&&f.engine.movementCalls==calls,"multiplayer freeze retains native speed1 without scenario callback");
    }
    {
        Fixture f;f.Configure();int money=16777217;
        Require(f.state->ModifyMoney(money),"absent money ability");Check(money==16777217,"absence preserves int beyond float exact range");
        f.services->money=0;Require(f.state->ModifyMoney(money),"present zero-percent ability");Check(money==16777216,"present zero ability intentionally takes original float conversion");
        money=123;f.services->money=50;Require(f.state->ModifyMoney(money),"percent add money consumer");Check(money==184,"money uses base plus percent not ratio multiplier");
    }
    {
        Fixture f;f.Configure(45);
        auto players=std::make_shared<CCSOLScenarioPlayers>();
        Require(players->Bind(f.state),"damage scaling binds the actual configured upgrade state");
        int count=-1;
        f.player.pev->health=0;f.player.pev->deadflag=DEAD_DEAD;f.player.m_iTeam=TERRORIST;
        Require(players->CountDamageScalePlayers(count),"count registered dead opposing-team player");
        Check(count==1,"scaling eligibility has no alive or CT filter");
        f.player.pev->flags=FL_CLIENT|FL_DORMANT|FL_ONGROUND;
        Require(players->CountDamageScalePlayers(count),"count with combined dormant flags");
        Check(count==0,"dormant rejection tests a bit, not equality of the complete flags");
        f.player.pev->flags=FL_CLIENT;
        Require(f.state->Configure(0,0,false),"class zero can have a valid upgrade record");
        Require(players->CountDamageScalePlayers(count),"count configured class zero");
        Check(f.state->State()&&f.state->ConfiguredClass()==0&&count==0,
            "classId zero is excluded independently of a nonnull upgrade record");
        Require(f.state->Configure(45,0,false),"restore a counted upgrade class");
        Check(!f.state->Configure(INT32_MAX,0,false),"unknown character class fails configuration");
        Require(players->CountDamageScalePlayers(count),"count after failed character replacement");
        Check(f.state->ConfiguredClass()<0&&count==0,"failed Configure cannot retain the previous eligible class");
        Require(f.state->Configure(45,0,false),"restore a counted class before publishing failure");
        const auto writeLong=g_engfuncs.pfnWriteLong;g_engfuncs.pfnWriteLong=nullptr;
        const bool published=f.state->Configure(45,0,false);
        g_engfuncs.pfnWriteLong=writeLong;
        Require(players->CountDamageScalePlayers(count),"count after the real HUD publisher rejects a missing writer");
        Check(!published&&f.state->Current()&&f.state->ConfiguredClass()<0&&count==0,
            "failed Configure publication clears eligibility even when the player and upgrade object remain valid");
    }
    {
        Fixture f;f.Configure(45);
        auto players=std::make_shared<CCSOLScenarioPlayers>();
        Require(players->Bind(f.state),"bind first of 32 damage-scale players");
        for(int index=2;index<=32;++index)
        {
            auto& player=f.engine.NewPlayer();
            auto state=CCSOLScenarioPlayerUpgrades::Create(player,f.tables,f.services,f.messages,1.0f,index);
            Require(state&&state->Configure(45,0,false)&&players->Bind(state),"bind actual upgrade state for every native player slot");
        }
        int count{};
        Require(players->CountDamageScalePlayers(count),"count all 32 registered players");
        Check(count==32,"the complete CS player capacity participates in scaling");
        f.engine.globals.maxClients=31;
        Require(players->CountDamageScalePlayers(count),"respect current engine maxClients");
        Check(count==31,"a bound state outside maxClients is not eligible");
        f.engine.globals.maxClients=32;
        auto& last=f.engine.edicts[32];last.free=TRUE;
        Require(players->CountDamageScalePlayers(count),"reject freed player edict");
        Check(count==31,"freed edict is excluded even while registry retains its state");
        last.free=FALSE;++last.serialnumber;
        Require(players->CountDamageScalePlayers(count),"reject player slot reuse");
        Check(count==31,"old upgrade handle cannot count the next occupant of its slot");
    }
    {
        Fixture f;f.Configure(45);
        auto players=std::make_shared<CCSOLScenarioPlayers>();
        Require(players->Bind(f.state),"bind state before an index callback mutation");
        const auto originalIndex=g_engfuncs.pfnIndexOfEdict;
        g_engfuncs.pfnIndexOfEdict=[](const edict_t* ed)
        {
            Engine::active->Step("damage-scale-index");
            return Engine::Index(ed);
        };
        bool removed=false;
        f.engine.boundary=[&](std::string_view label)
        {
            if(label=="damage-scale-index"&&!removed)
            {removed=true;players->Remove(EHANDLE(&f.player));}
        };
        int count=-1;
        Require(players->CountDamageScalePlayers(count),"count cancels a player removed inside ENTINDEX");
        f.engine.boundary={};g_engfuncs.pfnIndexOfEdict=originalIndex;
        Check(removed&&count==0&&!f.state->Current(),"callback invalidation is checked before counting the old registered player");
    }
}
}
