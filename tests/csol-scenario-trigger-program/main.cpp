// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/trigger_program.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace csol::scenario;
namespace
{
int checks{},failures{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
const std::string Header="id,type,map trigger id,wave id,cool time,dest ratio,end wave,hostCrashReset,roundTimeBase\n";
struct Event
{
    char kind{};std::uint16_t id{};float elapsed{};bool spatial{};
    bool operator==(const Event&) const=default;
};
struct Actions : TriggerActions
{
    // Explicit observation state; Current performs no engine/plugin callback.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Current() const override { return observerValid && observerError.message.empty(); }
    std::vector<Event> events;
    std::vector<std::uint16_t> active;
    std::function<void()> callback;
    std::function<void()> onEnd, onRefresh, onGoal;
    void ActivateWave(std::uint16_t id,float elapsed) override
    {events.push_back({'a',id,elapsed,false});active.push_back(id);if(callback)callback();}
    bool EndWave(std::uint16_t id,bool spatial) override
    {
        events.push_back({'e',id,0,spatial});auto found=std::find(active.begin(),active.end(),id);
        const bool removed=found!=active.end();if(removed)active.erase(found);
        if(onEnd)onEnd();return removed;
    }
    void RefreshWaves() override{events.push_back({'r'});if(onRefresh)onRefresh();}
    void GoalReached() override{events.push_back({'g'});if(onGoal)onGoal();}
    void Clear(){events.clear();}
};
TriggerRule Rule(int group,int id,char command='w',int cooldown=0,int ratio=0,
    std::vector<std::uint16_t> waves={},std::vector<std::uint16_t> ends={})
{
    TriggerRule rule;rule.group=static_cast<std::int16_t>(group);rule.trigger=static_cast<std::int16_t>(id);
    rule.command=command;rule.cooldown=static_cast<std::int16_t>(cooldown);rule.destinationRatio=static_cast<std::int8_t>(ratio);
    rule.waves=std::move(waves);rule.endWaves=std::move(ends);return rule;
}
TriggerRuleTable Table(std::initializer_list<TriggerRule> rows)
{
    TriggerRuleTable table;
    for(auto rule:rows){table.groups[rule.group].push_back(table.rows.size());table.rows.push_back(std::move(rule));}
    return table;
}
TriggerVolumes Boxes(std::initializer_list<int> ids)
{
    TriggerVolumes result;for(int id:ids)result.Register({{-10,-20,-30},{10,20,30},id,std::size_t(id+100)});return result;
}
bool Install(TriggerProgram& program,const TriggerRuleTable& table,const TriggerVolumes& volumes,int group=1)
{ConfigError error;const bool ok=program.Install(table,volumes,group,error);Check(ok&&error.message.empty(),"supported trigger group installs");return ok;}
void Parsing()
{
    ConfigError error;TriggerRuleTable table;
    const std::string source=Header+"65537,wSuffix,-65535,\"-5/+7 foo65535 65536 .3.4 7\",65535,255,\"12/12 -65537\",999,text\n"
        "\n1,g,100,,0,50,,,\n-1,k9,-1,1,2,128,3,,\n1,w,7,,,,,\n";
    Check(ReadTriggerRules(source,table,error),"TRG supports header data empty row extras and optional tail");
    Check(table.rows.size()==4&&table.groups.size()==2&&table.groups.at(1)==std::vector<std::size_t>{0,1,3},"groups narrow signed16 and retain source order while blank row skipped");
    const auto& row=table.rows[0];
    Check(row.recordIndex==1&&row.group==1&&row.command=='w'&&row.suffix=="Suffix"&&row.trigger==1&&row.cooldown==-1&&row.destinationRatio==-1,"rule keeps source record and command suffix while integer getters narrow");
    Check(row.waves==std::vector<std::uint16_t>{5,7,65535,0,3,4,7}&&row.endWaves==std::vector<std::uint16_t>{12,12,1},"digit-run scanner treats minus punctuation and letters as separators, preserves duplicates and low16 IDs");
    Check(table.source.sourceBytes==source&&table.source.records[1].fields[7]=="999"&&table.source.records[1].fields[8]=="text","unknown extra columns and exact source bytes preserved without guessed behavior");
    Check(table.rows[1].recordIndex==3&&table.rows[1].command=='g'&&table.rows[1].destinationRatio==50,"blank CSV record retains later source offsets");
    Check(table.rows[2].command=='k'&&table.rows[2].suffix=="9"&&table.rows[2].destinationRatio==-128,"parser preserves commands separately from runtime supported subset");
    Check(table.rows[3].waves.empty()&&table.rows[3].cooldown==0&&table.rows[3].destinationRatio==0&&table.rows[3].endWaves.empty(),"missing numeric/list tail defaults zero/empty like getters");
    TriggerRuleTable prefix;
    Check(ReadTriggerRules(Header+" +65537tail,w, -65535z,42, 5.5, 257extra,\n",prefix,error),"scalar integer prefixes accept sign leading whitespace and trailing nonnumeric text");
    Check(prefix.rows[0].group==1&&prefix.rows[0].trigger==1&&prefix.rows[0].cooldown==5&&prefix.rows[0].destinationRatio==1,"prefix parsing retains int narrowing without floating rounding");
    const auto before=table.source.sourceBytes;
    for(const std::string& bad:std::vector<std::string>{"","id,type\n1,w\n",Header+"1,,4,1,0,0,\n",Header+"abc,w,4,1,0,0,\n",Header+"1,w,4,2147483648,0,0,\n",Header+"1,w,4,1,0,2147483648,\n",Header+"+-1,w,4,1,0,0,\n"})
    {
        Check(!ReadTriggerRules(bad,table,error)&&!error.message.empty(),"malformed or overflowing rule rejects with diagnostic");
        Check(table.source.sourceBytes==before&&table.rows.size()==4,"failed rule parse preserves previously committed table");
    }
    TriggerRuleTable unsupported;Check(ReadTriggerRules(Header+"1,k,2,1,0,0,\n",unsupported,error),"unsupported execution command still structurally parsed");
    TriggerProgram program;Check(!program.Install(unsupported,Boxes({2}),1,error)&&!error.message.empty(),"runtime explicitly rejects unsupported group command");
}
void WaveExecution()
{
    TriggerProgram program;Actions actions;auto volumes=Boxes({1,2,3,4,5});
    auto table=Table({Rule(1,1,'w',3,0,{7,7,8},{7,99}),Rule(2,2,'w',0),Rule(3,3,'w',-1,0,{1}),Rule(4,4,'w',-2,0,{9})});
    Install(program,table,volumes);
    auto r=program.CheckPlayer({0,0,0},2.5f,4,actions);
    Check(r.valid&&r.waveCommands==1&&!r.goalReached,"spatial w first activation result");
    Check(actions.events==std::vector<Event>{{'a',7,2.5f},{'a',7,2.5f},{'a',8,2.5f},{'e',7,0,true},{'e',99,0,true},{'r'}},"wave activation precedes ordered end calls and one final summary refresh");
    Check(actions.active==std::vector<std::uint16_t>{7,8},"capture sink removes first active match only, duplicate activation retained");
    actions.Clear();r=program.CheckPlayer({0,0,0},std::nextafter(5.5f,0.0f),4,actions);
    Check(r.waveCommands==0&&actions.events.empty(),"nextTime greater than elapsed waits just before boundary");
    r=program.ExecuteTrigger(65537,5.5f,4,actions);
    Check(r.waveCommands==1&&actions.events[3]==Event{'e',7,0,false},"exact cooldown boundary fires explicit low16 ID path with spatial notification false");
    actions.Clear();Check(program.CheckPlayer({10,0,0},99,4,actions).waveCommands==0&&actions.events.empty(),"exact box face does not trigger even after cooldown expires");
    Install(program,table,volumes,2);r=program.CheckPlayer({0,0,0},0,4,actions);
    Check(r.waveCommands==1&&actions.events.empty()&&program.UsedTriggers().size()==1&&program.UsedTriggers()[0]==2,"empty one-shot disables and records ID without a summary refresh");
    Check(program.ExecuteTrigger(2,100,4,actions).waveCommands==0,"empty one-shot remains disabled in explicit path");
    Install(program,table,volumes,3);Check(program.CheckPlayer({0,0,0},100,4,actions).waveCommands==0&&actions.events.empty(),"cooldown-minus-one rule starts disabled");
    Install(program,table,volumes,4);r=program.CheckPlayer({0,0,0},0,4,actions);actions.Clear();
    Check(r.waveCommands==1&&program.CheckPlayer({0,0,0},0,4,actions).waveCommands==1,"negative cooldown other than minus-one remains repeatable");
    actions.Clear();
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
        Check(!program.CheckPlayer({0,0,0},invalid,4,actions).valid&&!program.ExecuteTrigger(4,invalid,4,actions).valid&&actions.events.empty(),"nonfinite elapsed explicitly rejected without actions");
    program.BeginMap();Check(program.UsedTriggers().empty()&&program.Arrivals()==0&&program.ExecuteTrigger(1,100,4,actions).waveCommands==0,"BeginMap clears all installed states and used IDs");
}
void OrderingAndSharedState()
{
    TriggerProgram program;Actions actions;auto volumes=Boxes({1,2,3});
    auto table=Table({Rule(1,1,'w',-2,0,{11}),Rule(1,2,'w',-2,0,{22}),Rule(1,3,'w',-2,0,{33})});
    Install(program,table,volumes);auto r=program.CheckPlayer({0,0,0},0,10,actions);
    Check(r.waveCommands==3&&actions.events==std::vector<Event>{{'a',33,0},{'a',22,0},{'a',11,0},{'r'}},"overlapping spatial rules execute reverse installation order and refresh once");
    table=Table({Rule(1,1,'w',-2,0,{100}),Rule(1,2,'w',-2,0,{200}),Rule(1,1,'w',-2,0,{300})});
    program.BeginMap();actions.Clear();Install(program,table,volumes);r=program.CheckPlayer({0,0,0},0,10,actions);
    Check(r.waveCommands==3&&actions.events==std::vector<Event>{{'a',300,0},{'a',200,0},{'a',300,0},{'r'}},"duplicate ID keeps duplicate nodes sharing final overwritten state rather than independent old rules");
    table.rows[2].cooldown=0;program.BeginMap();actions.Clear();Install(program,table,volumes);r=program.CheckPlayer({0,0,0},0,10,actions);
    Check(r.waveCommands==2&&actions.events==std::vector<Event>{{'a',300,0},{'a',200,0},{'r'}},"first duplicate one-shot disables shared state before its second node");
    Check(program.UsedTriggers().size()==1&&program.UsedTriggers()[0]==1,"shared duplicate one-shot appends used ID once");
}
void EndOnlyAndOwnedRules()
{
    TriggerProgram program;Actions actions;auto volumes=Boxes({1});
    auto table=Table({Rule(1,1,'w',-2,0,{}, {7,7,99})});Install(program,table,volumes);
    auto r=program.CheckPlayer({0,0,0},0,2,actions);
    Check(r.waveCommands==1&&actions.events==std::vector<Event>{{'e',7,0,true},{'e',7,0,true},{'e',99,0,true}},"end-only missing waves do not force summary refresh");
    actions.active={7,8,7,7};actions.Clear();r=program.ExecuteTrigger(1,0,2,actions);
    Check(r.waveCommands==1&&actions.active==std::vector<std::uint16_t>{8,7}&&actions.events==std::vector<Event>{{'e',7,0,false},{'e',7,0,false},{'e',99,0,false},{'r'}},"each repeated end ID removes first match, explicit path refreshes only after actual change");
    table=Table({Rule(1,1,'w',-2,0,{42},{7})});Install(program,table,volumes);
    table.rows[0].waves={99};table.rows[0].endWaves={88};table.rows.clear();table.groups.clear();actions.Clear();
    Check(program.CheckPlayer({0,0,0},0,2,actions).waveCommands==1&&actions.events[0].id==42&&actions.events[1].id==7,"installed runtime owns rule lists after source table mutation/destruction");
    auto bad=Table({Rule(1,1,'k',-2,0,{90})});ConfigError error;actions.Clear();
    Check(!program.Install(bad,volumes,1,error)&&!error.message.empty(),"unsupported group fails explicitly");
    Check(program.CheckPlayer({0,0,0},0,2,actions).waveCommands==1&&actions.events[0].id==42,"unsupported install keeps previous valid state and spatial order");
}
void GroupLifecycle()
{
    TriggerProgram program;Actions actions;auto volumes=Boxes({1,2,3});
    auto table=Table({Rule(1,1,'w',0,0,{10}),Rule(2,2,'w',0,0,{20}),Rule(3,1,'w',0,0,{30})});
    Install(program,table,volumes,1);program.CheckPlayer({0,0,0},0,5,actions);actions.Clear();
    Install(program,table,volumes,1);Check(program.CheckPlayer({0,0,0},1,5,actions).waveCommands==0&&actions.events.empty(),"reinstall used group skips spatial hookup");
    Check(program.ExecuteTrigger(1,1,5,actions).waveCommands==1&&actions.events[0].id==10,"reinstall overwrites fields before used-ID spatial skip so explicit command can fire again");
    Install(program,table,volumes,2);actions.Clear();Check(program.CheckPlayer({0,0,0},0,5,actions).waveCommands==1&&actions.events[0].id==20,"new group spatial index replaces old group nodes");
    Install(program,table,volumes,3);actions.Clear();Check(program.ExecuteTrigger(1,0,5,actions).waveCommands==1&&actions.events[0].id==30,"new group sharing ID overwrites command fields in same runtime");
    Install(program,table,volumes,77);actions.Clear();Check(program.CheckPlayer({0,0,0},10,5,actions).waveCommands==0,"missing group clears spatial order");
    Check(program.ExecuteTrigger(2,10,5,actions).waveCommands==0,"earlier explicit record retains its disabled state across other groups");
    program.ClearUsedTriggers();actions.Clear();Check(program.CheckPlayer({0,0,0},10,5,actions).waveCommands==0,"clearing used list alone does not reinstall spatial nodes");
    Install(program,table,volumes,1);Check(program.CheckPlayer({0,0,0},0,5,actions).waveCommands==1,"clear-used plus reinstall re-enables spatial one-shot");
    auto repeat=Table({Rule(4,3,'w',-2,0,{40}),Rule(5,2,'w',-2,0,{50})});program.BeginMap();Install(program,repeat,volumes,4);
    Install(program,repeat,volumes,5);actions.Clear();
    Check(program.ExecuteTrigger(3,0,5,actions).waveCommands==1&&actions.events[0].id==40,"unmentioned earlier group record remains callable explicitly");
    auto shifted=volumes;shifted.Register({{100,100,100},{200,200,200},3,999});Install(program,repeat,shifted,4);actions.Clear();
    Check(program.CheckPlayer({0,0,0},0,5,actions).waveCommands==1,"group reinstall preserves existing geometry instead of swapping to incoming volume table");
    auto invalid=repeat;invalid.groups[4].push_back(999);ConfigError error;actions.Clear();
    Check(!program.Install(invalid,volumes,4,error)&&program.CheckPlayer({0,0,0},0,5,actions).waveCommands==1,"invalid source index installation is transactional");
}
void UsedCapacityAndMissingGeometry()
{
    TriggerProgram program;Actions actions;TriggerRuleTable table;TriggerVolumes volumes;
    for(int i=0;i<31;++i){table.groups[1].push_back(table.rows.size());table.rows.push_back(Rule(1,100+i,'w',0));}
    Install(program,table,volumes);
    Check(program.CheckPlayer({0,0,0},0,1,actions).waveCommands==0,"missing geometry creates zero boxes that never contain a point");
    for(int i=0;i<31;++i)Check(program.ExecuteTrigger(100+i,0,1,actions).waveCommands==1,"explicit ID executes even with missing zero geometry");
    Check(program.UsedTriggers().size()==30&&program.UsedTriggers()[0]==100&&program.UsedTriggers()[29]==129,"used ID list stops at original30 entries in execution order");
    Check(program.ExecuteTrigger(130,100,1,actions).waveCommands==0,"31st one-shot disables even when used-ID list is full");
    Check(program.ExecuteTrigger(999,0,1,actions).valid&&actions.events.empty(),"unknown explicit ID has no actions");
}
void Goals()
{
    TriggerProgram program;Actions actions;auto volumes=Boxes({1,2,3});
    auto table=Table({Rule(1,1,'g',-1,50),Rule(2,1,'g',0,100),Rule(2,2,'g',0,100),Rule(3,1,'w',-2,0,{10}),Rule(3,2,'g',0,0),Rule(3,3,'w',-2,0,{30})});
    Install(program,table,volumes);
    Check(!program.CheckPlayer({0,0,0},0,5,actions).goalReached&&program.Arrivals()==1,"goal records count arrival despite cooldown-minus-one");
    Check(!program.ExecuteTrigger(1,0,5,actions).goalReached&&program.Arrivals()==2,"direct g contributes to same scan counter");
    Check(program.CheckPlayer({0,0,0},0,5,actions).goalReached&&program.Arrivals()==3&&actions.events==std::vector<Event>{{'g'}},"half of five requires third arrival and invokes goal once this call");
    program.BeginPlayerScanCycle();actions.Clear();Check(!program.CheckPlayer({0,0,0},0,5,actions).goalReached&&program.Arrivals()==1,"new scan resets aggregate arrival count");
    program.BeginPlayerScanCycle();Check(program.CheckPlayer({10,0,0},0,5,actions).valid&&program.Arrivals()==0,"outside player does not increment goal counter");
    Install(program,table,volumes,2);program.BeginPlayerScanCycle();actions.Clear();
    Check(!program.CheckPlayer({0,0,0},0,3,actions).goalReached&&program.Arrivals()==2,"two overlapping g boxes may count same player twice");
    Check(program.CheckPlayer({0,0,0},0,3,actions).goalReached&&program.Arrivals()==3,"goal stops remaining matching records once aggregate threshold met");
    Install(program,table,volumes,3);program.BeginPlayerScanCycle();actions.Clear();auto r=program.CheckPlayer({0,0,0},0,5,actions);
    Check(r.goalReached&&r.waveCommands==1&&actions.events==std::vector<Event>{{'a',30,0},{'g'},{'r'}},"reverse matching stops after g and refreshes prior w changes without executing later w");
    for(int count:{0,128,255,256})
    {
        Install(program,table,volumes,1);program.BeginPlayerScanCycle();actions.Clear();
        Check(program.ExecuteTrigger(1,0,count,actions).goalReached&&program.Arrivals()==1,"goal aliveCT parameter follows signed-byte narrowing includingzero and negative values");
    }
    auto negative=Table({Rule(1,1,'g',0,-50)});Install(program,negative,volumes);program.BeginPlayerScanCycle();
    Check(program.ExecuteTrigger(1,0,10,actions).goalReached,"negative signed destination ratio retained");
}
void Reentry()
{
    TriggerProgram program;Actions actions;auto volumes=Boxes({1});auto table=Table({Rule(1,1,'w',0,0,{10,11})});Install(program,table,volumes);
    int calls{};actions.callback=[&]
    {
        ++calls;ConfigError error;
        Check(!program.Install(table,volumes,1,error)&&!error.message.empty(),"callback cannot invalidate current state through Install");
        Check(!program.CheckPlayer({0,0,0},0,2,actions).valid&&!program.ExecuteTrigger(1,0,2,actions).valid,"recursive command entry explicitly rejected");
        program.BeginMap();program.ClearUsedTriggers();
    };
    Check(program.CheckPlayer({0,0,0},0,2,actions).waveCommands==1&&calls==2&&program.UsedTriggers().size()==1,"rejected callback mutation leaves current one-shot execution intact");
    actions.callback={};program.BeginMap();Check(program.UsedTriggers().empty(),"normal lifecycle reset remains usable after callbacks");
}
void ActionCancellation()
{
    for(bool explicitId:{false,true})for(bool diagnostic:{false,true})
    {
        TriggerProgram program;Actions actions;
        Install(program,Table({Rule(1,2,'g',0,50),Rule(1,1,'w',0,0,{7,8},{9})}),Boxes({1,2}));
        actions.active={9};
        actions.callback=[&]{actions.observerValid=false;if(diagnostic)actions.observerError={3,"activation stopped"};};
        const auto result=explicitId?program.ExecuteTrigger(1,4,2,actions):program.CheckPlayer({0,0,0},4,2,actions);
        Check(!result.valid&&!result.waveCommands&&!result.goalReached&&program.UsedTriggers().empty()&&program.Arrivals()==0,
            "action cancellation stops the program before once-only state or later goal");
        Check(actions.events==std::vector<Event>{{'a',7,4}}&&actions.active==std::vector<std::uint16_t>{9,7},
            "first callback side effect remains while second activation end and summary are suppressed");
        actions.callback={};actions.observerValid=true;actions.observerError={};actions.Clear();
        const auto retry=program.ExecuteTrigger(1,4,2,actions);
        Check(retry.valid&&retry.waveCommands==1&&program.UsedTriggers().size()==1,
            "cancelled once-only rule was not disabled and can be explicitly retried by a fresh valid caller");
    }
    {
        TriggerProgram program;Actions actions;
        Install(program,Table({Rule(1,1,'w',0,0,{}, {7,8})}),Boxes({1}));
        actions.active={7,8};actions.onEnd=[&]{actions.observerValid=false;};
        const auto result=program.CheckPlayer({0,0,0},4,2,actions);
        Check(!result.valid&&!result.waveCommands&&program.UsedTriggers().empty()&&
            actions.events==std::vector<Event>{{'e',7,0,true}}&&actions.active==std::vector<std::uint16_t>{8},
            "program checks after EndWave and preserves only the consumer's completed first removal");
    }
    for(bool explicitId:{false,true})
    {
        TriggerProgram program;Actions actions;
        Install(program,Table({Rule(1,1,'w',0,0,{7}),Rule(1,2,'g',0,50)}),Boxes({1,2}));
        actions.onGoal=[&]{actions.observerValid=false;};
        const auto result=explicitId?program.ExecuteTrigger(2,4,2,actions):program.CheckPlayer({0,0,0},4,2,actions);
        Check(!result.valid&&!result.goalReached&&!result.waveCommands&&program.Arrivals()==1&&
            actions.events==std::vector<Event>{{'g'}},
            "goal callback cancellation retains one arrival but reports no goal or following action");
    }
    {
        TriggerProgram program;Actions actions;
        Install(program,Table({Rule(1,1,'w',0,0,{7})}),Boxes({1}));
        actions.onRefresh=[&]{actions.observerError={6,"summary stopped"};};
        const auto result=program.ExecuteTrigger(1,4,2,actions);
        Check(!result.valid&&result.waveCommands==1&&program.UsedTriggers().size()==1&&
            actions.events==std::vector<Event>{{'a',7,4},{'r'}},
            "summary failure changes validity without rolling back an already completed command");
    }
    for(bool diagnostic:{false,true})
    {
        TriggerProgram program;Actions actions;
        Install(program,Table({Rule(1,1,'g',0,50)}),Boxes({1}));
        actions.observerValid=false;if(diagnostic)actions.observerError={2,"entry unavailable"};
        Check(!program.CheckPlayer({0,0,0},4,2,actions).valid&&!program.ExecuteTrigger(1,4,2,actions).valid&&
            !program.ExecuteTrigger(404,4,2,actions).valid&&actions.events.empty()&&program.Arrivals()==0,
            "invalid entry rejects spatial known and unknown explicit requests before any arrival/action");
    }
}
class Reader
{
    std::ifstream file;
public:
    explicit Reader(const char* path):file(path,std::ios::binary){if(!file)throw std::runtime_error("cannot open independent fixture");}
    std::uint32_t U32()
    {
        unsigned char bytes[4];file.read(reinterpret_cast<char*>(bytes),4);if(!file)throw std::runtime_error("truncated fixture");
        return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
    }
    int I32(){return std::bit_cast<std::int32_t>(U32());}
    float Float(){return std::bit_cast<float>(U32());}
    std::array<float,3> Vector3(){std::array<float,3> values;for(auto& f:values)f=Float();return values;}
    std::string String(){std::string text(U32(),'\0');file.read(text.data(),text.size());if(!file)throw std::runtime_error("truncated string");return text;}
    std::vector<std::uint16_t> List(){const auto count=U32();std::vector<std::uint16_t> result;for(std::uint32_t i=0;i<count;++i)result.push_back(static_cast<std::uint16_t>(U32()));return result;}
    bool Done(){return file.peek()==std::char_traits<char>::eof();}
};
std::string ReadFile(const char* path)
{
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot open actualCSV");
    return {std::istreambuf_iterator<char>(input),{}};
}
void Actual(const char* csvPath,const char* rulesPath,const char* boxesPath)
{
    const auto csv=ReadFile(csvPath);TriggerRuleTable table;ConfigError error;
    Check(ReadTriggerRules(csv,table,error),"actual decodedCSO TRG loads through production parser");
    Reader rules(rulesPath);Check(rules.U32()==0x4c555254u,"independentTRUL rule signature");const auto count=rules.U32();
    Check(count==29&&table.rows.size()==29,"actual29 rules");
    std::vector<TriggerRule> expectedRules;std::map<std::int16_t,std::vector<std::size_t>> expectedGroups;
    for(std::uint32_t i=0;i<count;++i)
    {
        TriggerRule expected;expected.recordIndex=rules.U32();const auto offset=rules.U32();expected.group=static_cast<std::int16_t>(rules.I32());
        expected.command=static_cast<char>(rules.U32());expected.suffix=rules.String();expected.trigger=static_cast<std::int16_t>(rules.I32());
        expected.waves=rules.List();expected.cooldown=static_cast<std::int16_t>(rules.I32());expected.destinationRatio=static_cast<std::int8_t>(rules.I32());expected.endWaves=rules.List();
        const auto fields=rules.U32();std::vector<std::string> raw;for(std::uint32_t j=0;j<fields;++j)raw.push_back(rules.String());
        Check(i<table.rows.size(),"actual expected row index exists");if(i>=table.rows.size())continue;
        const auto& actual=table.rows[i];
        Check(actual.recordIndex==expected.recordIndex&&actual.group==expected.group&&actual.command==expected.command&&actual.suffix==expected.suffix&&actual.trigger==expected.trigger&&actual.waves==expected.waves&&actual.cooldown==expected.cooldown&&actual.destinationRatio==expected.destinationRatio&&actual.endWaves==expected.endWaves,"all actual typed rule fields match independent decodedCSV oracle");
        Check(actual.recordIndex<table.source.records.size()&&table.source.records[actual.recordIndex].byteOffset==offset&&table.source.records[actual.recordIndex].fields==raw,"all actual raw columns and exact byte offsets preserved");
        expectedGroups[expected.group].push_back(expectedRules.size());expectedRules.push_back(std::move(expected));
    }
    Check(rules.Done()&&table.groups==expectedGroups&&table.source.sourceBytes==csv,"actual full rule transport source and group order agree");
    Check(table.groups.size()==2&&table.groups.at(0).size()==1&&table.groups.at(1).size()==28,"actual groups0 one rule and1 twenty-eight rules");
    for(const auto& [group,indices]:expectedGroups)
    {
        std::vector<int> ids;
        for(auto index:indices)
        {
            const auto& rule=expectedRules[index];
            Check(rule.cooldown==0&&(rule.command=='w'||(rule.command=='g'&&rule.destinationRatio==50)),"independent current actual schema assumptions for first-call oracle are explicit");
            ids.push_back(rule.trigger);
        }
        std::sort(ids.begin(),ids.end());Check(std::adjacent_find(ids.begin(),ids.end())==ids.end(),"actual groups have unique trigger IDs for independent first-call spatial oracle");
        (void)group;
    }
    Reader boxes(boxesPath);Check(boxes.U32()==0x56475254u,"independentTRGV box signature");const auto boxCount=boxes.U32();
    TriggerVolumes volumes;std::map<int,TriggerVolume> expectedBoxes;
    for(std::uint32_t i=0;i<boxCount;++i)
    {
        TriggerVolume volume;volume.entityIndex=boxes.U32();volume.id=boxes.I32();boxes.I32();boxes.Vector3();boxes.Vector3();
        volume.mins=boxes.Vector3();volume.maxs=boxes.Vector3();boxes.Vector3();boxes.Vector3();const int key=boxes.I32();
        const auto keys=boxes.U32();for(std::uint32_t j=0;j<keys;++j){boxes.String();boxes.String();}
        volumes.Register(volume);expectedBoxes[key]=volume;
    }
    Check(boxes.Done()&&boxCount==30&&expectedBoxes.size()==29,"actual volumes fixture fully consumed30markers29keys");
    auto expectedEvents=[](const TriggerRule& rule,float elapsed,bool spatial)
    {
        std::vector<Event> result;
        if(rule.command=='g'){result.push_back({'g'});return result;}
        for(auto id:rule.waves)result.push_back({'a',id,elapsed});
        for(auto id:rule.endWaves)result.push_back({'e',id,0,spatial});
        if(!rule.waves.empty())result.push_back({'r'});return result;
    };
    for(const auto& rule:expectedRules)
    {
        TriggerProgram program;Actions actions;Install(program,table,volumes,rule.group);
        const auto r=program.ExecuteTrigger(rule.trigger,12.25f,1,actions);
        Check(r.valid&&r.waveCommands==(rule.command=='w'?1u:0u)&&r.goalReached==(rule.command=='g'),"actual explicit rule result matches original command kind");
        Check(actions.events==expectedEvents(rule,12.25f,false),"actual explicit dispatch emits expected ordered waves goal and refresh via capture sink");
        if(rule.command=='w')Check(program.UsedTriggers().size()==1&&program.UsedTriggers()[0]==rule.trigger,"actual one-shot appends exact used trigger ID");
        const auto found=expectedBoxes.find(rule.trigger);Check(found!=expectedBoxes.end(),"every actual rule has independently extracted geometry");
        if(found==expectedBoxes.end())continue;
        std::array<float,3> center;for(std::size_t axis=0;axis<3;++axis)center[axis]=(found->second.mins[axis]+found->second.maxs[axis])*0.5f;
        program.BeginMap();Install(program,table,volumes,rule.group);actions.Clear();
        std::vector<Event> expectedSpatial;std::size_t expectedCommands{};bool expectedGoal{},changed{};
        const auto& group=expectedGroups.at(rule.group);
        // Independent real-data oracle for the first spatial call only. Current
        //29 actual rows have unique IDs within each group and cooldown0.
        for(auto it=group.rbegin();it!=group.rend();++it)
        {
            const auto& candidate=expectedRules[*it];const auto at=expectedBoxes.find(candidate.trigger);if(at==expectedBoxes.end())continue;
            bool inside=true;for(std::size_t axis=0;axis<3;++axis)inside&=(double(at->second.mins[axis])<double(center[axis])&&double(center[axis])<double(at->second.maxs[axis]));
            if(!inside)continue;
            if(candidate.command=='g'){expectedSpatial.push_back({'g'});expectedGoal=true;break;}
            ++expectedCommands;
            for(auto id:candidate.waves){expectedSpatial.push_back({'a',id,1.5f});changed=true;}
            for(auto id:candidate.endWaves)expectedSpatial.push_back({'e',id,0,true});
        }
        if(changed)expectedSpatial.push_back({'r'});
        const auto spatial=program.CheckPlayer(center,1.5f,1,actions);
        Check(spatial.valid&&spatial.waveCommands==expectedCommands&&spatial.goalReached==expectedGoal,"actual center chooses matching original boxes in reverse rule order");
        Check(actions.events==expectedSpatial,"actual spatial ordered capture matches independent rules/geometry oracle");
    }
    const auto* fifty=volumes.Find(50);
    Check(fifty&&fifty->entityIndex==681&&fifty->mins==std::array<float,3>{1679,-2481,-1},"actual rule50 uses final BSP geometry not stale fallback");
    std::cout<<"actual deadend TRG29rules/2groups and30BSPmarkers/29boxes; capture actions only, no monster or GameRules execution\n";
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==1){Parsing();WaveExecution();OrderingAndSharedState();EndOnlyAndOwnedRules();GroupLifecycle();UsedCapacityAndMissingGeometry();Goals();Reentry();ActionCancellation();}
        else if(argc==5&&std::string_view(argv[1])=="--actual")Actual(argv[2],argv[3],argv[4]);
        else throw std::runtime_error("usage: program tests [--actual decodedCSV independentTRUL independentTRGV]");
    }
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::cout<<"trigger program checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
