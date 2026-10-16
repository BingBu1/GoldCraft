// Execute the actual production round entry points with observable engine
// adapters. Full collision, Minecraft chunk clearing and live hooks are separate.
#include "goldcraft/gameplay_rules.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
unsigned checks;
#define CHECK(condition) do {++checks;if(!(condition)){std::fprintf(stderr,"round cleanup check %u line %d: %s\n",checks,__LINE__,#condition);std::exit(1);}} while(false)
struct Rules {bool multiplayer=true,m_bRoundTerminating=false;bool IsMultiplayer()const{return multiplayer;}} rules;
Rules* g_pGameRules=&rules;
struct Policy {std::uint64_t epoch=19,revision=1;} policy;
goldcraft::GameplayRules gameplay_rules;
struct Cvar {float value;};
Cvar round_clear_blocks{1},death_drop_items{0};
void GoldCraft_RoundCleanup(){gameplay_rules.configure(round_clear_blocks.value==1,death_drop_items.value!=0);gameplay_rules.cleanup_round();}
bool persist=false,cleanup_persist=false;
unsigned cleanup_depth=0,invalidations=0,physics_resets=0,prunes=0;
int physics;
int* map_physics=&physics;
struct Ledger {unsigned cuts=3,restores=0;void restore(){cuts=0;++restores;}} map_edits;
struct edict_t {int slot=5,serial=7;bool live=true;};
struct MinedEntity {int serial;bool matches(edict_t* entity)const{return entity->live&&entity->serial==serial;}};
std::map<int,MinedEntity> mined_entities;
int ENTINDEX(edict_t* entity){return entity->slot;}
bool Persist(){return persist;}
bool LiveEditTarget(int){return true;}
void InvalidateRequests(){++invalidations;++policy.revision;}
void ClearMapPhysics(){++physics_resets;}
constexpr int at_console=0;
void ALERT(int,const char*,const char*){}
struct Pruner {template<class A,class B>void run(Ledger&,int&,A,B){++prunes;}} background_pruner;
struct GoldCraft_MapMiningRoundCleanup {
    GoldCraft_MapMiningRoundCleanup();~GoldCraft_MapMiningRoundCleanup();
    GoldCraft_MapMiningRoundCleanup(const GoldCraft_MapMiningRoundCleanup&)=delete;
};
#include "../../native/server/round_cleanup.inc"

void Reset(bool retained=false){
    rules={};g_pGameRules=&rules;policy={};gameplay_rules.reset(19);gameplay_rules.configure(true,false);
    persist=retained;cleanup_persist=false;cleanup_depth=invalidations=physics_resets=prunes=0;
    map_physics=&physics;map_edits={};mined_entities={{5,{7}}};round_clear_blocks.value=1;death_drop_items.value=0;
}
void Frames(bool ended){
    rules.m_bRoundTerminating=ended;
    for(int i=0;i<120;++i)GoldCraft_MapMiningFrame();
}
}

int main(){
    edict_t entity;
    for(bool retained:{false,true}){
        Reset(retained);const auto revision=gameplay_rules.revision;
        Frames(false);Frames(true);Frames(false);Frames(true); // includes canceled and repeated ends
        CHECK(gameplay_rules.clear_revision==0&&gameplay_rules.revision==revision);
        CHECK(map_edits.cuts==3&&map_edits.restores==0&&mined_entities.size()==1);
        CHECK(invalidations==0&&physics_resets==0&&policy.revision==1&&prunes==480);
        {
            GoldCraft_MapMiningRoundCleanup outer;
            CHECK(gameplay_rules.clear_revision==1&&policy.revision==2&&invalidations==1);
            CHECK(cleanup_depth==1&&cleanup_persist==retained);
            CHECK(GoldCraft_MapMiningKeepEntity(&entity)==retained);
            CHECK((map_edits.cuts==3)==retained&&map_edits.restores==unsigned(!retained));
            CHECK(mined_entities.size()==unsigned(retained)&&physics_resets==unsigned(!retained));
            {GoldCraft_MapMiningRoundCleanup nested;CHECK(cleanup_depth==2&&gameplay_rules.clear_revision==1&&invalidations==1);}
            CHECK(cleanup_depth==1&&cleanup_persist==retained);
        }
        CHECK(cleanup_depth==0&&!cleanup_persist&&!GoldCraft_MapMiningKeepEntity(&entity));
        Frames(true);CHECK(gameplay_rules.clear_revision==1&&invalidations==1);
        {GoldCraft_MapMiningRoundCleanup next;} // immediate new round, no false frame required
        CHECK(gameplay_rules.clear_revision==2&&invalidations==2);
    }
    Reset();round_clear_blocks.value=0;
    {GoldCraft_MapMiningRoundCleanup direct;} // direct restart, no termination
    CHECK(gameplay_rules.clear_revision==0&&map_edits.cuts==0&&invalidations==1);
    Reset(true);Frames(true);persist=false;
    {GoldCraft_MapMiningRoundCleanup changed;} // sample policy at next start
    CHECK(map_edits.cuts==0&&gameplay_rules.clear_revision==1);
    Reset();Frames(true);persist=true;round_clear_blocks.value=0;death_drop_items.value=1;
    {GoldCraft_MapMiningRoundCleanup changed;CHECK(GoldCraft_MapMiningKeepEntity(&entity));}
    CHECK(map_edits.cuts==3&&gameplay_rules.clear_revision==0&&gameplay_rules.drop_items);
    Reset(true);
    {GoldCraft_MapMiningRoundCleanup scope;
        CHECK(!GoldCraft_MapMiningKeepEntity(nullptr));entity.serial=8;
        CHECK(!GoldCraft_MapMiningKeepEntity(&entity)&&mined_entities.empty());
    }
    Reset();map_physics=nullptr;Frames(true);CHECK(prunes==0&&map_edits.cuts==3);
    Reset();policy.epoch=0;Frames(true);CHECK(prunes==0&&map_edits.cuts==3);
    Reset();g_pGameRules=nullptr;GoldCraft_MapMiningFrame();CHECK(prunes==0&&map_edits.cuts==3);
    Reset();rules.multiplayer=false;Frames(true);CHECK(prunes==0&&map_edits.cuts==3);
    std::printf("Native round cleanup: %u production-entry-point checks passed\n",checks);
}
