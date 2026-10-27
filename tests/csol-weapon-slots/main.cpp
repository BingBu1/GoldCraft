// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_slots.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace csol;
int checks;
void Check(bool condition){++checks;if(!condition){std::cerr<<"FAIL "<<checks<<'\n';std::exit(1);}}
int main()
{
    for(int id=-100;id<65536;++id)
    {
        const auto mask=LegacyWeaponMask(id);
        Check((mask!=0)==(id>0 && id<32));
        Check(PredictionSlot(id,true)<32);
        if(ExtendedWeaponId(id)){Check(mask==0);Check(PredictionSlot(id,true)==0);Check(PredictionSlot(id,false)==-1);}
    }
    Check(LegacyWeaponMask(std::numeric_limits<int>::min())==0);
    Check(LegacyWeaponMask(std::numeric_limits<int>::max())==0);
    WeaponDefinition def{725,0,8,15,16,200,100,80,0,"weapon_beamgun"}, decoded;
    auto bytes=EncodeWeaponDefinition(def);Check(DecodeWeaponDefinition(bytes,decoded)&&decoded==def);
    for(std::size_t n=0;n<bytes.size();++n)Check(!DecodeWeaponDefinition(std::span(bytes).first(n),decoded)&&decoded==def);
    for(const auto name:{"weapon_a;quit","weapon_../x","weapon_","weapon_a\nquit"})
    {auto bad=def;bad.name=name;Check(EncodeWeaponDefinition(bad).empty());}
    auto bad=def;bad.id=1024;Check(EncodeWeaponDefinition(bad).empty());
    bad=def;bad.name=std::string("weapon_a\0quit",13);Check(EncodeWeaponDefinition(bad).empty());
    bytes.push_back(std::byte{});Check(!DecodeWeaponDefinition(bytes,decoded));
    for(const int id:{0,1,30,32,220,725,726,1023})
        for(const int clip:{-1,0,1,127,128,255,400,32767})
        {
            WeaponCurrent value{id,1,clip,400,100},read;
            auto packet=EncodeWeaponCurrent(value);Check(DecodeWeaponCurrent(packet,read)&&read==value);
            for(std::size_t n=0;n<packet.size();++n)Check(!DecodeWeaponCurrent(std::span(packet).first(n),read)&&read==value);
        }
    WeaponOwnership owned{};owned[1]=owned[30]=owned[220]=owned[725]=owned[726]=owned[1023]=true;
    WeaponInventoryReceiver receiver;
    auto packets=EncodeWeaponInventory(owned,65535);Check(packets.size()==1);
    Check(receiver.Receive(packets[0])&&receiver.Owned()==owned);
    WeaponOwnership all{};for(int id=1;id<WeaponIdLimit;++id)all[id]=ValidWeaponId(id);
    auto many=EncodeWeaponInventory(all,0);Check(many.size()>1);
    for(std::size_t n=0;n<many.size();++n)
    {
        Check(many[n].size()<=192);Check(receiver.Receive(many[n]));
        Check(receiver.Owned()==(n+1==many.size()?all:owned));
    }
    Check(!receiver.Receive(many.back())&&receiver.Owned()==all);
    Check(receiver.Receive(many[0]));Check(!receiver.Receive(many[2])&&receiver.Owned()==all);
    auto wrong=many[1];wrong[1]=std::byte{1};
    Check(receiver.Receive(many[0]));Check(!receiver.Receive(wrong)&&receiver.Owned()==all);
    for(std::size_t n=0;n<packets[0].size();++n)
        Check(!receiver.Receive(std::span(packets[0]).first(n))&&receiver.Owned()==all);
    auto duplicate=packets[0];duplicate[8]=duplicate[6];duplicate[9]=duplicate[7];
    Check(!receiver.Receive(duplicate)&&receiver.Owned()==all);
    auto invalid=packets[0];invalid[6]=std::byte{31};
    Check(!receiver.Receive(invalid)&&receiver.Owned()==all);
    WeaponOwnership empty{};auto clear=EncodeWeaponInventory(empty,1);
    Check(receiver.Receive(clear[0])&&receiver.Owned()==empty);
    receiver.Reset();Check(receiver.Owned()==empty);
    for(int id:{220,725,1023})for(int secondary:{0,1})for(int amount:{1,30,255,256,32767}){
        AmmoPickup pickup{id,secondary,amount},read;auto packet=EncodeAmmoPickup(pickup);
        Check(DecodeAmmoPickup(packet,read)&&read.id==id&&read.secondary==secondary&&read.amount==amount);
        for(std::size_t n=0;n<packet.size();++n)Check(!DecodeAmmoPickup(std::span(packet).first(n),read));
        packet[0]=std::byte{2};Check(!DecodeAmmoPickup(packet,read));
    }
    for(int amount:{-1,0,32768})Check(EncodeAmmoPickup({220,0,amount}).empty());
    Check(EncodeAmmoPickup({28,0,1}).empty());Check(EncodeAmmoPickup({220,2,1}).empty());
    std::cout<<"PASS "<<checks<<" checks\n";
}
