// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_famas_resources.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
namespace
{
using namespace csol::server::cof;
using namespace guest::cof::resources;
struct Asset {std::string path;std::vector<byte> bytes;};
std::vector<Asset> originals,files;
bool active=true;
unsigned calls{},cancelAt{},loads{},frees{},registered{},checks{};
std::string rejected;
std::vector<std::string> paths;
void CheckAt(bool value,int line)
{++checks;if(!value){std::cerr<<"FAMAS resource check "<<checks<<" failed line "<<line<<'\n';std::exit(2);}}
#define Check(value) CheckAt((value),__LINE__)
Asset* Find(const char* path)
{
    const auto i=std::find_if(files.begin(),files.end(),[&](const Asset& a){return a.path==path;});
    return i==files.end()?nullptr:&*i;
}
void Call(const char* path,bool cleanup=false)
{
    Check(active||cleanup);paths.emplace_back(path);
    if(++calls==cancelAt)active=false;
}
bool Current(void*) noexcept {return active;}
FamasResources Baseline()
{FamasResources value{};value.generation=918;value.viewModelString=71;return value;}
void Reset()
{
    files=originals;active=true;calls=loads=frees=registered=0;cancelAt=0;rejected.clear();paths.clear();
    g_engfuncs={};
    g_engfuncs.pfnGetFileSize=[](const char* path)->int {
        Call(path);const auto* file=Find(path);return file?int(file->bytes.size()):0;
    };
    g_engfuncs.pfnLoadFileForMe=[](const char* path,int* size)->byte* {
        Call(path);const auto* file=Find(path);*size=file?int(file->bytes.size()):0;
        if(!*size)return nullptr;
        auto* data=new byte[std::size_t(*size)];std::memcpy(data,file->bytes.data(),*size);++loads;return data;
    };
    g_engfuncs.pfnFreeFile=[](void* bytes){Call("free",true);++frees;delete[] static_cast<byte*>(bytes);};
    g_engfuncs.pfnPrecacheModel=[](const char* path)->int {
        Call(path);const auto* file=Find(path);Check(file&&!file->bytes.empty());
        ++registered;return rejected==path?0:17+int(registered);
    };
    g_engfuncs.pfnPrecacheSound=[](const char* path)->int {
        Call(path);const auto* file=Find(("sound/"+std::string(path)).c_str());
        Check(file&&!file->bytes.empty());++registered;return rejected==path?0:17+int(registered);
    };
    g_engfuncs.pfnPrecacheGeneric=[](const char* path)->int {
        Call(path);Check(Find(path)!=nullptr);++registered;return rejected==path?-1:0;
    };
    g_engfuncs.pfnAllocString=[](const char* path)->int {
        Call(path);++registered;return rejected==path?0:64+int(registered);
    };
}
void Remove(const std::string& path)
{files.erase(std::remove_if(files.begin(),files.end(),[&](const Asset& a){return a.path==path;}),files.end());}
std::vector<byte> Read(const std::string& path)
{
    std::ifstream f(path,std::ios::binary);Check(bool(f));
    return {std::istreambuf_iterator<char>(f),{}};
}
void Text(const std::string& value)
{auto* file=Find(FamasInventory.host);Check(file!=nullptr);file->bytes.assign(value.begin(),value.end());}
}
int main(int argc,char** argv)
{
    if(argc!=3)return 1;
    const std::string root=argv[1],inherited=argv[2];
    for(const auto& p:FamasModels)
        if(p.original)
        {
            const std::string base=std::strcmp(p.original,"models/w_9mmclip.mdl")?root:inherited;
            originals.push_back({p.host,Read(base+"/"+p.original)});
        }
    // A controlled valid MDL tests optional P registration. This is not an
    // authored FAMAS P model and is never exported as a game resource.
    originals.push_back({FamasModels[2].host,Read(root+"/models/weapons/famas/w_famas.mdl")});
    for(const auto& p:FamasSounds)originals.push_back({"sound/"+std::string(p.host),Read(root+"/sound/"+p.original)});
    originals.push_back({FamasInventory.host,Read(root+"/"+FamasInventory.original)});
    Reset();FamasResources result{};
    Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
    Check(result.generation==7&&result.preparedParts==(1u<<unsigned(FamasResourcePart::Count))-1);
    Check(result.aimOffset==Vector(.05F,-5,2.5F)&&result.aimZoom==50&&result.sequences.present==(1ULL<<24)-1);
    Check(result.viewModelString>0&&result.worldModelString>0&&result.playerModelString>0&&result.inventoryItemString>0);
    Check(result.inventoryDownload==0&&loads==frees);
    const unsigned normalCalls=calls;
    const auto completeTimes=result.sequences;
    {
        const auto* view=Find(FamasModels[0].host);guest::cof::MagazineSequenceTimes strict;
        Check(guest::cof::PrepareMagazineSequenceTimes(
            {reinterpret_cast<const std::byte*>(view->bytes.data()),view->bytes.size()},24,strict)==
            guest::cof::MagazineModelResult::Prepared);
        Check(strict.present==completeTimes.present&&strict.duration==completeTimes.duration);
    }
    Check(!result.Ready(static_cast<FamasResourcePart>(999)));
    const auto old=Baseline();
    // Losing one optional track must preserve every other real duration.
    for(unsigned i=0;i<24;++i)
    {
        Reset();auto* view=Find(FamasModels[0].host);studiohdr_t h;
        std::memcpy(static_cast<void*>(&h),view->bytes.data(),sizeof(h));
        const float unavailable=0;
        std::memcpy(view->bytes.data()+h.seqindex+i*sizeof(mstudioseqdesc_t)+32,&unavailable,sizeof(unavailable));
        // One-frame tracks do not divide by fps in the original helper.
        const int frames=2;std::memcpy(view->bytes.data()+h.seqindex+i*sizeof(mstudioseqdesc_t)+56,&frames,sizeof(frames));
        result={};Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
        Check(!result.Ready(FamasResourcePart::SequenceTimes)&&
            result.sequences.present==(completeTimes.present&~(1ULL<<i)));
        for(unsigned j=0;j<24;++j)if(i!=j)
            Check(std::bit_cast<unsigned>(result.sequences.duration[j])==std::bit_cast<unsigned>(completeTimes.duration[j]));
        Check(loads==frees);
    }
    Reset();
    {
        auto* view=Find(FamasModels[0].host);const int count=23;
        std::memcpy(view->bytes.data()+164,&count,sizeof(count));
        result={};Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
        Check(!result.Ready(FamasResourcePart::SequenceTimes)&&result.sequences.present==(1ULL<<23)-1);
        Check(result.sequences.duration[5]==completeTimes.duration[5]&&result.sequences.duration[6]==completeTimes.duration[6]);
    }
    Reset();
    {
        auto* view=Find(FamasModels[0].host);studiohdr_t h;
        std::memcpy(static_cast<void*>(&h),view->bytes.data(),sizeof(h));
        const int one=1;const float unused=NAN;
        std::memcpy(view->bytes.data()+h.seqindex+56,&one,sizeof(one));
        std::memcpy(view->bytes.data()+h.seqindex+32,&unused,sizeof(unused));
        result={};Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
        Check(result.Ready(FamasResourcePart::SequenceTimes)&&result.sequences.duration[0]==0);
    }
    for(unsigned i=0;i<2;++i)
    {
        Reset();Remove(FamasModels[i].host);std::memcpy(&result,&old,sizeof(old));
        Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::MissingFile);
        Check(!std::memcmp(&old,&result,sizeof(old))&&registered==0&&loads==frees);
        Reset();Find(FamasModels[i].host)->bytes[0]=0;std::memcpy(&result,&old,sizeof(old));
        Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::InvalidModel);
        Check(!std::memcmp(&old,&result,sizeof(old))&&registered==0&&loads==frees);
    }
    Reset();Remove(FamasInventory.host);std::memcpy(&result,&old,sizeof(old));
    Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::MissingFile&&registered==0);
    for(const std::string& bad:std::array<std::string,4>{"classname { data weapon_m16 }","classname { data weapon_famas } classname { data weapon_famas }",
            "classname { data weapon_famas",std::string("classname { data weapon_famas }")+char(0)})
    {
        Reset();Text(bad);std::memcpy(&result,&old,sizeof(old));
        Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::InvalidMetadata);
        Check(!std::memcmp(&old,&result,sizeof(old))&&registered==0&&loads==frees);
    }
    for(const std::string optional:{"classname { data weapon_famas }",
            "classname { data weapon_famas } ironsight_zoom { data nope }",
            "classname { data weapon_famas } ironsight_zoom { data \"\" }",
            "classname { data weapon_famas } ironsight_forward { data nan }"})
    {
        Reset();Text(optional);result={};Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
        Check(result.generation==7&&!result.Ready(FamasResourcePart::Aim)&&result.Ready(FamasResourcePart::SequenceTimes));
        Check(result.parts[unsigned(FamasResourcePart::Aim)].status==ResourceStatus::InvalidMetadata&&loads==frees);
    }
    constexpr FamasResourcePart modelParts[]{FamasResourcePart::PlayerModel,FamasResourcePart::NullModel,
        FamasResourcePart::ShellModel,FamasResourcePart::ClipModel};
    for(unsigned i=2;i<FamasModels.size();++i)for(int mode:{0,1,2})
    {
        Reset();const auto* path=FamasModels[i].host;
        if(mode==0)Remove(path);else if(mode==1)Find(path)->bytes[0]=0;else rejected=path;
        result={};Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
        const auto part=modelParts[i-2];Check(!result.Ready(part));
        const auto wanted=mode==0?ResourceStatus::MissingFile:mode==1?ResourceStatus::InvalidModel:ResourceStatus::EngineRejected;
        Check(result.parts[unsigned(part)].status==wanted&&result.Ready(FamasResourcePart::Aim)&&loads==frees);
        if(i==2)Check(!result.playerModelString);
    }
    constexpr FamasResourcePart soundParts[]{FamasResourcePart::LegacyFire,FamasResourcePart::FirstDraw,
        FamasResourcePart::ItemGet,FamasResourcePart::Fire};
    for(unsigned i=0;i<FamasSounds.size();++i)for(bool missing:{false,true})
    {
        Reset();if(missing)Remove("sound/"+std::string(FamasSounds[i].host));else rejected=FamasSounds[i].host;
        result={};Check(bool(PrepareFamasResources({nullptr,Current,7},result)));
        Check(!result.Ready(soundParts[i])&&result.generation==7&&loads==frees);
    }
    Reset();g_engfuncs.pfnPrecacheSound=nullptr;result={};
    Check(bool(PrepareFamasResources({nullptr,Current,7},result))&&result.Ready(FamasResourcePart::Aim));
    for(auto part:soundParts)Check(!result.Ready(part)&&result.parts[unsigned(part)].status==ResourceStatus::MissingEngine);
    for(unsigned i=0;i<2;++i)
    {
        Reset();rejected=FamasModels[i].host;std::memcpy(&result,&old,sizeof(old));
        Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::EngineRejected);
        Check(!std::memcmp(&old,&result,sizeof(old))&&loads==frees);
    }
    Reset();g_engfuncs.pfnAllocString=nullptr;std::memcpy(&result,&old,sizeof(old));
    Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::MissingEngine&&calls==0);
    Check(!std::memcmp(&old,&result,sizeof(old)));
    for(unsigned at=1;at<=normalCalls;++at)
    {
        Reset();cancelAt=at;std::memcpy(&result,&old,sizeof(old));
        Check(PrepareFamasResources({nullptr,Current,7},result).status==ResourceStatus::Cancelled);
        Check(!std::memcmp(&old,&result,sizeof(old))&&loads==frees);
    }
    Reset();std::memcpy(&result,&old,sizeof(old));Check(PrepareFamasResources({nullptr,Current,0},result).status==ResourceStatus::InvalidAuthority);
    Check(!std::memcmp(&old,&result,sizeof(old))&&calls==0);
    std::cout<<checks<<" FAMAS resource SDK checks; "<<normalCalls<<" actual callback retirement points passed\n";
}
