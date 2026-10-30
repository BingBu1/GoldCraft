// SPDX-License-Identifier: GPL-3.0-or-later
// Real core and separately compiled consumers; engine message/time edges are fixtures.
#include "native/csol/client/runtime.hpp"
#include "native/csol/client/scenario_abilities.hpp"
#include "native/csol/client/scenario_upgrades.hpp"
#include "native/csol/scenario_abilities.hpp"
#include "native/csol/scenario_upgrades.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <source_location>
#include <sstream>
#include <stdexcept>

namespace test {
int checks{},forwards{},sounds{},syncs{};
float now=100;
std::map<std::string,pfnUserMsgHook> hooks;
void Check(bool condition,std::source_location loc=std::source_location::current()) {
    ++checks;
    if(!condition)throw std::runtime_error("snapshot test line "+std::to_string(loc.line()));
}
bool Near(float first,float second) {return std::abs(first-second)<=0.00001f;}
int Previous(const char*,int,void*) {++forwards;return 73;}
void Log(const char*,...) {}
std::vector<std::byte> Skill(unsigned opcode,int type,float value,float duration) {
    std::vector<std::byte> result{std::byte(opcode),std::byte(type),std::byte(type>>8)};
    for(float number:opcode==0x25?std::vector<float>{duration}:std::vector<float>{value,duration})
        for(unsigned i=0;i<4;++i)result.push_back(std::byte(std::bit_cast<unsigned>(number)>>(i*8)));
    return result;
}
std::vector<std::byte> Upgrade(unsigned opcode,int value,int cap=0) {
    std::vector<std::byte> result{std::byte(opcode)};
    for(int number:{value,cap})for(unsigned i=0;i<4;++i)
        result.push_back(std::byte(std::bit_cast<unsigned>(number)>>(i*8)));
    result.resize(opcode>=6&&opcode<=14&&opcode%2==0?9:5);
    return result;
}
std::vector<std::byte> Snapshot(unsigned type,float total,float elapsed,float duration=0,unsigned version=1) {
    std::vector<std::byte> result{std::byte(version),std::byte(type),std::byte(type>>8)};
    for(float value:{total,elapsed,duration})for(unsigned i=0;i<4;++i)
        result.push_back(std::byte(std::bit_cast<unsigned>(value)>>(i*8)));
    return result;
}
int Send(const char* name,std::vector<std::byte> data) {
    return hooks.at(name)(name,int(data.size()),data.data());
}
std::string Abilities() {std::ostringstream out;csol::client::ScenarioAbilitiesDiagnostics(out);return out.str();}
std::string Upgrades() {std::ostringstream out;csol::client::ScenarioUpgradesDiagnostics(out);return out.str();}
void Has(const std::string& value,const char* needle) {Check(value.find(needle)!=std::string::npos);}
}

CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE) {return nullptr;}
namespace csol::client {
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
void SynchronizeSession() {++test::syncs;}
std::vector<std::byte> ReadResource(const char*) {throw std::runtime_error("unexpected resource read");}
}

void TestOriginalProtocol() {
    using namespace test;
    using namespace csol::abilities;
    State state;
    Message message;
    Check(Decode(Skill(0x24,1,50,0),message)==ParseResult::Accepted);
    state.Apply(message);state.Advance(20);
    Check(Near(state.Fraction(),.4f));
    // A native cooldown replay starts over; notification duration is not elapsed.
    Check(Decode(Skill(0x24,1,30,20),message)==ParseResult::Accepted);
    state.Apply(message);state.Advance(0);Check(state.Elapsed()==0&&state.Fraction()==0);
    state.Advance(10);Check(Near(state.Fraction(),1.0f/3));
    Check(Decode(Skill(0x26,1,.4f,20),message)==ParseResult::Accepted);
    state.Apply(message);state.Advance(10);Check(state.Elapsed()==0&&Near(state.Fraction(),.4f));
    Check(Decode(Skill(0x25,3,0,2),message)==ParseResult::Accepted);
    state.Apply(message);state.Advance(1);Check(state.Number()==3&&Near(state.NumberTime(),1));
    state.Advance(1);Check(state.Number()==0&&state.NumberTime()==0);
}

