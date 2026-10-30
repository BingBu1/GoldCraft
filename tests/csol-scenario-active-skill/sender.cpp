// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the exact production sender and packet guard with engine write edges.
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <vector>
#include <string>
#include "../../native/csol/scenario_abilities.hpp"

struct edict_s { bool free{}; };
using edict_t = edict_s;
class CCSOLScenarioMessages
{
public:
    int m_upgradeMessageId{177};
    int m_messageId{},m_statusMessageId{},m_skillSnapshotId{178};
    bool Register();
    bool SendUpgradeExperience(edict_t*,int) const;
    bool SendActiveSkillSnapshot(edict_t*,const csol::abilities::Snapshot&) const;
#include "actual_sender_declaration.inc"
};
namespace
{
constexpr int MSG_ONE = 1;
struct Engine
{
    void (*pfnMessageBegin)(int, int, const float*, edict_t*);
    void (*pfnMessageEnd)();
    void (*pfnWriteByte)(int);
    void (*pfnWriteShort)(int);
    void (*pfnWriteLong)(int);
    int (*pfnRegUserMsg)(const char*,int);
} g_engfuncs{};
struct Packet
{
    int begins{}, ends{}, destination{}, id{};
    const float* origin{};
    edict_t* recipient{};
    std::vector<std::uint8_t> bytes;
    std::function<void()> onBegin;
    std::vector<std::pair<std::string,int>> registrations;
    int failedRegistration{-1};
} packet;
void Begin(int destination, int id, const float* origin, edict_t* recipient)
{
    ++packet.begins; packet.destination=destination; packet.id=id;
    packet.origin=origin; packet.recipient=recipient;
    if (packet.onBegin) packet.onBegin();
}
void End() { ++packet.ends; }
void Byte(int value) { packet.bytes.push_back(static_cast<std::uint8_t>(value)); }
void Short(int value) { Byte(value); Byte(static_cast<unsigned>(value) >> 8); }
void Long(int value)
{
    for (int shift=0; shift<32; shift+=8) Byte(static_cast<unsigned>(value) >> shift);
}
int Register(const char* name,int size)
{
    const auto index=int(packet.registrations.size());
    packet.registrations.emplace_back(name,size);
    return index==packet.failedRegistration?0:175+index;
}
void Reset()
{
    packet = {};
    g_engfuncs = {Begin, End, Byte, Short, Long, Register};
}
int senderChecks{};
void Verify(bool value, const char* text)
{
    ++senderChecks;
    if (!value) { std::cerr << "FAIL sender " << senderChecks << ": " << text << '\n'; std::exit(1); }
}
}
#define MESSAGE_BEGIN(destination, id, origin, recipient) g_engfuncs.pfnMessageBegin(destination, id, origin, recipient)
#define MESSAGE_END() g_engfuncs.pfnMessageEnd()
#define WRITE_BYTE(value) g_engfuncs.pfnWriteByte(value)
#define WRITE_SHORT(value) g_engfuncs.pfnWriteShort(value)
#define WRITE_LONG(value) g_engfuncs.pfnWriteLong(value)
#define REG_USER_MSG(name,size) g_engfuncs.pfnRegUserMsg(name,size)
#include "actual_message_guard.inc"
#include "actual_skill_sender.inc"
#include "actual_hud_senders.inc"

