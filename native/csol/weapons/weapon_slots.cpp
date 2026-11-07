// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_slots.hpp"
#include <algorithm>

namespace csol
{
namespace
{
void Byte(std::vector<std::byte>& out, int value) { out.push_back(std::byte(value & 255)); }
void Short(std::vector<std::byte>& out, int value) { Byte(out,value); Byte(out,value >> 8); }
int Byte(std::span<const std::byte> b, std::size_t i) { return std::to_integer<unsigned char>(b[i]); }
int Short(std::span<const std::byte> b, std::size_t i) { return Byte(b,i) | Byte(b,i+1) << 8; }
int SignedShort(std::span<const std::byte> b, std::size_t i) { const int v=Short(b,i);return v>=32768?v-65536:v; }
bool Name(const std::string& s)
{
    return s.starts_with("weapon_") && s.size()>7 && s.size()<64 &&
        std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});
}
bool Definition(const WeaponDefinition& v)
{
    return ValidWeaponId(v.id) && v.slot>=0 && v.slot<8 && v.position>=0 && v.position<256 &&
        v.ammo1>=-1 && v.ammo1<32768 && v.ammo2>=-1 && v.ammo2<32768 &&
        v.maxAmmo1>=-1 && v.maxAmmo1<32768 && v.maxAmmo2>=-1 && v.maxAmmo2<32768 &&
        v.maxClip>=-1 && v.maxClip<32768 && v.flags>=0 && v.flags<65536 && Name(v.name);
}
bool Current(const WeaponCurrent& v)
{
    return (v.id==0 || ValidWeaponId(v.id)) && v.state>=0 && v.state<256 &&
        v.clip>=-1 && v.clip<32768 && v.reserve>=0 && v.reserve<32768 && v.secondary>=0 && v.secondary<32768;
}
}
std::vector<std::byte> EncodeWeaponDefinition(const WeaponDefinition& v)
{
    if(!Definition(v))return {};
    std::vector<std::byte> b;Byte(b,WeaponProtocolVersion);Short(b,v.id);Byte(b,v.slot);Byte(b,v.position);
    Short(b,v.ammo1);Short(b,v.ammo2);Short(b,v.maxAmmo1);Short(b,v.maxAmmo2);Short(b,v.maxClip);Short(b,v.flags);
    for(char c:v.name)Byte(b,c);Byte(b,0);return b;
}
std::vector<std::byte> EncodeAmmoPickup(const AmmoPickup& v)
{
    if(!ExtendedWeaponId(v.id) || v.secondary<0 || v.secondary>1 || v.amount<=0 || v.amount>32767)return {};
    std::vector<std::byte> b;Byte(b,WeaponProtocolVersion);Short(b,v.id);Byte(b,v.secondary);Short(b,v.amount);return b;
}
bool DecodeAmmoPickup(std::span<const std::byte> b,AmmoPickup& out)
{
    if(b.size()!=6 || Byte(b,0)!=WeaponProtocolVersion)return false;
    AmmoPickup v{Short(b,1),Byte(b,3),SignedShort(b,4)};
    if(EncodeAmmoPickup(v).empty())return false;
    out=v;return true;
}
bool DecodeWeaponDefinition(std::span<const std::byte> b,WeaponDefinition& out)
{
    if(b.size()<26 || b.size()>81 || Byte(b,0)!=WeaponProtocolVersion || b.back()!=std::byte{})return false;
    WeaponDefinition v;v.id=Short(b,1);v.slot=Byte(b,3);v.position=Byte(b,4);
    v.ammo1=SignedShort(b,5);v.ammo2=SignedShort(b,7);v.maxAmmo1=SignedShort(b,9);v.maxAmmo2=SignedShort(b,11);
    v.maxClip=SignedShort(b,13);v.flags=Short(b,15);
    v.name.assign(reinterpret_cast<const char*>(b.data()+17),b.size()-18);
    if(!Definition(v))return false;out=std::move(v);return true;
}
std::vector<std::byte> EncodeWeaponCurrent(const WeaponCurrent& v)
{
    if(!Current(v))return {};
    std::vector<std::byte> b;Byte(b,WeaponProtocolVersion);Short(b,v.id);Byte(b,v.state);
    Short(b,v.clip);Short(b,v.reserve);Short(b,v.secondary);return b;
}
bool DecodeWeaponCurrent(std::span<const std::byte> b,WeaponCurrent& out)
{
    if(b.size()!=10 || Byte(b,0)!=WeaponProtocolVersion)return false;
    WeaponCurrent v{Short(b,1),Byte(b,3),SignedShort(b,4),SignedShort(b,6),SignedShort(b,8)};
    if(!Current(v))return false;out=v;return true;
}
std::vector<std::vector<std::byte>> EncodeWeaponInventory(const WeaponOwnership& owned,std::uint16_t generation)
{
    std::vector<int> ids;for(int id=1;id<WeaponIdLimit;++id)if(owned[id] && ValidWeaponId(id))ids.push_back(id);
    constexpr std::size_t perPart=90;
    const std::size_t parts=std::max(std::size_t{1},(ids.size()+perPart-1)/perPart);
    std::vector<std::vector<std::byte>> result;
    for(std::size_t part=0;part<parts;++part)
    {
        auto& b=result.emplace_back();Byte(b,WeaponProtocolVersion);Short(b,generation);Byte(b,int(part));
        Byte(b,part+1==parts?1:0);
        const auto end=std::min(ids.size(),(part+1)*perPart);Byte(b,int(end-part*perPart));
        for(auto i=part*perPart;i<end;++i)Short(b,ids[i]);
    }
    return result;
}
bool WeaponInventoryReceiver::Receive(std::span<const std::byte> b)
{
    const auto reject=[this](){receiving_=false;return false;};
    if(b.size()<6 || b.size()>186 || Byte(b,0)!=WeaponProtocolVersion || Byte(b,4)>1 ||
        b.size()!=6+2*static_cast<std::size_t>(Byte(b,5)))return reject();
    const auto generation=static_cast<std::uint16_t>(Short(b,1));const unsigned part=Byte(b,3);
    if(part==0){pending_.fill(false);generation_=generation;nextPart_=0;lastId_=0;receiving_=true;}
    if(!receiving_ || generation!=generation_ || part!=nextPart_)return reject();
    for(std::size_t i=6;i<b.size();i+=2)
    {
        const int id=Short(b,i);if(!ValidWeaponId(id)||id<=lastId_)return reject();
        pending_[id]=true;lastId_=id;
    }
    ++nextPart_;
    if(Byte(b,4)){owned_=pending_;receiving_=false;}
    return true;
}
void WeaponInventoryReceiver::Reset(){owned_.fill(false);pending_.fill(false);receiving_=false;nextPart_=0;lastId_=0;generation_=0;}
}
