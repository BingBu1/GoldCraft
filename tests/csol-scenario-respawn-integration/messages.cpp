// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK callback ABI, unchanged extracted production writers and actual
// client decoders. Engine writes are captured; no real network/game is used.
#include "precompiled.h"
#include "../../native/csol/server/scenario_messages.h"
#include "../../native/csol/scenario_abilities.hpp"
#include "../../native/csol/scenario_monster_effects.hpp"
#include "../../native/csol/playerclip.hpp"
#include "../../native/csol/frozen_death.hpp"
#include "../../native/csol/frozen_smoke.hpp"
#include "../../native/csol/client/scenario_respawn_ui.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <climits>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
namespace hud=csol::scenario_respawn_ui;
struct Packet {int destination{},id{};edict_t* recipient{};std::vector<std::byte> bytes;std::string writes;};
std::vector<Packet> packets;Packet current;bool opened{};
std::vector<std::pair<std::string,int>> registrations;std::map<std::string,int> ids;
std::string failRegistration;int badId{};unsigned checks{};
std::function<void(char)> reentry;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
int RegisterMessage(const char* name,int size){registrations.emplace_back(name,size);if(failRegistration==name)return badId;return ids.emplace(name,64+int(ids.size())).first->second;}
void Step(char type){const auto callback=reentry;if(callback)callback(type);}
void Begin(int destination,int id,const float*,edict_t* entity){Check(!opened,"no nested MESSAGE_BEGIN");opened=true;current={destination,id,entity,{},{}};Step('m');}
void Bytes(std::uint32_t value,unsigned count,char type){Check(opened,"writer is inside a packet");current.writes+=type;for(unsigned i=0;i<count;++i)current.bytes.push_back(std::byte((value>>(i*8))&255));Step(type);}
void Byte(int value){Bytes(std::uint32_t(value),1,'b');}void Short(int value){Bytes(std::uint32_t(value),2,'s');}void Long(int value){Bytes(std::uint32_t(value),4,'l');}
void String(const char* text){Check(opened&&text,"non-null string in packet");current.writes+='t';do{current.bytes.push_back(std::byte(*text));}while(*text++);Step('t');}
void End(){Check(opened,"MESSAGE_END matches begin");opened=false;packets.push_back(current);Step('e');}
void Install(){g_engfuncs={};g_engfuncs.pfnRegUserMsg=RegisterMessage;g_engfuncs.pfnMessageBegin=Begin;g_engfuncs.pfnMessageEnd=End;
    g_engfuncs.pfnWriteByte=Byte;g_engfuncs.pfnWriteShort=Short;g_engfuncs.pfnWriteLong=Long;g_engfuncs.pfnWriteString=String;
    opened=false;packets.clear();registrations.clear();ids.clear();reentry={};failRegistration.clear();badId=0;}
#include "actual_messages.inc"

