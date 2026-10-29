// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK enginefuncs ABI and production one-recipient death-message method.
// Captured calls are not a network transport or the death-owner player loop.
#include "precompiled.h"
#include "../../native/csol/server/scenario_messages.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
namespace
{
int checks{},failures{},registrationId=96,recipientOffset=4;
struct Call
{
    char kind{};int a{},b{};const float* origin{};const edict_t* recipient{};std::string name;
};
std::vector<Call> calls;
std::vector<std::uint8_t> wire;
std::function<void(edict_t*)> onBegin;
void Check(bool value,const char* label)
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
int Register(const char* name,int size)
{calls.push_back({'r',size,0,nullptr,nullptr,name});return registrationId;}
int Offset(const edict_t* recipient)
{calls.push_back({'o',recipientOffset,0,nullptr,recipient,{}});return recipientOffset;}
void Begin(int destination,int type,const float* origin,edict_t* recipient)
{calls.push_back({'m',destination,type,origin,recipient,{}});if(onBegin)onBegin(recipient);}
void Byte(int value)
{calls.push_back({'b',value,0,nullptr,nullptr,{}});wire.push_back(static_cast<std::uint8_t>(value));}
void Short(int value)
{calls.push_back({'s',value,0,nullptr,nullptr,{}});wire.push_back(static_cast<std::uint8_t>(value));wire.push_back(static_cast<std::uint8_t>(static_cast<unsigned>(value)>>8));}
void End(){calls.push_back({'e',0,0,nullptr,nullptr,{}});}
void Clear(){calls.clear();wire.clear();}
void Install()
{
    g_engfuncs={};g_engfuncs.pfnRegUserMsg=Register;g_engfuncs.pfnEntOffsetOfPEntity=Offset;
    g_engfuncs.pfnMessageBegin=Begin;g_engfuncs.pfnWriteByte=Byte;g_engfuncs.pfnWriteShort=Short;g_engfuncs.pfnMessageEnd=End;
    registrationId=96;recipientOffset=4;onBegin={};Clear();
}
bool Packet(const edict_t* recipient,int id,int monster)
{
    const int signedShort=monster<32768?monster:monster-65536;
    return calls.size()==5 && calls[0].kind=='o' && calls[0].recipient==recipient &&
        calls[1].kind=='m' && calls[1].a==1 && calls[1].a==MSG_ONE && calls[1].b==id &&
        calls[1].origin==nullptr && calls[1].recipient==recipient &&
        calls[2].kind=='b' && calls[2].a==200 && calls[3].kind=='s' && calls[3].a==signedShort && calls[4].kind=='e' &&
        wire==std::vector<std::uint8_t>{200,static_cast<std::uint8_t>(monster&255),static_cast<std::uint8_t>((monster>>8)&255)};
}
bool NoPacket()
{
    return wire.empty() && std::none_of(calls.begin(),calls.end(),[](const Call& c){return c.kind=='m'||c.kind=='b'||c.kind=='s'||c.kind=='e';});
}
void PayloadAndRecipient()
{
    Install();CCSOLScenarioMessages messages;edict_t recipient{};
    Check(messages.Register() && calls.size()==1 && calls[0].name=="MPToCL" && calls[0].a==-1,"real MPToCL variable-size registration");
    for(int index:{1,255,256,32767,32768,65535})
    {Clear();Check(messages.SendMonsterDeath(&recipient,index) && Packet(&recipient,96,index),"single recipient exact opcode/short bit pattern and order");}
    for(int id:{64,255})
    {registrationId=id;Check(messages.Register(),"message ID boundary registration");Clear();Check(messages.SendMonsterDeath(&recipient,32768)&&Packet(&recipient,id,32768),"MessageBegin uses assigned ID independently of death opcode");}
    for(int team:{1,2})
    {
        recipient.v.team=team;recipient.v.health=0;recipient.v.deadflag=DEAD_DEAD;recipient.v.flags=FL_DORMANT;recipient.pvPrivateData=nullptr;
        Clear();Check(messages.SendMonsterDeath(&recipient,300)&&Packet(&recipient,255,300),"single-packet adapter does not filter recipient team life flags or private data");
    }
    onBegin=[&](edict_t* ed){ed->free=TRUE;ed->v.team=0;ed->pvPrivateData=nullptr;};Clear();
    Check(messages.SendMonsterDeath(&recipient,65535)&&Packet(&recipient,255,65535),"Begin hook mutation does not truncate copied scalar packet");
    onBegin={};recipient.free=FALSE;
    g_engfuncs.pfnRegUserMsg=nullptr;Clear();
    Check(messages.SendMonsterDeath(&recipient,1)&&Packet(&recipient,255,1),"registered send needs no registrar callback");
}
void InvalidInputs()
{
    Install();CCSOLScenarioMessages messages;edict_t recipient{};
    Check(!messages.SendMonsterDeath(&recipient,1)&&calls.empty(),"unregistered death sends nothing");
    Check(messages.Register(),"register invalid-input fixture");
    for(int index:{std::numeric_limits<int>::min(),-1,0,65536,std::numeric_limits<int>::max()})
    {Clear();Check(!messages.SendMonsterDeath(&recipient,index)&&calls.empty(),"invalid monster index rejected before engine side effects");}
    Clear();Check(!messages.SendMonsterDeath(nullptr,1)&&calls.empty(),"null recipient rejected before offset query");
    recipient.free=TRUE;Clear();Check(!messages.SendMonsterDeath(&recipient,1)&&calls.empty(),"free recipient rejected before offset query");recipient.free=FALSE;
    recipientOffset=0;Clear();Check(!messages.SendMonsterDeath(&recipient,1)&&calls.size()==1&&calls[0].kind=='o'&&NoPacket(),"world offset0 rejected without partially opening packet");recipientOffset=4;
    for(int invalid:{63,256})
    {
        registrationId=invalid;Clear();Check(!messages.Register()&&messages.MessageId()==0,"failed registration clears prior ID");
        Clear();Check(!messages.SendMonsterDeath(&recipient,1)&&calls.empty(),"failed registration cannot send with previous ID");
        registrationId=96;Check(messages.Register(),"independent valid registration restored");
    }
}
void MissingFunctions()
{
    Install();CCSOLScenarioMessages messages;edict_t recipient{};Check(messages.Register(),"missing-functions fixture registration");const auto complete=g_engfuncs;
    for(int missing=0;missing<5;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnEntOffsetOfPEntity=nullptr;
        if(missing==1)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==2)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==3)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==4)g_engfuncs.pfnWriteShort=nullptr;
        Clear();Check(!messages.SendMonsterDeath(&recipient,32768)&&calls.empty()&&NoPacket(),"every missing required function rejects before any half-packet");
        Check(messages.MessageId()==96,"missing send API does not erase registration");
        g_engfuncs=complete;Clear();Check(messages.SendMonsterDeath(&recipient,32768)&&Packet(&recipient,96,32768),"restored engine function permits full next packet");
    }
}
}
int main()
{
    PayloadAndRecipient();InvalidInputs();MissingFunctions();
    std::cout<<"death-message "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
