// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK enginefuncs ABI and production message methods; engine callbacks
// capture calls. No live engine, network transport, monster registry or client.
#include "precompiled.h"
#include "../../native/csol/server/scenario_messages.h"
#include "../../native/csol/server/damage_numbers.h"
#include "../../native/csol/scenario/wave_events.hpp"

#include <array>
#include <iostream>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};

namespace
{
int checks{}, failures{}, registrationResult=64;
std::string registrationFailure;
int registrationFailureResult{};
struct Call
{
    char kind;
    int a{},b{};
    const float* origin{};
    edict_t* recipient{};
    std::string name;
};
std::vector<Call> calls;
std::function<void()> onBegin;
std::function<void(char)> onPacketStep;
void Check(bool value,const char* label)
{ ++checks; if (!value) { ++failures; std::cerr<<"FAIL: "<<label<<'\n'; } }
int Register(const char* name,int size)
{ calls.push_back({'r',size,0,nullptr,nullptr,name});return registrationFailure==name?registrationFailureResult:registrationResult; }
void Begin(int destination,int type,const float* origin,edict_t* recipient)
{ calls.push_back({'m',destination,type,origin,recipient,{}});const auto callback=onBegin;if(callback)callback();const auto step=onPacketStep;if(step)step('m'); }
void Byte(int value) { calls.push_back({'b',value,0,nullptr,nullptr,{}});const auto step=onPacketStep;if(step)step('b'); }
void Short(int value) { calls.push_back({'s',value,0,nullptr,nullptr,{}});const auto step=onPacketStep;if(step)step('s'); }
void Long(int value) { calls.push_back({'l',value,0,nullptr,nullptr,{}});const auto step=onPacketStep;if(step)step('l'); }
void String(const char* value) { calls.push_back({'t',0,0,nullptr,nullptr,value});const auto step=onPacketStep;if(step)step('t'); }
void End() { calls.push_back({'e',0,0,nullptr,nullptr,{}});const auto step=onPacketStep;if(step)step('e'); }
void Install()
{
    g_engfuncs={};g_engfuncs.pfnRegUserMsg=Register;g_engfuncs.pfnMessageBegin=Begin;
    g_engfuncs.pfnWriteByte=Byte;g_engfuncs.pfnWriteShort=Short;g_engfuncs.pfnMessageEnd=End;
    calls.clear();registrationResult=64;registrationFailure.clear();registrationFailureResult=0;onBegin={};onPacketStep={};
}
bool RegisteredAsExpected(std::size_t count=3)
{
    constexpr std::array names{"MPToCL","ZBSVer2","MPToCL2"};
    if(count>names.size()||calls.size()!=count)return false;
    for(std::size_t i=0;i<count;++i)
        if(calls[i].kind!='r'||calls[i].name!=names[i]||calls[i].a!=-1)return false;
    return true;
}
bool SentAsExpected(int registeredId,int eventId)
{
    const int signedValue=eventId>=32768 ? eventId-65536:eventId;
    return calls.size()==5 && calls[0].kind=='m' && calls[0].a==MSG_ALL && calls[0].a==2 &&
        calls[0].b==registeredId && calls[0].origin==nullptr && calls[0].recipient==nullptr &&
        calls[1].kind=='b' && calls[1].a==27 && calls[2].kind=='b' && calls[2].a==7 &&
        calls[3].kind=='s' && calls[3].a==signedValue && calls[4].kind=='e';
}
using Payload=std::array<std::uint8_t,4>;
void Registration()
{
    Install();CCSOLScenarioMessages messages;
    Check(messages.MessageId()==0,"fresh object has no guessed registration id");
    Check(!messages.SendWaveEvent(Payload{27,7,193,0}) && calls.empty(),"unregistered send has no engine calls");
    for (int id=64;id<=255;++id)
    {
        registrationResult=id;calls.clear();
        Check(messages.Register() && messages.MessageId()==id,"all and only intended valid byte user-message ids accepted");
        Check(RegisteredAsExpected(),"actual registrar receives MPToCL, ZBSVer2 and MPToCL2 in order with variable length minus-one");
        calls.clear();
        Check(messages.SendWaveEvent(Payload{27,7,193,0}) && SentAsExpected(id,193),"registered engine id is used by real MessageBegin");
    }
    for (const int bad : {-2,-1,0,1,63,256,257,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
    {
        registrationResult=64;calls.clear();Check(messages.Register(),"valid previous registration established before failure");
        registrationResult=bad;calls.clear();
        Check(!messages.Register() && messages.MessageId()==0 && RegisteredAsExpected(1),"invalid first registration clears old id and prevents later channel registration");
        calls.clear();Check(!messages.SendWaveEvent(Payload{27,7,193,0}) && calls.empty(),"failed reregistration never sends with stale id");
    }
    registrationResult=100;Check(messages.Register(),"valid id restored before absent registrar");
    g_engfuncs.pfnRegUserMsg=nullptr;calls.clear();
    Check(!messages.Register() && messages.MessageId()==0 && calls.empty(),"missing registrar clears prior id without engine calls");
    g_engfuncs.pfnRegUserMsg=Register;calls.clear();
    Check(!messages.SendWaveEvent(Payload{27,7,193,0}) && calls.empty(),"restoring callback alone does not resurrect old id");
    registrationResult=200;Check(messages.Register() && messages.MessageId()==200,"fresh successful registration recovers object");
    calls.clear();Check(messages.SendWaveEvent(Payload{27,7,193,0}) && SentAsExpected(200,193),"recovered object uses new id");
    g_engfuncs.pfnWriteLong=Long;edict_t recipient{};
    constexpr std::array channels{"MPToCL","ZBSVer2","MPToCL2"};
    for(std::size_t failed=0;failed<channels.size();++failed)for(int bad:{0,63,256,INT32_MIN,INT32_MAX})
    {
        registrationFailure.clear();calls.clear();Check(messages.Register(),"all three prior ids exist before individual channel failure");
        registrationFailure=channels[failed];registrationFailureResult=bad;calls.clear();
        Check(!messages.Register()&&messages.MessageId()==0&&RegisteredAsExpected(failed+1),
            "registration stops exactly at the failed first, second or third channel");
        calls.clear();
        Check(!messages.SendWaveEvent(Payload{27,7,193,0})&&!messages.SendMonsterKillChain(&recipient,7)&&
            !messages.SendStatusLabel(&recipient,1,false,7,1)&&calls.empty(),
            "partial registration retains none of the prior main, upgrade or status channel ids");
        registrationFailure.clear();calls.clear();Check(messages.Register()&&RegisteredAsExpected(),"next full three-channel registration recovers");
        calls.clear();Check(messages.SendMonsterKillChain(&recipient,7)&&calls.size()==4&&calls[0].b==200,
            "recovered upgrade id sends after any individual registration failure");
    }
}
void PayloadsAndMissingFunctions()
{
    Install();CCSOLScenarioMessages messages;registrationResult=255;Check(messages.Register(),"payload test object registered");
    for (const int id : {0,1,43,193,204,255,256,32767,32768,32769,65534,65535})
    {
        calls.clear();const Payload payload{27,7,static_cast<std::uint8_t>(id&255),static_cast<std::uint8_t>((id>>8)&255)};
        Check(messages.SendWaveEvent(payload),"valid four-byte event accepted");
        Check(SentAsExpected(255,id),"actual short callback receives signed16 expansion with identical wire low16 bits");
    }
    for (const Payload bad : {Payload{0,7,193,0},Payload{26,7,193,0},Payload{28,7,193,0},Payload{27,0,193,0},Payload{27,8,193,0},Payload{255,255,0,0}})
    {
        calls.clear();Check(!messages.SendWaveEvent(bad) && calls.empty(),"wrong opcode or subopcode rejected before any engine side effect");
        Check(messages.MessageId()==255,"bad payload does not erase valid registration");
    }
    const auto complete=g_engfuncs;
    for (int missing=0;missing<4;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==2)g_engfuncs.pfnWriteShort=nullptr;
        if(missing==3)g_engfuncs.pfnMessageEnd=nullptr;
        calls.clear();Check(!messages.SendWaveEvent(Payload{27,7,0,128}) && calls.empty(),"every missing callback rejected before partially opening message");
        Check(messages.MessageId()==255,"missing send callback preserves independent registration");
        g_engfuncs=complete;calls.clear();
        Check(messages.SendWaveEvent(Payload{27,7,0,128}) && SentAsExpected(255,32768),"restored callback permits next complete send");
    }
    g_engfuncs.pfnRegUserMsg=nullptr;calls.clear();
    Check(messages.SendWaveEvent(Payload{27,7,255,255}) && SentAsExpected(255,65535),"already registered send does not require registrar callback");
}
struct Actions final : csol::scenario::WaveEventActions
{
    explicit Actions(CCSOLScenarioMessages& value):messages(value){}
    CCSOLScenarioMessages& messages;
    bool matches{};
    bool lastSend{};
    int broadcasts{};
    std::vector<int> queries;
    void BroadcastEvent(std::span<const std::uint8_t,4> payload) override
    {++broadcasts;lastSend=messages.SendWaveEvent(payload);}
    bool HasSolidMonster(int type) override {queries.push_back(type);return matches;}
};
csol::scenario::WaveRow Event(int type,std::string id)
{
    csol::scenario::WaveRow row;row.spawnPoint=9999;row.types={type,30000};row.weights={0,1};
    row.weightTotal=1;row.eventMessage=std::move(id);return row;
}
void ProductionCombination()
{
    using namespace csol::scenario;
    Install();CCSOLScenarioMessages messages;registrationResult=91;
    Check(messages.Register(),"production combination engine registration ready");
    for(const int type:{9999,30000})
    {
        Actions actions(messages);ConfigError error;calls.clear();
        Check(DispatchWaveEvent(Event(type,"-1"),actions,error)==WaveEventResult::Sent && actions.queries.empty(),"actual dispatcher sentinel bypasses controlled monster query");
        Check(actions.broadcasts==1 && actions.lastSend && SentAsExpected(91,65535),"production dispatcher payload passes real SDK message method into signed engine write");
    }
    Actions blocked(messages);ConfigError error;calls.clear();
    Check(DispatchWaveEvent(Event(40,"32768"),blocked,error)==WaveEventResult::Suppressed && blocked.queries==std::vector<int>{40},"first normal type gates even when second type is30000");
    Check(blocked.broadcasts==0 && calls.empty(),"suppression reaches no engine message callback");
    blocked.matches=true;blocked.queries.clear();calls.clear();
    Check(DispatchWaveEvent(Event(40,"32768"),blocked,error)==WaveEventResult::Sent && blocked.queries==std::vector<int>{40},"query capture true permits production dispatch");
    Check(blocked.broadcasts==1 && blocked.lastSend && SentAsExpected(91,32768),"conditional event emits correct high-bit signed short through engine ABI");
    Actions invalid(messages);calls.clear();
    Check(DispatchWaveEvent(Event(9999,"2147483648"),invalid,error)==WaveEventResult::Invalid && invalid.broadcasts==0 && calls.empty(),"overflow local input is rejected before engine adapter");
    CCSOLScenarioMessages unregistered;Actions absent(unregistered);calls.clear();
    Check(DispatchWaveEvent(Event(9999,"193"),absent,error)==WaveEventResult::Sent && absent.broadcasts==1 && !absent.lastSend && calls.empty(),
        "void event sink reports dispatch separately from engine-send failure; adapter must retain send result");
}

void FragsAndRibbons()
{
    Install();CCSOLScenarioMessages messages;edict_t recipient{};
    Check(messages.Register()&&RegisteredAsExpected(),"three real message channels registered before new senders");
    for(const auto [input,expected]:std::array<std::pair<std::int32_t,int>,7>{
        {{0,0},{65535,-1},{65536,0},{65537,1},{-1,-1},{INT32_MIN,0},{INT32_MAX,-1}}})
    {
        calls.clear();Check(messages.SendPlayerFrags(32,input),"valid scoreboard input sends");
        Check(calls.size()==6&&calls[0].kind=='m'&&calls[0].a==MSG_BROADCAST&&calls[0].b==64&&
            !calls[0].recipient&&calls[1].kind=='b'&&calls[1].a==27&&calls[2].kind=='b'&&calls[2].a==2&&
            calls[3].kind=='b'&&calls[3].a==32&&calls[4].kind=='s'&&calls[4].a==expected&&calls[5].kind=='e',
            "27/2 broadcast writes player byte and original low sixteen score bits");
    }
    for(int slot:{0,33,-1}){calls.clear();Check(!messages.SendPlayerFrags(slot,1)&&calls.empty(),"outside 32 player slots is rejected before opening packet");}
    const std::array<std::uint8_t,7> ids{29,30,31,39,42,57,255};
    for(std::size_t count=1;count<=6;++count)
    {
        calls.clear();Check(messages.SendAnnounceRibbons(&recipient,std::span(ids).first(count)),"one through six candidates accepted");
        bool exact=calls.size()==count+4&&calls[0].a==MSG_ONE&&calls[0].b==64&&calls[0].recipient==&recipient&&
            calls[1].kind=='b'&&calls[1].a==124&&calls[2].kind=='b'&&calls[2].a==static_cast<int>(count)&&calls.back().kind=='e';
        for(std::size_t i=0;i<count&&exact;++i)exact=calls[i+3].kind=='b'&&calls[i+3].a==ids[i];
        Check(exact,"124 unicast preserves candidate order and payload bytes");
    }
    calls.clear();Check(!messages.SendAnnounceRibbons(&recipient,{})&&calls.empty(),"empty ribbon message is rejected");
    Check(!messages.SendAnnounceRibbons(&recipient,ids)&&calls.empty(),"seven candidates cannot overflow six-byte payload");
    Check(!messages.SendAnnounceRibbons(nullptr,std::span(ids).first(1))&&calls.empty(),"null ribbon recipient rejected");
    recipient.free=TRUE;Check(!messages.SendAnnounceRibbons(&recipient,std::span(ids).first(1))&&calls.empty(),"freed ribbon recipient rejected");recipient.free=FALSE;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<4;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteShort=nullptr;
        calls.clear();Check(!messages.SendPlayerFrags(1,1)&&calls.empty(),"scoreboard requires every wire callback before opening packet");
        if(missing<3)Check(!messages.SendAnnounceRibbons(&recipient,std::span(ids).first(1))&&calls.empty(),"ribbon sender requires every byte-message callback");
    }
    g_engfuncs=complete;
    struct Owner{CCSOLScenarioMessages sender;std::vector<std::uint8_t> source{29,30,31,39,42,57};};
    auto owner=std::make_unique<Owner>();Check(owner->sender.Register(),"owned ribbon sender registered");
    auto* sender=&owner->sender;const std::span<const std::uint8_t> source(owner->source);
    calls.clear();onBegin=[&]{owner.reset();Check(!messages.SendPlayerFrags(1,1),"nested packet rejected while ribbon message is open");};
    Check(sender->SendAnnounceRibbons(&recipient,source),"sender finishes after callback destroys sender and payload owner");onBegin={};
    bool stable=!owner&&calls.size()==10&&calls[1].a==124&&calls[2].a==6&&calls.back().kind=='e';
    for(std::size_t i=0;i<6&&stable;++i)stable=calls[i+3].kind=='b'&&calls[i+3].a==ids[i];
    Check(stable,"message uses pre-callback copied payload after source vector is freed");
    calls.clear();Check(messages.SendPlayerFrags(1,1),"packet guard is released after destroyed-owner send");
}

// Distinct ids expose accidentally selecting the wrong registered channel.
int RecoveryRegister(const char* name,int size)
{
    calls.push_back({'r',size,0,nullptr,nullptr,name});
    if(std::string_view(name)=="MPToCL")return registrationResult;
    if(std::string_view(name)=="ZBSVer2")return 217;
    if(std::string_view(name)=="MPToCL2")return 218;
    return 0;
}
void InstallRecovery()
{ Install();g_engfuncs.pfnRegUserMsg=RecoveryRegister;g_engfuncs.pfnWriteLong=Long; }
int RecoveryOffset(const edict_t*)
{ calls.push_back({'o',0,0,nullptr,nullptr,{}});return 4; }
bool RecoveryRegisteredAsExpected()
{
    return calls.size()==3&&calls[0].kind=='r'&&calls[0].name=="MPToCL"&&calls[0].a==-1&&
        calls[1].kind=='r'&&calls[1].name=="ZBSVer2"&&calls[1].a==-1&&
        calls[2].kind=='r'&&calls[2].name=="MPToCL2"&&calls[2].a==-1;
}
bool RecoverySentAsExpected(int messageId,edict_t* recipient,int playerIndex,std::int32_t amount)
{
    // Independent expected wire sequence; do not call the writer's byte helper
    // to produce the oracle for that same writer.
    return calls.size()==7&&calls[0].kind=='m'&&calls[0].a==MSG_ONE_UNRELIABLE&&calls[0].a==8&&
        calls[0].b==messageId&&!calls[0].origin&&calls[0].recipient==recipient&&
        calls[1].kind=='b'&&calls[1].a==206&&calls[2].kind=='s'&&calls[2].a==playerIndex&&
        calls[3].kind=='b'&&calls[3].a==6&&calls[4].kind=='b'&&calls[4].a==4&&
        calls[5].kind=='l'&&calls[5].a==amount&&calls[6].kind=='e';
}
void RecoveryPayloads()
{
    InstallRecovery();CCSOLScenarioMessages messages;edict_t recipient{};recipient.v.flags=FL_CLIENT;
    Check(!messages.SendRecoveryNumber(&recipient,1,0)&&calls.empty(),"unregistered recovery sends no engine call");
    for(int id:{64,96,255})
    {
        registrationResult=id;calls.clear();
        Check(messages.Register()&&messages.MessageId()==id&&RecoveryRegisteredAsExpected(),
            "recovery records three real channels with distinct upgrade/status ids");
        for(int slot:{1,MAX_CLIENTS})for(std::int32_t amount:{0,1,0x12345678,INT32_MAX})
        {
            calls.clear();
            Check(messages.SendRecoveryNumber(&recipient,slot,amount)&&RecoverySentAsExpected(id,&recipient,slot,amount),
                "actual writer sends nine-byte 206/short/style6/flags4/long payload unreliably, including zero and INT32_MAX");
        }
    }
    g_engfuncs.pfnRegUserMsg=nullptr;calls.clear();
    Check(messages.SendRecoveryNumber(&recipient,1,0)&&RecoverySentAsExpected(255,&recipient,1,0),
        "a registered recovery packet does not repeat registration or require entity lookup callbacks");
}
void RecoveryRejections()
{
    InstallRecovery();CCSOLScenarioMessages messages;edict_t recipient{};recipient.v.flags=FL_CLIENT;
    Check(messages.Register(),"recovery rejection setup registers actual sender");
    for(int slot:{INT32_MIN,-1,0,MAX_CLIENTS+1,INT32_MAX})
    {calls.clear();Check(!messages.SendRecoveryNumber(&recipient,slot,1)&&calls.empty(),"invalid player index fails before Begin or any payload write");}
    for(std::int32_t amount:{INT32_MIN,-1})
    {calls.clear();Check(!messages.SendRecoveryNumber(&recipient,1,amount)&&calls.empty(),"negative recovery amount fails before any engine call");}
    calls.clear();Check(!messages.SendRecoveryNumber(nullptr,1,1)&&calls.empty(),"null recovery recipient has no writes");
    recipient.free=TRUE;Check(!messages.SendRecoveryNumber(&recipient,1,1)&&calls.empty(),"free recovery recipient has no writes");
    recipient.free=FALSE;recipient.v.flags=0;
    Check(!messages.SendRecoveryNumber(&recipient,1,1)&&calls.empty(),"non-client recovery recipient has no writes");
    recipient.v.flags=FL_CLIENT;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<5;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteShort=nullptr;
        if(missing==4)g_engfuncs.pfnWriteLong=nullptr;
        calls.clear();Check(!messages.SendRecoveryNumber(&recipient,1,1)&&calls.empty(),
            "every required writer callback is checked before opening recovery packet");
        g_engfuncs=complete;calls.clear();
        Check(messages.SendRecoveryNumber(&recipient,1,0)&&RecoverySentAsExpected(64,&recipient,1,0),
            "failed writer precondition unwinds both guards for the next complete send");
    }
    for(int id:{-1,0,63,256})
    {
        registrationResult=id;calls.clear();Check(!messages.Register()&&messages.MessageId()==0,"bad registration clears prior recovery channel");
        calls.clear();Check(!messages.SendRecoveryNumber(&recipient,1,1)&&calls.empty(),"failed registration cannot emit stale recovery id");
    }
}
void RecoveryReentrancy()
{
    InstallRecovery();CCSOLScenarioMessages first,second;edict_t recipient{};recipient.v.flags=FL_CLIENT;
    Check(first.Register()&&second.Register(),"two independent scenario message owners registered");
    // Make death sends otherwise admissible. A missing OFFSET callback must
    // not be mistaken for rejection by the global open-message guard.
    g_engfuncs.pfnEntOffsetOfPEntity=RecoveryOffset;
    for(char stage:{'m','b','s','l','e'})
    {
        int attempts{};calls.clear();
        onPacketStep=[&](char current)
        {
            if(current!=stage||attempts)return;
            ++attempts;const auto size=calls.size();
            Check(!second.SendWaveEvent(Payload{27,7,193,0})&&
                !second.SendStatusLabel(&recipient,1,false,7,1.0f)&&
                !second.SendMonsterDeath(&recipient,20)&&
                !second.SendRecoveryNumber(&recipient,2,9)&&
                !CSOLWriteDamageNumber(64,&recipient,CSOLDamageNumber{2,6,4,9})&&calls.size()==size,
                "open recovery rejects scenario/status/death/recovery and direct writer nesting before any nested call");
        };
        Check(first.SendRecoveryNumber(&recipient,1,19)&&attempts==1&&RecoverySentAsExpected(64,&recipient,1,19),
            "recovery remains exact across Begin, byte, short, long and End callback reentry");
        onPacketStep={};calls.clear();
        Check(second.SendWaveEvent(Payload{27,7,193,0})&&SentAsExpected(64,193),"recovery releases scenario guard after End");
        calls.clear();Check(CSOLWriteDamageNumber(64,&recipient,CSOLDamageNumber{1,6,4,19})&&
            RecoverySentAsExpected(64,&recipient,1,19),"recovery releases common writer guard after End");
    }
    for(char stage:{'m','b','s','e'})
    {
        int attempts{};calls.clear();
        onPacketStep=[&](char current)
        {
            if(current!=stage||attempts)return;
            ++attempts;const auto size=calls.size();
            Check(!second.SendRecoveryNumber(&recipient,1,7)&&calls.size()==size,
                "open scenario packet rejects recovery without touching common writer or packet stream");
        };
        Check(first.SendWaveEvent(Payload{27,7,193,0})&&attempts==1&&SentAsExpected(64,193),
            "scenario packet stays exact when recovery is requested at each wire boundary");
        onPacketStep={};calls.clear();
        Check(second.SendRecoveryNumber(&recipient,1,0)&&RecoverySentAsExpected(64,&recipient,1,0),
            "scenario End permits the next recovery packet");
    }
    calls.clear();int directAttempts{};
    onBegin=[&]
    {
        ++directAttempts;const auto size=calls.size();
        Check(!second.SendRecoveryNumber(&recipient,1,2)&&calls.size()==size,
            "direct common writer also rejects nested recovery through its own guard");
    };
    Check(CSOLWriteDamageNumber(64,&recipient,CSOLDamageNumber{1,6,4,3})&&directAttempts==1&&
        RecoverySentAsExpected(64,&recipient,1,3),"common writer packet stays intact across nested scenario recovery request");
    onBegin={};calls.clear();
    Check(second.SendRecoveryNumber(&recipient,1,4)&&RecoverySentAsExpected(64,&recipient,1,4),
        "failed nested common-writer request does not strand scenario guard");
}
void RecoveryOwnerRetirement()
{
    InstallRecovery();auto owner=std::make_unique<CCSOLScenarioMessages>();
    Check(owner->Register(),"retiring recovery message owner registered");
    CCSOLScenarioMessages surviving;Check(surviving.Register(),"independent post-retirement sender registered");
    auto* storage=VirtualAlloc(nullptr,sizeof(edict_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(storage!=nullptr,"allocate independent recipient pages for late-read detection");
    if(!storage)return;
    auto* recipient=std::construct_at(static_cast<edict_t*>(storage));recipient->v.flags=FL_CLIENT;
    auto* sender=owner.get();int begins{};DWORD oldProtection{};bool protectedRecipient{};
    calls.clear();onBegin=[&]
    {
        ++begins;owner.reset();
        protectedRecipient=VirtualProtect(storage,sizeof(edict_t),PAGE_NOACCESS,&oldProtection)!=FALSE;
        Check(protectedRecipient,"recipient storage becomes unreadable after Begin");
        const auto size=calls.size();
        Check(!surviving.SendWaveEvent(Payload{27,7,193,0})&&calls.size()==size,
            "retiring message owner cannot release the global scenario guard mid-packet");
    };
    const bool sent=sender->SendRecoveryNumber(recipient,MAX_CLIENTS,INT32_MAX);
    onBegin={};
    Check(sent&&!owner&&begins==1&&RecoverySentAsExpected(64,recipient,MAX_CLIENTS,INT32_MAX),
        "packet completes from scalars after Begin destroys message owner and denies recipient access");
    DWORD ignored{};
    const bool restored=!protectedRecipient||VirtualProtect(storage,sizeof(edict_t),oldProtection,&ignored)!=FALSE;
    Check(restored,"recipient page restored before fixture destruction");
    if(restored)std::destroy_at(recipient);
    Check(VirtualFree(storage,0,MEM_RELEASE)!=FALSE,"release guarded recipient allocation");
    edict_t next{};next.v.flags=FL_CLIENT;calls.clear();
    Check(surviving.SendRecoveryNumber(&next,1,0)&&RecoverySentAsExpected(64,&next,1,0),
        "both message guards recover after actual owner destruction");
}
void Recovery()
{ RecoveryPayloads();RecoveryRejections();RecoveryReentrancy();RecoveryOwnerRetirement(); }

bool KillChainSentAsExpected(int messageId,edict_t* recipient,std::int32_t count)
{
    return calls.size()==4&&calls[0].kind=='m'&&calls[0].a==MSG_ONE&&calls[0].a==1&&
        calls[0].b==messageId&&!calls[0].origin&&calls[0].recipient==recipient&&
        calls[1].kind=='b'&&calls[1].a==1&&calls[2].kind=='l'&&calls[2].a==count&&calls[3].kind=='e';
}
void KillChainPayloadsAndRejections()
{
    InstallRecovery();CCSOLScenarioMessages messages;edict_t recipient{};
    Check(!messages.SendMonsterKillChain(&recipient,0)&&calls.empty(),"unregistered kill chain cannot send guessed ids");
    registrationResult=91;
    Check(messages.Register()&&RecoveryRegisteredAsExpected(),"kill chain registers all three distinct real channels");
    for(std::int32_t count:{INT32_MIN,-65537,-1,0,1,32768,65536,0x12345678,INT32_MAX})
    {
        calls.clear();
        Check(messages.SendMonsterKillChain(&recipient,count)&&KillChainSentAsExpected(217,&recipient,count),
            "kill chain uses registered ZBSVer2 reliable unicast, byte1 and the exact signed32 count including zero and negatives");
    }
    calls.clear();Check(!messages.SendMonsterKillChain(nullptr,1)&&calls.empty(),"null kill-chain recipient rejected before Begin");
    recipient.free=TRUE;Check(!messages.SendMonsterKillChain(&recipient,1)&&calls.empty(),"freed kill-chain recipient rejected before Begin");
    recipient.free=FALSE;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<4;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==2)g_engfuncs.pfnWriteLong=nullptr;
        if(missing==3)g_engfuncs.pfnMessageEnd=nullptr;
        calls.clear();Check(!messages.SendMonsterKillChain(&recipient,1)&&calls.empty(),
            "all four required kill-chain callbacks are validated before opening a partial packet");
        g_engfuncs=complete;calls.clear();
        Check(messages.SendMonsterKillChain(&recipient,-1)&&KillChainSentAsExpected(217,&recipient,-1),
            "restoring each missing callback permits the next signed count send");
    }
    g_engfuncs.pfnRegUserMsg=nullptr;g_engfuncs.pfnWriteShort=nullptr;
    g_engfuncs.pfnEntOffsetOfPEntity=nullptr;g_engfuncs.pfnIndexOfEdict=nullptr;calls.clear();
    Check(messages.SendMonsterKillChain(&recipient,0)&&KillChainSentAsExpected(217,&recipient,0),
        "registered kill-chain writer needs no registrar, short writer, OFFSET, ENTINDEX or client flag lookup");
}
void KillChainReentrancy()
{
    InstallRecovery();CCSOLScenarioMessages first,second;edict_t recipient{};recipient.v.flags=FL_CLIENT;
    Check(first.Register()&&second.Register(),"two independent owners registered for kill-chain reentry");
    g_engfuncs.pfnEntOffsetOfPEntity=RecoveryOffset;
    const std::array<std::uint8_t,1> ribbon{44};
    for(char stage:{'m','b','l','e'})
    {
        int attempts{};calls.clear();
        onPacketStep=[&](char current)
        {
            if(current!=stage||attempts)return;
            ++attempts;const auto size=calls.size();
            Check(!first.SendMonsterKillChain(&recipient,2)&&!second.SendMonsterKillChain(&recipient,3)&&
                !second.SendWaveEvent(Payload{27,7,193,0})&&!second.SendPlayerFrags(1,5)&&
                !second.SendUpgradeTotal(&recipient,1,true)&&!second.SendMonsterHeadHits(&recipient,1)&&
                !second.SendStatusLabel(&recipient,1,false,7,1)&&!second.SendAnnounceRibbons(&recipient,ribbon)&&
                !second.SendMonsterDeath(&recipient,20)&&!second.SendRecoveryNumber(&recipient,1,1)&&calls.size()==size,
                "open kill-chain packet rejects same-owner and independent scenario sends at every callback boundary");
        };
        Check(first.SendMonsterKillChain(&recipient,INT32_MIN)&&attempts==1&&KillChainSentAsExpected(217,&recipient,INT32_MIN),
            "Begin, byte, long and End reentry cannot interleave or truncate the original kill-chain packet");
        onPacketStep={};calls.clear();
        Check(second.SendMonsterKillChain(&recipient,0)&&KillChainSentAsExpected(217,&recipient,0),
            "kill-chain packet guard releases after each End boundary");
    }
    for(char stage:{'m','b','s','e'})
    {
        int attempts{};calls.clear();
        onPacketStep=[&](char current)
        {
            if(current!=stage||attempts)return;
            ++attempts;const auto size=calls.size();
            Check(!second.SendMonsterKillChain(&recipient,1)&&calls.size()==size,
                "kill-chain sender respects a different scenario packet's open guard");
        };
        Check(first.SendWaveEvent(Payload{27,7,193,0})&&attempts==1&&SentAsExpected(64,193),
            "wave Begin, byte, short and End retain exact packet despite nested kill-chain request");
        onPacketStep={};calls.clear();
        Check(second.SendMonsterKillChain(&recipient,1)&&KillChainSentAsExpected(217,&recipient,1),
            "wave End permits a later kill-chain message");
    }
}
void KillChainScalarLifetime()
{
    for(char stage:{'m','b','l','e'})
    {
        InstallRecovery();auto owner=std::make_unique<CCSOLScenarioMessages>();CCSOLScenarioMessages surviving;
        Check(owner->Register()&&surviving.Register(),"kill-chain scalar lifetime owners registered");
        auto* storage=VirtualAlloc(nullptr,sizeof(edict_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(storage!=nullptr,"allocate recipient page for kill-chain late-read detection");
        if(!storage)continue;
        auto* recipient=std::construct_at(static_cast<edict_t*>(storage));
        auto* argumentRecipient=recipient;std::int32_t count=0x12345678;int attempts{};
        auto* sender=owner.get();DWORD oldProtection{};bool protectedRecipient{};
        calls.clear();
        onPacketStep=[&](char current)
        {
            if(current!=stage||attempts)return;
            ++attempts;owner.reset();count=-1;argumentRecipient=nullptr;
            protectedRecipient=VirtualProtect(storage,sizeof(edict_t),PAGE_NOACCESS,&oldProtection)!=FALSE;
            Check(protectedRecipient,"recipient becomes unreadable after the selected kill-chain callback");
            const auto size=calls.size();
            Check(!surviving.SendWaveEvent(Payload{27,7,193,0})&&calls.size()==size,
                "destroying kill-chain owner retains the global packet guard through End");
        };
        const bool sent=sender->SendMonsterKillChain(argumentRecipient,count);onPacketStep={};
        Check(sent&&!owner&&count==-1&&!argumentRecipient&&attempts==1&&
            KillChainSentAsExpected(217,recipient,0x12345678),
            "kill-chain finishes from scalar snapshots despite owner destruction, argument mutation and inaccessible recipient");
        DWORD ignored{};
        const bool restored=!protectedRecipient||VirtualProtect(storage,sizeof(edict_t),oldProtection,&ignored)!=FALSE;
        Check(restored,"restore guarded kill-chain recipient before destruction");
        if(restored)std::destroy_at(recipient);
        Check(VirtualFree(storage,0,MEM_RELEASE)!=FALSE,"release guarded kill-chain recipient storage");
        edict_t next{};calls.clear();
        Check(surviving.SendMonsterKillChain(&next,0)&&KillChainSentAsExpected(217,&next,0),
            "independent sender recovers after original owner destruction at each callback boundary");
    }
}
void KillChain()
{ KillChainPayloadsAndRejections();KillChainReentrancy();KillChainScalarLifetime(); }

bool ClassModDamageSent(int id, edict_t* recipient, int slot, std::int32_t amount)
{
    return calls.size()==5 && calls[0].kind=='m' && calls[0].a==MSG_ONE && calls[0].b==id &&
        !calls[0].origin && calls[0].recipient==recipient && calls[1].kind=='b' && calls[1].a==0xAA &&
        calls[2].kind=='b' && calls[2].a==slot && calls[3].kind=='l' && calls[3].a==amount && calls[4].kind=='e';
}
void ClassModDamagePayloads()
{
    InstallRecovery(); CCSOLScenarioMessages messages; edict_t recipient{};
    Check(!messages.SendClassModMonsterDamage(&recipient,1,0) && calls.empty(), "unregistered AA sender cannot open a packet");
    registrationResult=173; Check(messages.Register(), "AA sender registers distinct real channels");
    for(int slot:{1,MAX_CLIENTS}) for(std::int32_t amount:{0,1,-1,0x12345678,INT32_MIN,INT32_MAX})
    {
        calls.clear(); Check(messages.SendClassModMonsterDamage(&recipient,slot,amount) && ClassModDamageSent(173,&recipient,slot,amount),
            "AA is reliable MPToCL with exactly byte opcode, byte player slot and signed long, including zero and negatives");
    }
    for(int slot:{INT32_MIN,-1,0,MAX_CLIENTS+1,INT32_MAX})
    { calls.clear(); Check(!messages.SendClassModMonsterDamage(&recipient,slot,1) && calls.empty(), "invalid AA slot fails before Begin"); }
    calls.clear(); Check(!messages.SendClassModMonsterDamage(nullptr,1,1) && calls.empty(), "null AA recipient fails before Begin");
    recipient.free=TRUE;
    Check(!messages.SendClassModMonsterDamage(&recipient,1,1) && calls.empty(), "free AA recipient fails before Begin"); recipient.free=FALSE;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<4;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteLong=nullptr;
        calls.clear(); Check(!messages.SendClassModMonsterDamage(&recipient,1,1) && calls.empty(), "AA validates every required writer before Begin");
    }
    g_engfuncs=complete; g_engfuncs.pfnWriteShort=nullptr; g_engfuncs.pfnRegUserMsg=nullptr;
    g_engfuncs.pfnIndexOfEdict=nullptr; g_engfuncs.pfnEntOffsetOfPEntity=nullptr; calls.clear();
    Check(messages.SendClassModMonsterDamage(&recipient,1,-7) && ClassModDamageSent(173,&recipient,1,-7),
        "registered AA sender needs no short writer, entity lookup or client flag; authority supplies the already captured index");
}
void ClassModDamageReentrancy()
{
    InstallRecovery(); CCSOLScenarioMessages first, second; edict_t recipient{};
    Check(first.Register() && second.Register(), "independent AA owners register before packet reentry cases");
    for(int boundary=0;boundary<5;++boundary)
    {
        int step{}, attempts{}; calls.clear();
        onPacketStep=[&](char)
        {
            if(step++!=boundary)return;
            ++attempts; const auto prefix=calls.size();
            Check(!first.SendClassModMonsterDamage(&recipient,2,8) && !second.SendClassModMonsterDamage(&recipient,3,9) &&
                !second.SendWaveEvent(Payload{27,7,193,0}) && !second.SendMonsterKillChain(&recipient,10) && calls.size()==prefix,
                "AA packet rejects same-owner and independent AA/wave/kill-chain packets at Begin, both bytes, long and End");
        };
        Check(first.SendClassModMonsterDamage(&recipient,MAX_CLIENTS,INT32_MIN) && attempts==1 &&
            ClassModDamageSent(64,&recipient,MAX_CLIENTS,INT32_MIN), "nested requests cannot alter the exact six-byte AA payload");
        onPacketStep={}; calls.clear();
        Check(second.SendClassModMonsterDamage(&recipient,1,0) && ClassModDamageSent(64,&recipient,1,0), "AA guard releases after End");
    }
    int attempts{}; calls.clear();
    onBegin=[&]
    {
        ++attempts; const auto prefix=calls.size();
        Check(!second.SendClassModMonsterDamage(&recipient,1,4) && calls.size()==prefix, "AA respects another scenario packet's guard");
    };
    Check(first.SendWaveEvent(Payload{27,7,193,0}) && attempts==1 && SentAsExpected(64,193), "wave packet remains intact against nested AA");
    onBegin={}; calls.clear();
    Check(second.SendClassModMonsterDamage(&recipient,1,4) && ClassModDamageSent(64,&recipient,1,4), "wave End permits a subsequent AA packet");
}
void ClassModDamage()
{ ClassModDamagePayloads(); ClassModDamageReentrancy(); }
void WeaponDamageHint()
{
    Install(); CCSOLScenarioMessages first,second; edict_t recipient{};
    Check(!first.SendWeaponDamageBuffHint(&recipient) && calls.empty(), "unregistered4D rejects before Begin");
    Check(first.Register() && second.Register(), "independent hint owners registered");
    const auto packet=[&]{return calls.size()==3 && calls[0].kind=='m' && calls[0].a==MSG_ONE && calls[0].b==64 &&
        !calls[0].origin && calls[0].recipient==&recipient && calls[1].kind=='b' && calls[1].a==0x4D && calls[2].kind=='e';};
    calls.clear(); Check(!first.SendWeaponDamageBuffHint(nullptr) && calls.empty(), "null hint recipient rejected");
    recipient.free=TRUE; Check(!first.SendWeaponDamageBuffHint(&recipient) && calls.empty(), "free hint recipient rejected"); recipient.free=FALSE;
    for(char stage : {'m','b','e'})
    {
        int attempts{}; calls.clear();
        onPacketStep=[&](char current)
        {
            if(current!=stage || attempts)return;
            ++attempts; const auto prefix=calls.size();
            Check(!first.SendWeaponDamageBuffHint(&recipient) && !second.SendWeaponDamageBuffHint(&recipient) &&
                !second.SendWaveEvent(Payload{27,7,193,0}) && calls.size()==prefix, "hint blocks same/different owner and wave reentry");
        };
        Check(first.SendWeaponDamageBuffHint(&recipient) && attempts==1 && packet(), "MSG_ONE MPToCL contains single4D with no extra zero through all boundaries");
        onPacketStep={}; calls.clear(); Check(second.SendWeaponDamageBuffHint(&recipient) && packet(), "hint guard releases after End");
    }
    onBegin=[&]{const auto prefix=calls.size();Check(!second.SendWeaponDamageBuffHint(&recipient) && calls.size()==prefix, "hint respects open wave guard");};
    calls.clear(); Check(first.SendWaveEvent(Payload{27,7,193,0}) && SentAsExpected(64,193), "reverse wave packet remains intact"); onBegin={};
}

void RoundCompletionMessages()
{
    Install(); g_engfuncs.pfnWriteString=String;
    int scoreMessageId=121;
    CCSOLScenarioMessages first,second;
    const auto send=[&](CCSOLScenarioMessages& owner,int which,bool value)
    {
        if(which==0)return owner.SendRoundClearSound();
        if(which==1)return owner.SendRoundOutcome(value);
        if(which==2)return owner.SendRoundRewardState(value);
        return owner.SendNativeTeamScore(scoreMessageId,value,-32768);
    };
    const auto packet=[&](int which,bool value)
    {
        if(calls.empty()||calls.front().kind!='m'||calls.back().kind!='e'||calls.front().origin)return false;
        if(which==0)return calls.size()==3&&calls[0].a==MSG_BROADCAST&&calls[0].b==9&&
            !calls[0].recipient&&calls[1].kind=='t'&&calls[1].name=="spk \"RoundClear\"\n";
        if(which==1||which==2)return calls.size()==4&&calls[0].a==MSG_ALL&&calls[0].b==64&&
            !calls[0].recipient&&calls[1].kind=='b'&&calls[1].a==(which==1?0x19:0x24)&&
            calls[2].kind=='b'&&calls[2].a==(which==1?(value?8:9):(value?1:0));
        return calls.size()==4&&calls[0].a==MSG_ALL&&calls[0].b==121&&!calls[0].recipient&&
            calls[1].kind=='t'&&calls[1].name==(value?"CT":"TERRORIST")&&calls[2].kind=='s'&&calls[2].a==-32768;
    };
    calls.clear();Check(!first.SendRoundOutcome(true)&&!first.SendRoundRewardState(false)&&calls.empty(),
        "unregistered MP outcome and reward messages reject before Begin");
    for(int which:{0,3})
    {calls.clear();Check(send(first,which,true)&&packet(which,true),"engine/native round messages use their existing channels without MP registration");}
    Check(first.Register()&&second.Register(),"both round message owners register actual channels");
    const auto complete=g_engfuncs;
    for(int which=0;which<4;++which)for(bool value:{false,true})
    {
        calls.clear();Check(send(first,which,value)&&packet(which,value),
            "actual round message uses original destination, channel, payload order and widths");
        const int steps=static_cast<int>(calls.size());
        for(int boundary=0;boundary<steps;++boundary)
        {
            int step{},attempts{};calls.clear();
            onPacketStep=[&](char)
            {
                if(step++!=boundary)return;
                ++attempts;const auto prefix=calls.size();
                for(int nested=0;nested<4;++nested)
                    Check(!send(second,nested,!value)&&calls.size()==prefix,"all round channels reject nested packets across independent owners");
            };
            Check(send(first,which,value)&&attempts==1&&packet(which,value),"round packet remains exact at every actual write boundary");
            onPacketStep={};calls.clear();
            Check(send(second,which,value)&&packet(which,value),"round packet guard releases after successful completion");
        }
        for(int missing=0;missing<5;++missing)
        {
            const bool required=missing<2||(missing==2&&(which==1||which==2))||
                (missing==3&&which==3)||(missing==4&&(which==0||which==3));
            if(!required)continue;
            g_engfuncs=complete;
            if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
            if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
            if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
            if(missing==3)g_engfuncs.pfnWriteShort=nullptr;
            if(missing==4)g_engfuncs.pfnWriteString=nullptr;
            calls.clear();Check(!send(first,which,value)&&calls.empty(),"round sender validates every required engine edge before a partial packet");
        }
        g_engfuncs=complete;
        for(int boundary=0;boundary<steps;++boundary)
        {
            int step{};bool threw{};calls.clear();
            onPacketStep=[&](char){if(step++==boundary)throw std::runtime_error("round engine edge");};
            try{send(first,which,value);}catch(const std::runtime_error&){threw=true;}
            onPacketStep={};calls.clear();
            Check(threw&&send(second,which,value)&&packet(which,value),"round guard releases even after an engine callback exception");
        }
    }
    for(int invalid:{0,63,256,INT32_MAX})
    {
        scoreMessageId=invalid;calls.clear();
        Check(!first.SendNativeTeamScore(scoreMessageId,true,1)&&calls.empty(),
            "native score rejects absent or out-of-byte-range registered channel IDs");
    }
    scoreMessageId=121;
    for(const std::int16_t score:{std::int16_t(-32768),std::int16_t(-1),std::int16_t(0),std::int16_t(32767)})
    {
        calls.clear();Check(first.SendNativeTeamScore(scoreMessageId,true,score)&&calls.size()==4&&calls[2].kind=='s'&&calls[2].a==score,
            "native score keeps the exact signed16 boundary without further narrowing");
    }
    for(int boundary=0;boundary<4;++boundary)
    {
        scoreMessageId=121;bool ct=true;std::int16_t score=-32768;int step{};calls.clear();
        onPacketStep=[&](char){if(step++==boundary){scoreMessageId=255;ct=false;score=0;}};
        Check(first.SendNativeTeamScore(scoreMessageId,ct,score)&&packet(3,true),
            "native score finishes from its copied channel, team and signed value after caller inputs change");
        onPacketStep={};
    }
}

using FinalValues=std::array<std::int32_t,5>;
constexpr FinalValues FinalFields{INT32_MIN,-17,0,0x12345678,INT32_MAX};
void InstallFinalResult()
{
    InstallRecovery();g_engfuncs.pfnWriteString=String;
    g_engfuncs.pfnRegUserMsg=[](const char* name,int size)
    {
        if(registrationFailure==name)
        { calls.push_back({'r',size,0,nullptr,nullptr,name});return registrationFailureResult; }
        return RecoveryRegister(name,size);
    };
}
bool FinalResultSent(int id,edict_t* recipient,std::string_view caption,const FinalValues& fields)
{
    if(calls.size()!=9||calls[0].kind!='m'||calls[0].a!=MSG_ONE||calls[0].b!=id||
        calls[0].origin||calls[0].recipient!=recipient||calls[1].kind!='b'||calls[1].a!=0x18||
        calls[2].kind!='t'||calls[2].name!=caption||calls[8].kind!='e')return false;
    for(std::size_t i=0;i<fields.size();++i)
        if(calls[i+3].kind!='l'||calls[i+3].a!=fields[i])return false;
    return true;
}
void FinalResultPayloads()
{
    InstallFinalResult();CCSOLScenarioMessages messages;edict_t recipient{};
    Check(!messages.SendFinalResult(&recipient,"result",FinalFields)&&calls.empty(),"unregistered final result cannot open a packet");
    Check(messages.Register(),"final result registers actual independently numbered channels");
    for(const auto& caption:std::vector<std::string>{"","result",std::string(169,'a'),std::string(170,'b'),std::string("\xE5\xAE\x8C\xE6\x88\x90")})
    {
        calls.clear();Check(messages.SendFinalResult(&recipient,caption,FinalFields)&&FinalResultSent(217,&recipient,caption,FinalFields),
            "final result is one reliable ZBSVer2 byte0x18/string/five signed LONG packet in exact original order");
        Check(calls.size()==9&&1+calls[2].name.size()+1+5*4==caption.size()+22&&caption.size()+22<=192,
            "result string includes its terminating NUL within the stock192-byte payload budget");
    }
    for(int slot=0;slot<5;++slot)for(std::int32_t value:{INT32_MIN,-1,0,1,INT32_MAX})
    {
        FinalValues fields{11,22,33,44,55};fields[slot]=value;calls.clear();
        Check(messages.SendFinalResult(&recipient,"boundaries",fields)&&FinalResultSent(217,&recipient,"boundaries",fields),
            "all five result fields retain signed32 boundaries without truncation, clamping or field swaps");
    }
    for(const auto& caption:std::vector<std::string>{std::string(171,'x'),std::string(1024,'x'),std::string("a\0b",3),std::string(1,'\0')})
    {
        calls.clear();Check(!messages.SendFinalResult(&recipient,caption,FinalFields)&&calls.empty(),
            "171-byte text and embeddedNUL reject before any partial result packet");
    }
    calls.clear();Check(!messages.SendFinalResult(nullptr,"",FinalFields)&&calls.empty(),"null final-result recipient rejects before Begin");
    recipient.free=TRUE;Check(!messages.SendFinalResult(&recipient,"",FinalFields)&&calls.empty(),"freed final-result recipient rejects before Begin");
    recipient.free=FALSE;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<5;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteString=nullptr;
        if(missing==4)g_engfuncs.pfnWriteLong=nullptr;
        calls.clear();Check(!messages.SendFinalResult(&recipient,"",FinalFields)&&calls.empty(),
            "every required final-result engine edge is checked before Begin");
    }
    g_engfuncs=complete;g_engfuncs.pfnRegUserMsg=nullptr;g_engfuncs.pfnWriteShort=nullptr;
    g_engfuncs.pfnEntOffsetOfPEntity=nullptr;g_engfuncs.pfnIndexOfEdict=nullptr;calls.clear();
    Check(messages.SendFinalResult(&recipient,"",FinalFields)&&FinalResultSent(217,&recipient,"",FinalFields),
        "registered final sender needs no entity lookup, short writer, registrar or client flag");
    for(const char* failed:{"MPToCL","ZBSVer2","MPToCL2"})for(int invalid:{0,63,256})
    {
        InstallFinalResult();Check(messages.Register(),"prior valid final-result registration exists before failed refresh");
        registrationFailure=failed;registrationFailureResult=invalid;
        Check(!messages.Register(),"failed actual channel registration cancels complete result registration");
        calls.clear();Check(!messages.SendFinalResult(&recipient,"",FinalFields)&&calls.empty(),
            "failed registration cannot reuse the prior final-result channel");
        registrationFailure.clear();Check(messages.Register(),"all channels can recover after failed final registration");
        calls.clear();Check(messages.SendFinalResult(&recipient,"ok",FinalFields)&&FinalResultSent(217,&recipient,"ok",FinalFields),
            "new final message uses the recovered actual ZBSVer2 channel");
    }
}
void FinalResultReentryAndExceptions()
{
    InstallFinalResult();CCSOLScenarioMessages first,second;edict_t recipient{};
    Check(first.Register()&&second.Register(),"two final-result owners use real channel registration");
    for(int boundary=0;boundary<9;++boundary)
    {
        int step{},attempts{};calls.clear();
        onPacketStep=[&](char)
        {
            if(step++!=boundary)return;
            ++attempts;const auto count=calls.size();
            Check(!first.SendFinalResult(&recipient,"nested",FinalFields)&&!second.SendFinalResult(&recipient,"nested",FinalFields)&&
                !second.SendWaveEvent(Payload{27,7,193,0})&&!second.SendRoundClearSound()&&!second.SendAllRoundsClear(219)&&
                !second.SendMonsterKillChain(&recipient,9)&&calls.size()==count,
                "final-result Begin/byte/string/eachLONG/End rejects same and independent owner interleaving");
        };
        Check(first.SendFinalResult(&recipient,"original",FinalFields)&&attempts==1&&FinalResultSent(217,&recipient,"original",FinalFields),
            "all nine callback boundaries retain one complete original final-result packet");
        onPacketStep={};calls.clear();
        Check(second.SendFinalResult(&recipient,"next",FinalFields)&&FinalResultSent(217,&recipient,"next",FinalFields),
            "independent final-result sender can write after each completed packet");
        step=0;bool threw{};calls.clear();
        onPacketStep=[&](char){if(step++==boundary)throw std::runtime_error("result engine edge");};
        try{first.SendFinalResult(&recipient,"throws",FinalFields);}catch(const std::runtime_error&){threw=true;}
        onPacketStep={};calls.clear();
        Check(threw&&second.SendFinalResult(&recipient,"recovered",FinalFields)&&FinalResultSent(217,&recipient,"recovered",FinalFields),
            "result guard releases after an exception at every actual write boundary without claiming packet rollback");
    }
    int attempts{};calls.clear();
    onBegin=[&]{++attempts;const auto count=calls.size();Check(!second.SendFinalResult(&recipient,"nested",FinalFields)&&calls.size()==count,
        "final result also respects an existing non-result scenario packet");};
    Check(first.SendWaveEvent(Payload{27,7,193,0})&&attempts==1&&SentAsExpected(64,193),"reverse wave-to-final nesting cannot corrupt the original wave");
    onBegin={};
}
void FinalResultInputLifetime()
{
    struct Input { std::array<char,171> caption; FinalValues fields; };
    for(bool retire:{false,true})for(int boundary=0;boundary<9;++boundary)
    {
        InstallFinalResult();auto owner=std::make_unique<CCSOLScenarioMessages>();CCSOLScenarioMessages survivor;
        Check(owner->Register()&&survivor.Register(),"final-result lifetime owners registered");
        auto* storage=VirtualAlloc(nullptr,sizeof(Input),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        auto* recipientStorage=VirtualAlloc(nullptr,sizeof(edict_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(storage&&recipientStorage,"allocate independent input/recipient pages for final-result stale-read detection");
        if(!storage||!recipientStorage)
        {if(storage)VirtualFree(storage,0,MEM_RELEASE);if(recipientStorage)VirtualFree(recipientStorage,0,MEM_RELEASE);continue;}
        auto* input=std::construct_at(static_cast<Input*>(storage));input->caption.fill('x');input->fields=FinalFields;
        auto* recipient=std::construct_at(static_cast<edict_t*>(recipientStorage));auto* argumentRecipient=recipient;
        const std::string original(170,'x');std::string_view caption(input->caption.data(),170);
        std::span<const std::int32_t,5> fields(input->fields);auto* sender=owner.get();
        int step{},attempts{};DWORD oldInput{},oldRecipient{};bool inputProtected{},recipientProtected{};calls.clear();
        onPacketStep=[&](char)
        {
            if(step++!=boundary)return;
            ++attempts;input->caption.fill('y');input->fields.fill(7);caption="changed";argumentRecipient=nullptr;
            if(retire)
            {
                owner.reset();
                inputProtected=VirtualProtect(storage,sizeof(Input),PAGE_NOACCESS,&oldInput)!=FALSE;
                recipientProtected=VirtualProtect(recipientStorage,sizeof(edict_t),PAGE_NOACCESS,&oldRecipient)!=FALSE;
                Check(inputProtected&&recipientProtected,"caller result storage and recipient become unreadable after owner destruction");
            }
            const auto count=calls.size();
            Check(!survivor.SendFinalResult(recipient,"nested",FinalFields)&&calls.size()==count,
                "lifetime callback cannot open another result even when original owner has been released");
        };
        const bool sent=sender->SendFinalResult(argumentRecipient,caption,fields);onPacketStep={};
        Check(sent&&attempts==1&&!argumentRecipient&&caption=="changed"&&(!retire||!owner)&&
            FinalResultSent(217,recipient,original,FinalFields),
            "result finishes from bounded text/value copies after every callback mutates or retires its caller and owner");
        DWORD ignored{};
        const bool restoredInput=!inputProtected||VirtualProtect(storage,sizeof(Input),oldInput,&ignored)!=FALSE;
        const bool restoredRecipient=!recipientProtected||VirtualProtect(recipientStorage,sizeof(edict_t),oldRecipient,&ignored)!=FALSE;
        Check(restoredInput&&restoredRecipient,"restore result input/recipient pages before destroying test objects");
        if(restoredInput)std::destroy_at(input);if(restoredRecipient)std::destroy_at(recipient);
        const bool releasedInput=VirtualFree(storage,0,MEM_RELEASE)!=FALSE;
        const bool releasedRecipient=VirtualFree(recipientStorage,0,MEM_RELEASE)!=FALSE;
        Check(releasedInput&&releasedRecipient,"release both guarded result allocations");
        edict_t next{};calls.clear();
        Check(survivor.SendFinalResult(&next,"after",FinalFields)&&FinalResultSent(217,&next,"after",FinalFields),
            "surviving sender recovers once the original result stack and global packet guard finish");
    }
}
bool AllRoundsClearSent(int id)
{
    return calls.size()==4&&calls[0].kind=='m'&&calls[0].a==MSG_ALL&&calls[0].b==id&&!calls[0].origin&&!calls[0].recipient&&
        calls[1].kind=='b'&&calls[1].a==4&&calls[2].kind=='t'&&calls[2].name=="恭喜！！您已经成功通关！"&&calls[3].kind=='e';
}
void AllRoundsClearMessages()
{
    InstallFinalResult();CCSOLScenarioMessages first,second;edict_t recipient{};
    for(int id:{64,219,255})
    {calls.clear();Check(first.SendAllRoundsClear(id)&&AllRoundsClearSent(id),"native final-clear is MSG_ALL/TextMsg/center byte4/Chinese STRING with no CSO-only prefix");}
    for(int invalid:{INT32_MIN,-1,0,63,256,INT32_MAX})
    {calls.clear();Check(!first.SendAllRoundsClear(invalid)&&calls.empty(),"native final-clear rejects invalid TextMsg IDs before Begin");}
    const auto complete=g_engfuncs;
    for(int missing=0;missing<4;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteString=nullptr;
        calls.clear();Check(!first.SendAllRoundsClear(219)&&calls.empty(),"native final-clear validates each required engine edge");
    }
    g_engfuncs=complete;Check(first.Register()&&second.Register(),"independent final-clear and result owners registered");
    for(int boundary=0;boundary<4;++boundary)
    {
        int step{},attempts{},id=219;calls.clear();
        onPacketStep=[&](char)
        {
            if(step++!=boundary)return;
            ++attempts;id=255;const auto count=calls.size();
            Check(!first.SendAllRoundsClear(219)&&!second.SendAllRoundsClear(219)&&
                !second.SendFinalResult(&recipient,"nested",FinalFields)&&!second.SendRoundOutcome(true)&&calls.size()==count,
                "final-clear blocks same-owner, other-owner and different-channel nested packets at every boundary");
        };
        Check(first.SendAllRoundsClear(id)&&attempts==1&&AllRoundsClearSent(219),"final-clear captures its TextMsg ID before engine callback mutation");
        onPacketStep={};calls.clear();
        Check(second.SendAllRoundsClear(219)&&AllRoundsClearSent(219),"final-clear guard releases after complete packet");
        step=0;bool threw{};calls.clear();
        onPacketStep=[&](char){if(step++==boundary)throw std::runtime_error("final-clear edge");};
        try{first.SendAllRoundsClear(219);}catch(const std::runtime_error&){threw=true;}
        onPacketStep={};calls.clear();
        Check(threw&&second.SendAllRoundsClear(219)&&AllRoundsClearSent(219),"final-clear exception releases the shared message guard");
    }
}
void FinalResultMessages()
{ FinalResultPayloads();FinalResultReentryAndExceptions();FinalResultInputLifetime();AllRoundsClearMessages(); }
}
int main(int argc,char** argv)
{
    if(argc==2&&std::string_view(argv[1])=="--announce")FragsAndRibbons();
    else if(argc==2&&std::string_view(argv[1])=="--recovery")Recovery();
    else if(argc==2&&std::string_view(argv[1])=="--kill-chain")KillChain();
    else if(argc==2&&std::string_view(argv[1])=="--classmod-damage")ClassModDamage();
    else if(argc==2&&std::string_view(argv[1])=="--weapon-damage-hint")WeaponDamageHint();
    else if(argc==2&&std::string_view(argv[1])=="--round-completion")RoundCompletionMessages();
    else if(argc==2&&std::string_view(argv[1])=="--final-result")FinalResultMessages();
    else if(argc==1){Registration();PayloadsAndMissingFunctions();ProductionCombination();}
    else {std::cerr<<"usage: [--announce|--recovery|--kill-chain|--classmod-damage|--weapon-damage-hint|--round-completion|--final-result]\n";return 2;}
    std::cout<<"scenario_messages checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
