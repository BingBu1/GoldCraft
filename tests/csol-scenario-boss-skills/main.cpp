// SPDX-License-Identifier: GPL-3.0-or-later
// Preparation only: pure SK code and controlled resource callbacks, no Boss AI.
#include "../../native/csol/scenario/boss_skills.hpp"
#include "../csol-scenario-difficulty/support.hpp"
#include <cmath>

using namespace csol::scenario;
using namespace difficulty_test;
namespace
{
constexpr std::string_view Header = "temp,id,type,ratio,cooltime,aniTime,aniSeq,damageDelay,damage,damageDegree,damageHeight,damageDist,forceAttackTime,startDistMin,startDistMax,startHealthMin,startHealthMax,approachDist,startMsg,kickSide,kickUp,rushPower\r\n";
using Cells = std::array<std::string,22>;
Cells Row()
{
    return {"label","1002","3","3","3","2.5","zbs_attack1","0.5","100","0","2","2",
        "4","2","3","25","75","2","-1","400","250","30"};
}
std::string Csv(const Cells& cells)
{
    std::string text;
    for (std::size_t i=0;i<cells.size();++i) { if (i) text += ','; text += cells[i]; }
    return text+"\r\n";
}
BossSkillTable Parse(const std::string& text)
{
    BossSkillTable table; ConfigError error;
    Require(ReadBossSkills(text,table,error),"authored complete SK parses through production reader");
    return table;
}
BossSkillTable Table(std::initializer_list<int> weights)
{
    auto cells=Row(); std::string text(Header);
    for (int weight:weights) { cells[3]=std::to_string(weight);text+=Csv(cells); }
    auto table=Parse(text);
    for (auto& row:table.rows)
    { row.startDistanceMinSquared=0;row.startDistanceMaxSquared=100;row.startHealthMin=0;row.startHealthMax=100; }
    return table;
}
bool Selected(const std::optional<BossSkillCandidate>& selected,std::size_t row,int slot)
{ return selected&&selected->row==row&&selected->playerSlot==slot; }

void Parsing()
{
    auto row=Row(); ConfigError error{9,"stale"}; BossSkillTable table;
    row[1]=" +66538key";row[2]="65535";row[3]="65790x";row[18]="65535";
    row[4]=" +3seconds";row[6]="CaseSensitive";
    const auto text=std::string("arbitrary header\r\n")+Csv(row)+Csv(Row());
    Require(ReadBossSkills(text,table,error),"fixed columns ignore header names and accept decimal numeric prefixes");
    const auto& first=table.rows[0];
    Check(error.message.empty()&&table.rows.size()==2&&table.groups.size()==1&&
        table.groups.at(1002)==std::vector<std::size_t>{0,1}&&first.recordIndex==1&&
        first.id==1002&&first.type==-1&&first.ratio==254&&first.startMessage==-1&&first.sequence=="CaseSensitive",
        "low signed16 key collision appends in physical order and preserves sequence bytes");
    Check(first.cooltime==3&&first.animationTime==2.5f&&first.damageDelay==0.5f&&first.damage==100&&
        first.damageCosine==1&&first.damageHeight==78.73999786376953125f&&
        first.damageDistance==78.73999786376953125f&&first.startDistanceMinSquared==6199.9873046875f&&
        first.startDistanceMaxSquared==13949.9716796875f&&first.approachDistanceSquared==6199.9873046875f&&
        first.forceAttackTime==4&&first.startHealthMin==25&&first.startHealthMax==75&&
        first.kickSide==400&&first.kickUp==250&&first.rushPower==30,
        "SK times and damage retain units while linear and squared distance use original float32 conversions");
    row=Row();row[9]="90";
    auto angle=Parse(std::string(Header)+Csv(row));
    Check(angle.rows[0].damageCosine>0.0000002f&&angle.rows[0].damageCosine<0.0000004f,
        "degree90 uses the original truncated float pi and no half-angle conversion");
    row[1]="";row[2]="";row[3]="";row[18]="";row[6]="";
    auto emptyShort=Parse(std::string(Header)+Csv(row));
    Check(emptyShort.rows[0].id==0&&emptyShort.rows[0].type==0&&emptyShort.rows[0].ratio==0&&
        emptyShort.rows[0].startMessage==0&&emptyShort.rows[0].sequence.empty(),
        "empty short fields become zero and valid empty sequence remains empty");
    const auto stable=table.source.records.size();
    for (const auto bad: {std::string{},std::string("nan"),std::string("1e40"),std::string("0x1")})
    {
        row=Row();row[4]=bad;
        Check(!ReadBossSkills(std::string(Header)+Csv(row),table,error)&&
            !error.message.empty()&&error.byteOffset==Header.size()&&table.source.records.size()==stable&&table.rows[0].type==-1,
            "missing, nonfinite or unsupported required float rejects at its row without replacing previous table");
    }
    row=Row();row[13]="1e20";
    Check(!ReadBossSkills(std::string(Header)+Csv(row),table,error)&&table.rows[0].type==-1,
        "finite input that overflows squared distance fails transactionally");
    const auto encrypted=Encrypt(std::string(Header)+Csv(Row()));
    Check(ReadBossSkillResource(Bytes(encrypted),table,error)&&table.rows.size()==1,
        "actual encrypted reader accepts complete authored SK and strips only trailing padding");
    const auto embedded=Encrypt(std::string(Header)+std::string("x\0x",3)+Csv(Row()));
    Check(!ReadBossSkillResource(Bytes(embedded),table,error)&&table.rows.size()==1,
        "encrypted interior NUL rejects without changing successful table");
}

void Original24(const std::filesystem::path& path)
{
    BossSkillTable table; ConfigError error; const auto encrypted=Read(path);
    Require(encrypted.size()==2360&&ReadBossSkillResource(Bytes(encrypted),table,error),
        "current common deadend encrypted SK decodes through the production resource reader");
    Require(table.rows.size()==24&&table.groups.size()==1&&table.groups.at(1002).size()==24,
        "original24 rows all belong to skill1002, not monster30200");
    struct Expected { int type,ratio,damage,lo,hi;std::string_view sequence; };
    constexpr std::array<Expected,24> expected{{
        Expected{3,3,100,75,100,"zbs_attack1"},{3,2,140,50,74,"zbs_attack1"},{3,2,240,25,49,"knife_attack1"},{3,2,300,0,24,"knife_attack1"},
        {4,3,80,75,100,"zbs_attack2"},{4,2,120,50,74,"zbs_attack2"},{4,2,200,25,49,"knife_attack2"},{4,2,250,0,24,"knife_attack2"},
        {5,1,120,75,100,"zbs_attack3"},{5,1,150,50,74,"zbs_attack3"},{5,1,250,25,49,"knife_attack3"},{5,1,300,0,24,"knife_attack3"},
        {5,255,50,90,99,"zbs_attack3"},{6,1,100,75,100,"zbs_attack_hole"},{6,1,120,50,74,"zbs_attack_hole"},
        {6,1,280,25,49,"knife_attack_hole"},{6,1,350,0,24,"knife_attack_hole"},{6,254,50,90,99,"zbs_attack_hole"},
        {7,1,30,75,100,"zbs_attack_bomb"},{7,1,45,50,74,"zbs_attack_bomb"},{7,1,100,25,49,"knife_attack_bomb"},{7,1,150,0,24,"knife_attack_bomb"},
        {8,255,0,50,100,"scene_appear"},{9,255,0,0,49,"scene_knife"}}};
    for (std::size_t i=0;i<expected.size();++i)
    {
        const auto& a=table.rows[i];const auto& e=expected[i];
        Check(a.recordIndex==i+1&&table.groups.at(1002)[i]==i&&a.type==e.type&&a.ratio==e.ratio&&
            a.damage==e.damage&&a.startHealthMin==e.lo&&a.startHealthMax==e.hi&&a.sequence==e.sequence&&
            a.startMessage==(i==23?205:-1),"each original row retains independent physical order, health range, priority, damage and animation");
    }
    Check(table.rows[0].animationTime==2.7f&&table.rows[0].damageDelay==0.93f&&
        table.rows[18].animationTime==11&&table.rows[18].damageDelay==3&&
        table.rows[22].animationTime==7&&table.rows[23].animationTime==10,
        "original fractional and scene timing comes from SK rather than model duration");
    BossSkills skills(std::move(table));BossSkillCandidates candidates;std::optional<BossSkillCandidate> chosen;
    const std::array players{BossSkillPlayer{1,2,0,true,{0,0,0}},BossSkillPlayer{32,2,0,true,{0,0,0}}};
    for (const auto& [health,row]:std::array<std::pair<float,std::size_t>,3>{{{95,12},{100,22},{49,23}}})
    {
        Require(skills.Candidates(1002,{1,health,100,{}},players,1,candidates,error),"original candidates generated");
        Check(!candidates.NeedsRandom()&&skills.Choose(candidates,{},chosen,error)&&Selected(chosen,row,1),
            "first original255 row chooses first eligible slot before any later priority row");
    }
    Require(skills.Candidates(1002,{1,49.5f,100,{}},players,1,candidates,error),"fractional health candidates generated");
    Check(candidates.entries.empty()&&skills.Choose(candidates,{},chosen,error)&&!chosen,
        "the original49-to50 health gap is not rounded or patched into a skill");
}

void CandidateWeights()
{
    BossSkills skills(Table({2,3}));BossSkillCandidates candidates;ConfigError error;
    const BossSkillSubject subject{1,50,100,{}};
    const std::array players{BossSkillPlayer{1,2,0,true,{}},BossSkillPlayer{3,2,0,true,{}},BossSkillPlayer{32,2,0,true,{}}};
    Require(skills.Candidates(1002,subject,players,1,candidates,error),"multiplayer candidates generated");
    Check(candidates.total==15&&candidates.entries.size()==6&&candidates.NeedsRandom(),
        "each row-player pair contributes weight, including slot32 beyond original reserve10");
    std::optional<BossSkillCandidate> chosen;
    struct Draw { int random;std::size_t row;int slot; };
    for (const auto& draw:std::array{Draw{0,0,1},Draw{2,0,3},Draw{4,0,32},Draw{6,1,1},Draw{9,1,3},Draw{14,1,32},Draw{17,0,3}})
        Check(skills.Choose(candidates,draw.random,chosen,error)&&Selected(chosen,draw.row,draw.slot),
            "one UCRT remainder uses strict cumulative inequality in row-then-slot order");
    const auto before=chosen;
    for (auto bad:std::array<std::optional<std::int32_t>,3>{{std::nullopt,-1,32768}})
        Check(!skills.Choose(candidates,bad,chosen,error)&&Selected(chosen,before->row,before->playerSlot),
            "weighted branch requires one valid UCRT draw and failure preserves output");
    const std::array one{players[0]};
    for (const auto& test:std::array<std::pair<int,int>,3>{{{-1,2},{1,-2},{254,1}}})
    {
        BossSkills signedSkills(Table({test.first,test.second}));
        Require(signedSkills.Candidates(1002,subject,one,1,candidates,error),"signed-weight candidates generated");
        const int random=test.first==254?254:0;
        Check(candidates.NeedsRandom()&&signedSkills.Choose(candidates,random,chosen,error)&&
            Selected(chosen,test.first==1?0:1,1),"negative total follows signed remainder and254 stays ordinary weighted selection");
    }
    BossSkills priority(Table({-255,255,255}));
    Require(priority.Candidates(1002,subject,one,1,candidates,error),"cancelled-priority candidates generated");
    Check(candidates.total==0&&candidates.priority==1&&!candidates.NeedsRandom()&&
        priority.Choose(candidates,{},chosen,error)&&!chosen,"prefix[-255,255] totalzero prevents selection even after priority was found");
    BossSkills malformed(Table({-1}));
    Require(malformed.Candidates(1002,subject,one,1,candidates,error),"negative-only candidate generated");
    Check(!malformed.Choose(candidates,0,chosen,error)&&!error.message.empty(),
        "original no-index signed-weight case reports failure instead of dereferencing candidate[-1]");
}

void GatesAndSharedState()
{
    auto table=Table({1});auto& row=table.rows[0];row.startDistanceMinSquared=25;row.startDistanceMaxSquared=100;
    auto owner=std::make_shared<BossSkills>(table);auto other=owner;BossSkills independent(table);
    BossSkillCandidates candidates;ConfigError error;BossSkillSubject subject{1,50,100,{}};
    const std::array players{
        BossSkillPlayer{1,2,0,true,{0,0,5}},BossSkillPlayer{2,2,0,true,{0,0,10}},
        BossSkillPlayer{3,2,0,true,{0,0,11}},BossSkillPlayer{4,1,0,true,{0,0,5}},
        BossSkillPlayer{5,2,0x80000000,true,{0,0,5}},BossSkillPlayer{6,2,0x80000001,true,{0,0,5}},
        BossSkillPlayer{7,2,0,false,{0,0,5}},BossSkillPlayer{32,2,0,true,{0,0,4}}};
    Require(owner->Candidates(1002,subject,players,1,candidates,error),"gate candidates generated");
    Check(candidates.entries.size()==3&&candidates.entries[0].playerSlot==1&&candidates.entries[1].playerSlot==2&&
        candidates.entries[2].playerSlot==6,"3D distance endpoints include, exact dormant flags reject, alive enemy slots remain");
    auto invalid=players;invalid[1].slot=1;const auto previous=candidates.entries.size();
    Check(!owner->Candidates(1002,subject,invalid,1,candidates,error)&&candidates.entries.size()==previous,
        "duplicate or unsorted input slots reject transactionally");
    Require(owner->Candidates(1002,subject,players,0,candidates,error),"deadline equality checked");
    Check(candidates.entries.empty()&&owner->State(0)->sequence==-1&&owner->State(0)->cooldownDeadline==0,
        "new shared row starts sequenceFFFF/deadlinezero and strict time equality cannot select");
    BossSkillClocks clocks;
    Check(owner->CommitSelection(0,10,clocks)&&other->State(0)->cooldownDeadline==13&&independent.State(0)->cooldownDeadline==0,
        "selection commits cooldown to shared resource row but not independent map owner");
    Check(clocks.target.duration==6.5f&&clocks.target.deadline==16.5f&&clocks.target.startedAt==10&&
        clocks.end.deadline==16.5f&&clocks.selection.duration==7&&clocks.selection.deadline==17&&
        clocks.startCheck.duration==-1&&clocks.startCheck.deadline==9&&clocks.force.deadline==14,
        "selection initializes all five original clocks in float addition order");
    Check(!clocks.end.Elapsed(16.5f)&&clocks.end.Elapsed(16.75f),"timer uses strict greater-than deadline");
    Check(owner->CacheSequence(0,-1)&&other->State(0)->sequence==-1&&other->CacheSequence(0,65538)&&
        owner->State(0)->sequence==2&&owner->CacheSequence(0,8)&&owner->State(0)->sequence==2,
        "failed lookup remains retryable and first successful low16 cache is shared and retained");
    Check(owner->Interrupt(0,11,1)&&owner->State(0)->cooldownDeadline==13&&owner->Interrupt(0,11,-1)&&
        other->State(0)->cooldownDeadline==11,"only host sequence state minusone rewrites shared cooldown to now");
    Require(other->Candidates(1002,subject,players,11,candidates,error),"post-interrupt equality checked");
    Check(candidates.entries.empty(),"interrupt at now still cannot select at the same instant");
    Require(other->Candidates(1002,subject,players,11.25f,candidates,error),"post-interrupt later time checked");
    Check(candidates.entries.size()==3&&!owner->CommitSelection(1,20,clocks)&&!owner->CacheSequence(1,2)&&!owner->Interrupt(1,20,-1),
        "later time permits shared row and invalid row mutation APIs reject");
}

bool Current(const void* value) { return *static_cast<const bool*>(value); }
void Resources()
{
    Files files;BossSkillResources resources;ResourceError error;bool current=true;
    const std::string path="maps/zbs_sk_test.cso";
    files.contents[path]=Encrypt(std::string(Header)+Csv(Row()));
    Require(resources.Load("test",files.Api(),Current,&current,error),"resource owner loads encrypted SK through filesystem");
    auto old=resources.Snapshot();BossSkillClocks clocks;
    Require(old&&old->CommitSelection(0,10,clocks),"old snapshot has live shared state");
    for (const bool release:{false,true})
    {
        int callbacks{};
        const auto reenter=[&](std::string_view name)
        {
            if(name!=path)return;++callbacks;
            Check(!resources.Snapshot()&&!resources.Clear()&&
                !resources.Load("test",files.Api(),Current,&current,error),
                "load/release callbacks cannot clear or publish a half-built snapshot");
        };
        if(release)files.onFree=reenter;else files.onLoad=reenter;
        Check(resources.Load("test",files.Api(),Current,&current,error)&&callbacks==1&&error.detail.message.empty()&&
            resources.Snapshot()!=old&&resources.Snapshot()->State(0)->cooldownDeadline==0&&old->State(0)->cooldownDeadline==13,
            "outer success publishes new state after release and restores diagnostic without mutating retained old snapshot");
        files.onLoad={};files.onFree={};
    }
    for(const bool release:{false,true})
    {
        current=true;
        const auto cancel=[&](std::string_view name){if(name==path)current=false;};
        if(release)files.onFree=cancel;else files.onLoad=cancel;
        Check(!resources.Load("test",files.Api(),Current,&current,error)&&!resources.Snapshot()&&
            error.detail.message.empty()&&files.buffers.empty(),"session cancellation at load/release leaves no published table or stale error and frees buffer");
        files.onLoad={};files.onFree={};
    }
    current=true;files.contents.erase(path);
    Check(resources.Load("test",files.Api(),Current,&current,error)&&resources.Snapshot()->Table().rows.empty(),
        "missing SK succeeds with empty current-map table, never previous-map data");
    files.contents[path]="";
    Check(resources.Load("test",files.Api(),Current,&current,error)&&resources.Snapshot()->Table().rows.empty()&&files.buffers.empty(),
        "existing zero-length file follows original empty-table behavior and releases its buffer");
    files.contents[path]="bad";
    Check(!resources.Load("test",files.Api(),Current,&current,error)&&!resources.Snapshot()&&error.path==path&&files.buffers.empty(),
        "nonzero invalid ciphertext size rejects instead of silently becoming missing");
    Check(resources.Clear()&&!resources.Snapshot()&&old->State(0)->cooldownDeadline==13,
        "owner clear drops publication while an explicit old borrower retains only its old table");
}
}

int main(int argc,char** argv)
{
    if(argc!=2){std::cerr<<"usage: csol-scenario-boss-skills <common/maps/zbs_sk_zs_deadend.cso>\n";return 2;}
    try { Parsing();Original24(argv[1]);CandidateWeights();GatesAndSharedState();Resources(); }
    catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<error.what()<<'\n';}
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