void NoSends(CCSOLScenarioMessages& messages,edict_t* target){
    const auto count=packets.size();
    Check(!messages.SendConsumableState(target,0,1,1),"invalid context rejects item state");
    Check(!messages.SendConsumableVisibility(target,0xffff),"invalid context rejects mask");
    Check(!messages.SendRespawnCountdown(target,false,4),"invalid context rejects countdown");
    Check(!messages.SendRespawnUnavailable(target),"invalid context rejects unavailable");
    Check(!messages.SendRevivalHint(target,false),"invalid context rejects revival hint");
    Check(!messages.SendRespawnReloadWait(target,4),"invalid context rejects cooldown hint");
    Check(packets.size()==count&&!opened,"rejection makes no engine packet");
}
void RegistrationCases(){
    Install();edict_t player{};CCSOLScenarioMessages messages;NoSends(messages,&player);
    Check(messages.Register(),"valid eleven-message registration");
    const std::vector<std::pair<std::string,int>> expected{{"MPToCL",-1},{"ZBSVer2",-1},{"MPToCL2",-1},{"CsoSkState",15},
        {csol::monster_effects::Message,7},{csol::playerclip::Message,-1},{csol::frozen_death::IceMessage,int(csol::frozen_death::IcePayloadSize)},
        {csol::frozen_smoke::SmokeMessage,int(csol::frozen_smoke::PayloadSize)},{"HudIcon",2},{"TextMsg",-1},{"HudTextArgs",-1}};
    Check(registrations==expected,"exact channel names lengths and registration order");
    for(std::size_t i=0;i<expected.size();++i)for(int value:{-1,0,63,256,INT_MAX}){
        failRegistration.clear();Check(messages.Register(),"restore before partial registration failure");
        failRegistration=expected[i].first;badId=value;registrations.clear();Check(!messages.Register(),"invalid registered message ID rejected");
        Check(registrations.size()==i+1&&messages.MessageId()==0,"partial failure clears every channel and stops at failure");NoSends(messages,&player);
    }
    failRegistration.clear();Check(messages.Register(),"register restored");g_engfuncs.pfnRegUserMsg=nullptr;
    Check(!messages.Register(),"absent registration callback fails");NoSends(messages,&player);
}
void WireCases(){
    Install();CCSOLScenarioMessages messages;Check(messages.Register(),"wire test registered");edict_t player{};
    for(unsigned slot=0;slot<4;++slot)for(float cooldown:{0.f,.125f,1.1f,15.75f,std::numeric_limits<float>::max()})
    for(int count:{-32768,-1,0,1,255,256,32767}){
        packets.clear();Check(messages.SendConsumableState(&player,slot,cooldown,count),"valid original item state sent");
        Check(packets.size()==1&&packets[0].destination==MSG_ONE&&packets[0].id==ids.at("MPToCL")&&packets[0].recipient==&player,"private message routing");
        const auto& p=packets[0];Check(p.bytes.size()==9&&p.writes=="bbbls","raw float32 LONG produces exact 9 bytes, not WRITE_COORD");
        hud::Message decoded;Check(hud::DecodeMP(p.bytes,decoded)==hud::ParseResult::Accepted&&decoded.kind==hud::Kind::Consumable&&decoded.index==slot&&decoded.count==count&&
            std::bit_cast<std::uint32_t>(decoded.cooldown)==std::bit_cast<std::uint32_t>(cooldown),"actual client consumes exact server float and signed count");
    }
    for(auto mask:{0u,1u,0x400u,0x8000u,0xffffu}){
        packets.clear();Check(messages.SendConsumableVisibility(&player,std::uint16_t(mask)),"full HudIcon mask sent");
        Check(packets.size()==1&&packets[0].id==ids.at("HudIcon")&&packets[0].bytes.size()==2&&hud::Word(packets[0].bytes,0)==mask,"mask preserves every bit including high bit");
    }
    for(bool automatic:{false,true})for(int seconds:{0,1,45,999,INT_MAX}){
        packets.clear();Check(messages.SendRespawnCountdown(&player,automatic,seconds),"countdown sent");hud::CenterMessage decoded;
        Check(packets.size()==1&&packets[0].id==ids.at("TextMsg")&&packets[0].writes=="btt"&&
            hud::DecodeText(packets[0].bytes,decoded)==hud::ParseResult::Accepted&&decoded.token==(automatic?0u:2u)&&decoded.seconds==seconds,"actual native TextMsg consumed with decimal countdown");
    }
    packets.clear();Check(messages.SendRespawnUnavailable(&player),"unavailable sent");hud::CenterMessage text;
    Check(hud::DecodeText(packets.back().bytes,text)==hud::ParseResult::Accepted&&text.token==1&&packets.back().writes=="bt","unavailable carries no invented argument");
    for(bool retry:{false,true}){packets.clear();Check(messages.SendRevivalHint(&player,retry),"R prompt sent");unsigned hint=99;
        Check(packets.size()==1&&packets[0].id==ids.at("HudTextArgs")&&packets[0].writes=="tbb"&&
            hud::DecodeHint(packets[0].bytes,true,hint)==hud::ParseResult::Accepted&&hint==unsigned(retry),"actual hint client receives string hint flag and zero argc");}
    for(int seconds:{0,1,45,255,256,32768,INT_MAX}){packets.clear();Check(messages.SendRespawnReloadWait(&player,seconds),"cooldown warning sent");std::int32_t value=-1;
        Check(packets.size()==1&&packets[0].destination==MSG_ONE_UNRELIABLE&&packets[0].recipient==&player&&
            packets[0].id==ids.at("ZBSVer2")&&packets[0].writes=="bl"&&packets[0].bytes.size()==5&&
            hud::DecodeReloadWait(packets[0].bytes,value)==hud::ParseResult::Accepted&&value==seconds,"original cooldown channel carries 0x1C and signed LONG exactly");}
}
void InvalidAndReentry(){
    Install();CCSOLScenarioMessages messages;Check(messages.Register(),"boundary test registered");edict_t player{};NoSends(messages,nullptr);
    player.free=1;NoSends(messages,&player);player.free=0;
    for(float bad:{-1.f,-.001f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
        Check(!messages.SendConsumableState(&player,0,bad,1),"invalid float rejected before any packet");
    for(int bad:{INT_MIN,-32769,32768,INT_MAX})Check(!messages.SendConsumableState(&player,0,1,bad),"count outside signed16 rejected");
    for(unsigned bad:{4u,255u,UINT_MAX})Check(!messages.SendConsumableState(&player,bad,1,1),"slot outside original four rejected");
    Check(!messages.SendRespawnCountdown(&player,false,-1)&&packets.empty(),"negative countdown rejects without packets");
    Check(!messages.SendRespawnReloadWait(&player,-1)&&!messages.SendRespawnReloadWait(&player,INT_MIN)&&packets.empty(),"negative cooldown warning rejects without packets");
    const auto full=g_engfuncs;
    for(int missing=0;missing<6;++missing){g_engfuncs=full;
        if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
        if(missing==2)g_engfuncs.pfnWriteByte=nullptr;if(missing==3)g_engfuncs.pfnWriteLong=nullptr;
        if(missing==4)g_engfuncs.pfnWriteShort=nullptr;if(missing==5)g_engfuncs.pfnWriteString=nullptr;
        packets.clear();
        if(missing<5)Check(!messages.SendConsumableState(&player,0,1,1)&&packets.empty(),"missing item callback sends nothing");
        if(missing==0||missing==1||missing==4)Check(!messages.SendConsumableVisibility(&player,3)&&packets.empty(),"missing mask callback sends nothing");
        if(missing==0||missing==1||missing==2||missing==5)Check(!messages.SendRevivalHint(&player,false)&&!messages.SendRespawnCountdown(&player,false,2)&&packets.empty(),"missing text callback sends nothing");
        if(missing==0||missing==1||missing==2||missing==3)Check(!messages.SendRespawnReloadWait(&player,4)&&packets.empty(),"missing cooldown callback sends nothing");
    }
    g_engfuncs=full;
    for(int outer=0;outer<5;++outer){packets.clear();unsigned callbacks{};
        reentry=[&](char){++callbacks;Check(!messages.SendConsumableState(&player,0,1,1)&&!messages.SendConsumableVisibility(&player,1)&&
            !messages.SendRespawnCountdown(&player,false,1)&&!messages.SendRevivalHint(&player,false)&&!messages.SendRoundOutcome(false)&&
            !messages.SendRespawnReloadWait(&player,4),"all writers reject reentry through Begin/write/End");};
        bool sent=outer==0?messages.SendConsumableState(&player,0,1,1):outer==1?messages.SendConsumableVisibility(&player,3):
            outer==2?messages.SendRespawnCountdown(&player,true,1):outer==3?messages.SendRevivalHint(&player,true):messages.SendRespawnReloadWait(&player,4);
        reentry={};Check(sent&&packets.size()==1&&callbacks>2,"one complete outer packet survives nested attempts");
        Check(messages.SendRevivalHint(&player,false),"guard releases after packet");
    }
    packets.clear();reentry=[&](char kind){if(kind=='m')messages=CCSOLScenarioMessages{};};
    Check(messages.SendConsumableState(&player,3,1,2),"map-owned message object can retire after packet starts");reentry={};
    Check(packets.size()==1&&packets[0].id==ids.at("MPToCL")&&packets[0].bytes.size()==9,"inflight packet uses captured ID");NoSends(messages,&player);
}
int main(){try{RegistrationCases();WireCases();InvalidAndReentry();std::cout<<"respawn integration writer checks="<<checks<<"\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}}
