// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace upgrade_test
{
void MessageCases()
{
    Fixture f;f.Configure();
    Check(f.engine.registrations==std::vector<std::string>{"MPToCL","ZBSVer2"},"registration binds separate original message names");
    Require(f.messages.SendUpgradeTotal(f.player.edict(),0x01020304,false),"full-width total packet");
    Require(f.messages.SendUpgradeTotal(f.player.edict(),-257,true),"signed upgraded total packet");
    Require(f.messages.SendUpgradeStat(f.player.edict(),4,-257,0x01020304),"full-width ammo stat packet");
    const auto& packets=f.engine.messages;
    Check(packets.size()==3&&packets[0].writes=="BLE"&&packets[1].writes=="BLE"&&packets[2].writes=="BLLE","payload fields use BYTE then actual LONG widths");
    Check(packets[0].wire==std::vector<std::uint8_t>{4,4,3,2,1}&&
        packets[1].wire==std::vector<std::uint8_t>{5,255,254,255,255}&&
        packets[2].wire==std::vector<std::uint8_t>{14,255,254,255,255,4,3,2,1},"exact little-endian signed payloads and original opcodes");
    for(const auto& packet:packets)Check(packet.destination==MSG_ONE&&packet.type==97&&packet.recipient==1,"ZBSVer2 targets only its player with assigned ID");
    f.engine.messages.clear();Require(f.state->Publish(),"publish player snapshot");
    Check(f.engine.messages.size()==6,"one total plus five stat packets");
    for(unsigned stat=0;stat<5;++stat)Check(f.engine.messages[stat+1].wire==std::vector<std::uint8_t>{std::uint8_t(6+stat*2),1,0,0,0,21,0,0,0},"stat opcodes6 8 10 12 14 retain table order");
    f.engine.messages.clear();bool attempted=false;
    f.engine.boundary=[&](std::string_view label){if(label=="message-begin"&&!attempted){attempted=true;
        Check(!f.state->Configure(45,0,false),"Publish busy guard rejects callback Configure");
        Check(!f.state->Publish()&&!f.messages.SendUpgradeTotal(f.player.edict(),99,false),"reentrant publish and nested packet rejected");}};
    Require(f.state->Publish(),"outer publish completes coherent original snapshot");
    Check(f.engine.messages.size()==6&&f.state->State()->attributes[0].cap==21,"callback cannot mix old total with new character stats");
    f.engine.boundary={};f.engine.messages.clear();
    auto saved=g_engfuncs.pfnWriteLong;g_engfuncs.pfnWriteLong=nullptr;
    Check(!f.messages.SendUpgradeStat(f.player.edict(),0,1,21)&&f.engine.messages.empty()&&!f.engine.open,"missing LONG service rejects before packet begins");
    g_engfuncs.pfnWriteLong=saved;
    Check(!f.messages.SendUpgradeStat(f.player.edict(),5,1,21)&&f.engine.messages.empty(),"invalid stat never opens message");

    // The skill12 packet is MPToCL, independent of the upgrade-message ID.
    const std::vector<std::uint8_t> contact{0x51,0x0a,0,0,0,0x80,0x3f,0xff,0xff,0xff};
    const auto contactPacket=[&]
    {
        return f.engine.messages.size()==1&&f.engine.messages[0].destination==MSG_ONE&&
            f.engine.messages[0].type==96&&f.engine.messages[0].recipient==1&&
            f.engine.messages[0].writes=="BSLBBBE"&&f.engine.messages[0].wire==contact&&!f.engine.open;
    };
    CCSOLScenarioMessages unregistered;
    Check(!unregistered.SendMonsterContactEffect(f.player.edict())&&f.engine.messages.empty(),
        "unregistered contact effect cannot send an engine opcode");
    Require(f.messages.SendMonsterContactEffect(f.player.edict()),"contact effect uses registered message sender");
    Check(contactPacket(),"original contact body is exactly10 bytes with float1 bits and white RGB");
    f.engine.messages.clear();
    Check(!f.messages.SendMonsterContactEffect(nullptr)&&f.engine.messages.empty(),"null contact recipient opens no packet");
    f.player.edict()->free=TRUE;
    Check(!f.messages.SendMonsterContactEffect(f.player.edict())&&f.engine.messages.empty(),"free contact recipient opens no packet");
    f.player.edict()->free=FALSE;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<5;++missing)
    {
        g_engfuncs=complete;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==3)g_engfuncs.pfnWriteShort=nullptr;
        if(missing==4)g_engfuncs.pfnWriteLong=nullptr;
        f.engine.messages.clear();
        Check(!f.messages.SendMonsterContactEffect(f.player.edict())&&f.engine.messages.empty()&&!f.engine.open,
            "each absent contact writer rejects before opening a partial packet");
        g_engfuncs=complete;
        Check(f.messages.SendMonsterContactEffect(f.player.edict())&&contactPacket(),
            "restoring required contact writer permits the next complete packet");
    }
    CCSOLScenarioMessages another;Require(another.Register(),"separate sender registered for nested-packet checks");
    for(const std::string_view edge:{"message-begin","message-write","message-end"})
    {
        f.engine.messages.clear();bool nested=false;
        f.engine.boundary=[&](std::string_view label)
        {
            if(label!=edge||nested)return;
            nested=true;
            Check(!f.messages.SendMonsterContactEffect(f.player.edict())&&
                !another.SendMonsterContactEffect(f.player.edict())&&
                !another.SendUpgradeTotal(f.player.edict(),99,false),
                "open contact packet rejects same-object cross-object and cross-opcode nesting");
        };
        Check(f.messages.SendMonsterContactEffect(f.player.edict())&&nested&&contactPacket(),
            "contact packet completes unchanged after rejected callback reentry");
        f.engine.boundary={};
    }
}
}