void TestSnapshotCore() {
    using namespace test;
    namespace ability=csol::abilities;
    ability::Snapshot output{29,60,25,4};
    const auto valid=Snapshot(1,50,20);
    for(unsigned size=0;size<25;++size) {
        auto bytes=valid;bytes.resize(size);
        const auto result=ability::DecodeSnapshot(bytes,output);
        Check(result==(size==15?ability::ParseResult::Accepted:ability::ParseResult::Malformed));
    }
    for(unsigned version=0;version<256;++version) {
        Check(ability::DecodeSnapshot(Snapshot(1,50,20,0,version),output)==
            (version==1?ability::ParseResult::Accepted:ability::ParseResult::Malformed));
    }
    for(unsigned type=1;type<=55;++type) {
        Check(ability::DecodeSnapshot(Snapshot(type,50,20),output)==ability::ParseResult::Accepted);
        Check(output.type==type&&output.cooldown==50&&output.elapsed==20&&output.duration==0);
    }
    for(unsigned type:{56u,65535u})Check(ability::DecodeSnapshot(Snapshot(type,50,20),output)==ability::ParseResult::Malformed);
    Check(ability::DecodeSnapshot(Snapshot(0,0,0),output)==ability::ParseResult::Accepted);
    for(const auto& bytes:{Snapshot(0,50,0),Snapshot(0,0,0,1),Snapshot(1,50,51)})
        Check(ability::DecodeSnapshot(bytes,output)==ability::ParseResult::Malformed);
    for(float invalid:{-1.0f,-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        for(const auto& bytes:{Snapshot(1,invalid,0),Snapshot(1,50,invalid),Snapshot(1,50,20,invalid)}) {
            output={29,60,25,4};
            Check(ability::DecodeSnapshot(bytes,output)==ability::ParseResult::Malformed);
            Check(output.type==29&&output.cooldown==60&&output.elapsed==25&&output.duration==4);
        }
    }
    ability::State state;
    Check(state.Restore({1,50,20,0}));Check(state.Visible()&&Near(state.Fraction(),.4f)&&state.Elapsed()==20);
    Check(state.FlashCount()==0&&state.FlashAlpha()==0);
    const auto frame=state.Layout(1280,720,false);
    Check(frame.count==3);Check(frame.quads[0].y==599&&frame.quads[0].height==30);
    state.Advance(10);Check(state.Elapsed()==30&&Near(state.Fraction(),.6f));
    Check(!state.Restore({1,50,51,0}));Check(state.Elapsed()==30&&Near(state.Fraction(),.6f));
    Check(state.Restore({29,50,50,0}));state.Advance(.01);
    Check(state.Type()==29&&state.Fraction()==1&&state.FlashCount()==0&&state.FlashAlpha()==0);
    Check(state.Restore({29,50,49,0}));state.Advance(1);
    Check(state.Fraction()==1&&state.FlashCount()==2&&state.FlashAlpha()==255);
    Check(state.Restore({29,50,49.75f,0}));state.Advance(1);
    Check(state.Fraction()==1&&state.FlashCount()==0);
    Check(state.Restore({0,0,0,0}));Check(!state.Visible()&&state.Type()==0);
    Check(state.Restore({1,.0005f,0,0}));Check(!state.Visible());
    Check(state.Restore({1,.001f,0,0}));Check(state.Visible()&&state.Fraction()==0);
    state.Advance(.0005);Check(Near(state.Fraction(),.5f));
}

void TestProductionMessageChain() {
    using namespace test;
    using namespace csol::client;
    metahook_api_t storage{};api=&storage;
    storage.GetEngineType=[](){return ENGINE_GOLDSRC;};
    storage.GetEngineFactory=[]()->CreateInterfaceFn{return nullptr;};
    storage.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return nullptr;};
    storage.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook {
        const auto previous=hooks.contains(name)?hooks.at(name):Previous;
        hooks[name]=hook;return previous;
    };
    engine.GetClientTime=[](){return now;};
    engine.pfnGetCvarFloat=[](const char*){return 1.0f;};
    engine.pfnPlaySoundByNameAtPitch=[](const char*,float,int){++sounds;};
    engine.Con_DPrintf=Log;
    view.health=100;
    // Match production order and separate translation units, retaining every prior handler.
    InitializeScenarioUpgrades();InitializeScenarioAbilities();
    Check(Send("ZBSVer2",Upgrade(4,7))==73);
    Check(Send("ZBSVer2",Upgrade(3,40))==73);
    Check(Send("ZBSVer2",Skill(0x24,1,50,0))==73);
    DrawScenarioAbilities(120,0);Has(Abilities(),"progress=0.4");
    const int before=forwards;
    Check(Send("ResetHUD",{std::byte{0}})==73);Check(forwards==before+1);
    Has(Abilities(),"visible=1 type=1 progress=0.4");
    Has(Upgrades(),"active=1 total=7 exp=0.4");Check(sounds==0);
    now=120;Check(Send("ZBSVer2",Upgrade(5,8))==73);Check(sounds==1);
    Check(Send("ResetHUD",{std::byte{0}})==73);
    Has(Upgrades(),"total=8 exp=0.4 notice=0");Check(sounds==1);
    Check(Send("InitHUD",{std::byte{0}})==73);
    Has(Abilities(),"visible=0");Has(Upgrades(),"active=0 total=0 exp=0");
    // Simulate non-event state replay with original total/stat/XP messages.
    Check(Send("ZBSVer2",Upgrade(4,8))==73);
    Check(Send("ZBSVer2",Upgrade(3,40))==73);
    for(unsigned stat=0;stat<5;++stat)Check(Send("ZBSVer2",Upgrade(6+stat*2,2,21))==73);
    Has(Upgrades(),"active=1 total=8 exp=0.4 notice=0");Check(sounds==1);
    const auto forwardedBeforeSnapshot=forwards;
    Check(Send("CsoSkState",Snapshot(1,50,20))==1);
    Check(forwards==forwardedBeforeSnapshot);Has(Abilities(),"visible=1 type=1 progress=0.4");
    DrawScenarioAbilities(130,0);Has(Abilities(),"progress=0.6");
    Check(Send("ResetHUD",{std::byte{0}})==73);Has(Abilities(),"progress=0.6");
    now=140;Check(Send("CsoSkState",Snapshot(29,50,50))==1);
    DrawScenarioAbilities(140,0);Has(Abilities(),"visible=1 type=29 progress=1 flashes=0");
    for(const auto& bytes:{Snapshot(1,50,20,0,2),Snapshot(56,50,20),Snapshot(1,50,51)}) {
        Check(Send("CsoSkState",bytes)==1);Has(Abilities(),"type=29 progress=1 flashes=0");
    }
    now=std::numeric_limits<float>::quiet_NaN();
    Check(Send("CsoSkState",Snapshot(1,50,20))==1);Has(Abilities(),"type=29 progress=1 flashes=0");
    now=150;Check(Send("CsoSkState",Snapshot(1,50,20))==1);
    DrawScenarioAbilities(149.99f,0);Has(Abilities(),"type=1 progress=0.4");
    DrawScenarioAbilities(151,0);Has(Abilities(),"progress=0.42");
    Check(Send("CsoSkState",Snapshot(0,0,0))==1);Has(Abilities(),"visible=0");
    Check(Send("CsoSkState",Snapshot(29,50,20))==1);Check(Send("InitHUD",{std::byte{0}})==73);
    Has(Abilities(),"visible=0");Has(Upgrades(),"active=0");Check(sounds==1);
    ShutdownScenarioAbilities();ShutdownScenarioUpgrades();
}

int main() {try {
    TestOriginalProtocol();
    TestSnapshotCore();
    TestProductionMessageChain();
    std::cout<<"snapshot and production message chain checks="<<test::checks<<" errors=0\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