int RunSenderTests()
{
    {
        Reset();CCSOLScenarioMessages sender;
        Verify(sender.Register(),"actual registration accepts engine allocated IDs");
        Verify(packet.registrations==std::vector<std::pair<std::string,int>>{{"MPToCL",-1},{"ZBSVer2",-1},{"MPToCL2",-1},{"CsoSkState",15}},
            "snapshot has dedicated fixed 15-byte registration");
        Verify(sender.m_skillSnapshotId==178 && sender.m_upgradeMessageId==176,"registered IDs retained independently");
    }
    for(int failure=0;failure<4;++failure)
    {
        Reset();CCSOLScenarioMessages sender;packet.failedRegistration=failure;
        Verify(!sender.Register(),"invalid engine registration rejected");
        Verify(sender.m_skillSnapshotId==0 && sender.m_upgradeMessageId==0,"failed registration cannot leave a sendable stale ID");
    }
    for(unsigned type : {0u,1u,29u,55u})
    {
        Reset();CCSOLScenarioMessages sender;edict_t receiver;
        const csol::abilities::Snapshot snapshot{type,type?50.f:0.f,type?20.f:0.f,0};
        Verify(sender.SendActiveSkillSnapshot(&receiver,snapshot),"valid snapshot sent");
        std::vector<std::uint8_t> expected{1,std::uint8_t(type),0};
        for(float value : {snapshot.cooldown,snapshot.elapsed,snapshot.duration})
            for(int shift=0;shift<32;shift+=8)expected.push_back(std::uint8_t(std::bit_cast<std::uint32_t>(value)>>shift));
        Verify(packet.bytes==expected && packet.bytes.size()==15 && packet.id==178 && packet.recipient==&receiver &&
            packet.begins==1 && packet.ends==1,"snapshot carries exact elapsed/full float32 words to original recipient");
    }
    for(int failure=0;failure<17;++failure)
    {
        Reset();CCSOLScenarioMessages sender;edict_t receiver;edict_t* target=&receiver;
        csol::abilities::Snapshot snapshot{29,50,20,0};
        if(failure==0)sender.m_skillSnapshotId=0;
        if(failure==1)target=nullptr;
        if(failure==2)receiver.free=true;
        if(failure==3)snapshot.type=56;
        if(failure==4)snapshot.cooldown=std::numeric_limits<float>::quiet_NaN();
        if(failure==5)snapshot.elapsed=std::numeric_limits<float>::infinity();
        if(failure==6)snapshot.duration=std::numeric_limits<float>::quiet_NaN();
        if(failure==7)snapshot.cooldown=-1;
        if(failure==8)snapshot.elapsed=-1;
        if(failure==9)snapshot.elapsed=51;
        if(failure==10)snapshot.duration=-1;
        if(failure==11)snapshot.type=0;
        if(failure==12)g_engfuncs.pfnMessageBegin=nullptr;
        if(failure==13)g_engfuncs.pfnMessageEnd=nullptr;
        if(failure==14)g_engfuncs.pfnWriteByte=nullptr;
        if(failure==15)g_engfuncs.pfnWriteShort=nullptr;
        if(failure==16)g_engfuncs.pfnWriteLong=nullptr;
        Verify(!sender.SendActiveSkillSnapshot(target,snapshot),"invalid snapshot/engine boundary rejected");
        Verify(packet.begins==0 && packet.bytes.empty(),"invalid snapshot cannot partially open packet");
    }
    {
        Reset();CCSOLScenarioMessages sender;edict_t receiver;
        csol::abilities::Snapshot snapshot{29,50,20,0};
        packet.onBegin=[&]{snapshot={};Verify(!sender.SendActiveSkillSnapshot(&receiver,snapshot),"nested snapshot rejected");};
        Verify(sender.SendActiveSkillSnapshot(&receiver,snapshot),"outer snapshot survives input mutation during callback");
        Verify(packet.bytes[1]==29 && packet.bytes[9]==160 && packet.bytes[10]==65 && !g_scenarioMessageOpen,
            "snapshot values copied before callback and packet guard released");
    }
    for(int percent : {0,33,100,-1,INT32_MAX})
    {
        Reset();CCSOLScenarioMessages sender;edict_t receiver;
        Verify(sender.SendUpgradeExperience(&receiver,percent),"original signed XP payload accepted");
        std::vector<std::uint8_t> expected{3};
        for(int shift=0;shift<32;shift+=8)expected.push_back(std::uint8_t(unsigned(percent)>>shift));
        Verify(packet.bytes==expected && packet.id==177,"XP is opcode3 followed by exact signed LONG");
    }
    for (int type : {0, 1, 29, 32767})
    {
        Reset(); CCSOLScenarioMessages sender; edict_t receiver;
        const float cooldown = type ? 50.25f : 0.f;
        const float duration = type ? -1.5f : 0.f;
        Verify(sender.SendActiveSkillCooldown(&receiver,type,cooldown,duration), "accepted subtype and finite raw float values");
        Verify(packet.begins==1 && packet.ends==1 && packet.destination==MSG_ONE && packet.id==177 &&
            packet.origin==nullptr && packet.recipient==&receiver, "one reliable packet goes to exact recipient");
        std::vector<std::uint8_t> expected{0x24,static_cast<std::uint8_t>(type),static_cast<std::uint8_t>(type>>8)};
        for (float value : {cooldown,duration})
        {
            auto bits=std::bit_cast<std::uint32_t>(value);
            for(int shift=0;shift<32;shift+=8) expected.push_back(static_cast<std::uint8_t>(bits>>shift));
        }
        Verify(packet.bytes==expected && packet.bytes.size()==11, "byte/short/raw float32 payload is exact and has no trailing bytes");
        Verify(!g_scenarioMessageOpen, "production RAII guard closes after packet");
    }
    for (int failure=0;failure<14;++failure)
    {
        Reset(); CCSOLScenarioMessages sender; edict_t receiver; edict_t* target=&receiver;
        int type=29; float cooldown=50, duration=0;
        if(failure==0) sender.m_upgradeMessageId=0;
        if(failure==1) target=nullptr;
        if(failure==2) receiver.free=true;
        if(failure==3) type=-1;
        if(failure==4) type=32768;
        if(failure==5) cooldown=std::numeric_limits<float>::quiet_NaN();
        if(failure==6) cooldown=std::numeric_limits<float>::infinity();
        if(failure==7) duration=std::numeric_limits<float>::quiet_NaN();
        if(failure==8) duration=-std::numeric_limits<float>::infinity();
        if(failure==9) g_engfuncs.pfnMessageBegin=nullptr;
        if(failure==10) g_engfuncs.pfnMessageEnd=nullptr;
        if(failure==11) g_engfuncs.pfnWriteByte=nullptr;
        if(failure==12) g_engfuncs.pfnWriteShort=nullptr;
        if(failure==13) g_engfuncs.pfnWriteLong=nullptr;
        Verify(!sender.SendActiveSkillCooldown(target,type,cooldown,duration), "invalid recipient/timer/subtype/engine edge fails before opening");
        Verify(packet.begins==0 && packet.ends==0 && packet.bytes.empty(), "invalid state produces no partial packet");
    }
    {
        Reset(); CCSOLScenarioMessages sender; edict_t receiver;
        packet.onBegin=[&] {
            Verify(g_scenarioMessageOpen, "guard is held before first engine callback");
            Verify(!sender.SendActiveSkillCooldown(&receiver,0,0,0), "reentrant send cannot open another packet");
        };
        Verify(sender.SendActiveSkillCooldown(&receiver,29,50,0), "outer sender survives rejected reentrant send");
        Verify(packet.begins==1 && packet.ends==1 && packet.bytes.size()==11 && !g_scenarioMessageOpen,
            "reentrancy leaves exactly one complete packet and releases guard");
    }
    return senderChecks;
}
