// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequence_owners.hpp"
#include "sequences.hpp"
#include <cstdlib>
#include <iostream>
#include <malloc.h>
#include <new>

namespace allocation_probe
{
bool enabled{};
std::size_t calls{};
void Count(){if(enabled)++calls;}
}
void* operator new(std::size_t size)
{
    allocation_probe::Count();
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t size,std::align_val_t alignment)
{
    allocation_probe::Count();
    if(void* p=_aligned_malloc(size?size:1,std::size_t(alignment)))return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size,std::align_val_t alignment){return ::operator new(size,alignment);}
void operator delete(void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{_aligned_free(p);}

namespace
{
using Bytes=std::vector<std::byte>;
using Registry=csol::SequenceOwnerRegistry;
using Prepared=csol::PreparedSequenceModel;
using Ticket=csol::SequenceSample;
using Result=csol::SequenceOwnerCheck;
std::size_t checks{};
void Check(bool value,const char* why)
{++checks;if(!value)throw std::runtime_error(why);}
template<class T> void Put(Bytes& bytes,std::size_t at,T value)
{Check(at<=bytes.size() && sizeof(T)<=bytes.size()-at,"fixture write overflow");std::memcpy(bytes.data()+at,&value,sizeof(value));}
std::uint32_t Word(const Bytes& bytes,std::size_t at)
{Check(at<=bytes.size() && 4<=bytes.size()-at,"fixture read overflow");std::uint32_t value;std::memcpy(&value,bytes.data()+at,4);return value;}
std::uintptr_t Address(const Bytes& bytes){return reinterpret_cast<std::uintptr_t>(bytes.data());}
const void* Pointer(std::uintptr_t value){return reinterpret_cast<const void*>(value);}
const void* Descriptor(const Bytes& bytes,std::size_t index=0)
{return Pointer(Address(bytes)+Word(bytes,168)+index*176);}
const void* Animation(const Bytes& bytes,const Ticket& sample,std::size_t offset=0)
{return Pointer(Address(bytes)+sample.selection.animation+offset);}
bool Empty(const Ticket& ticket)
{return !ticket.owner && !ticket.base && !ticket.generation && !ticket.selection.animation && !ticket.selection.frames;}
Bytes Model(bool texture,int frames=3,int bones=2,int count=101)
{
    constexpr int blends=2;
    const auto textureIndex=244+std::size_t(bones)*112;
    const auto descriptors=textureIndex+(texture?80:0),anim=descriptors+count*176;
    const auto tables=std::size_t(bones)*blends*12;
    const auto textureData=anim+tables+8;
    Bytes bytes(textureData+(texture?32:0));
    Put(bytes,0,0x54534449u);Put(bytes,4,10u);Put(bytes,72,std::uint32_t(bytes.size()));
    Put(bytes,140,bones);Put(bytes,144,244);Put(bytes,164,count);Put(bytes,168,std::uint32_t(descriptors));
    Put(bytes,180,texture?1:0);Put(bytes,184,std::uint32_t(texture?textureIndex:0));Put(bytes,188,std::uint32_t(texture?textureData:0));
    for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)Put(bytes,244+bone*112+88+axis*4,axis<3?.01f:.0003f);
    for(int i=0;i<count;++i)
    {
        const auto at=descriptors+i*176;
        Put(bytes,at+56,frames);Put(bytes,at+120,blends);Put(bytes,at+124,std::uint32_t(anim));
    }
    // Two blends/two bones, one rotation channel; final next endpoint is explicit.
    Put(bytes,anim+6,std::uint16_t(tables));bytes[anim+tables]=std::byte{1};bytes[anim+tables+1]=std::byte(frames);
    Put(bytes,anim+tables+2,std::int16_t{123});bytes[anim+tables+4]=bytes[anim+tables+5]=std::byte{1};
    Put(bytes,anim+tables+6,std::int16_t{321});
    if(texture){Put(bytes,textureIndex+76,std::uint32_t(textureData));std::fill(bytes.begin()+textureData,bytes.end(),std::byte{0x75});}
    return bytes;
}
Bytes Seq(int frames=3,int bones=2)
{
    const auto tables=std::size_t(bones)*2*12,block=tables+8,descriptors=8+2*block;
    Bytes bytes(descriptors+2*176);Put(bytes,0,2);Put(bytes,4,std::uint32_t(descriptors));
    for(int i=0;i<2;++i)
    {
        const auto anim=8+i*block,at=descriptors+i*176;
        Put(bytes,anim+6,std::uint16_t(tables));bytes[anim+tables]=std::byte{1};bytes[anim+tables+1]=std::byte(frames);
        Put(bytes,anim+tables+2,std::int16_t{123});bytes[anim+tables+4]=bytes[anim+tables+5]=std::byte{1};
        Put(bytes,anim+tables+6,std::int16_t{321});
        Put(bytes,at+56,frames);Put(bytes,at+120,2);Put(bytes,at+124,std::uint32_t(10000+i*block));
    }
    return bytes;
}
Bytes Packed(bool texture=true,int frames=3,int bones=2)
{return csol::AppendSequences(Model(texture,frames,bones),{Seq(frames,bones)});}
Bytes Prefix(const Bytes& file)
{
    const auto size=Word(file,184)?Word(file,188):Word(file,72);
    return {file.begin(),file.begin()+size};
}
Ticket Select(Registry& registry,std::uintptr_t owner,const Bytes& live,std::size_t index=0)
{
    Ticket ticket;
    Check(registry.Select(owner,Address(live),live.data(),Descriptor(live,index),ticket)==Result::Ready,"valid owned selection rejected");
    return ticket;
}
Ticket Publish(Registry& registry,std::uintptr_t owner,const Bytes& file,Bytes& live)
{
    auto prepared=Prepared::Read(file);
    const auto token=registry.BeginLoad(owner);
    registry.Allocating(owner,live.size());
    Check(registry.CompleteLoad(owner,token,std::move(prepared),Address(live),live.size()),"valid load publication rejected");
    return Select(registry,owner,live);
}
template<class Callback> void Throws(Callback callback,const char* why)
{try{callback();}catch(const std::exception&){++checks;return;}throw std::runtime_error(why);}

void PreparationAndPublishing()
{
    for(bool texture:{false,true})
    {
        auto source=Packed(texture);auto live=Prefix(source);const auto owner=std::uintptr_t(0x101);
        auto prepared=Prepared::Read(source);Check(prepared.Tagged(),"tagged source classified ordinary");
        Check(prepared.RetainedBytes()==live.size(),"prepared retained size differs");
        // Destroy the source allocation before publishing; Prepared must own its values.
        std::fill(source.begin(),source.end(),std::byte{0xcc});Bytes{}.swap(source);
        auto copied=prepared;auto moved=std::move(copied);
        Registry registry;auto token=registry.BeginLoad(owner);Ticket output;
        Check(registry.Select(owner,Address(live),live.data(),Descriptor(live),output)==Result::MissingOwner,"pending load selectable");
        Check(!registry.CompleteLoad(owner,token,moved,Address(live),live.size()),"missing actual allocation accepted");
        registry.Allocating(owner,live.size()+16);
        Check(!registry.CompleteLoad(owner,token,moved,Address(live),live.size()),"allocation receipt size mismatch accepted");
        token=registry.BeginLoad(owner);
        registry.Allocating(owner,live.size()-1);
        Check(!registry.CompleteLoad(owner,token,moved,Address(live),live.size()-1),"short allocation accepted");
        token=registry.BeginLoad(owner);
        registry.Allocating(owner,live.size());
        Check(!registry.CompleteLoad(owner,token,moved,0,live.size()),"null live base accepted");
        Check(!registry.CompleteLoad(owner,token,moved,std::numeric_limits<std::uintptr_t>::max()-10,live.size()),"overflowing live range accepted");
        const auto old=Word(live,140);Put(live,140,old+1);
        Check(!registry.CompleteLoad(owner,token,moved,Address(live),live.size()),"header mismatch accepted");Put(live,140,old);
        const auto footer=Word(live,168)-csol::SequenceSafetyFooterBytes;
        const auto records=Word(live,footer+20);
        const auto marker=Word(live,footer);Put(live,footer,marker^1u);
        Check(!registry.CompleteLoad(owner,token,moved,Address(live),live.size()),"copied footer mismatch accepted");Put(live,footer,marker);
        const auto reserved=Word(live,records+28);Put(live,records+28,1u);
        Check(!registry.CompleteLoad(owner,token,moved,Address(live),live.size()),"copied owned record mismatch accepted");Put(live,records+28,reserved);
        Check(registry.CompleteLoad(owner,token,std::move(moved),Address(live),live.size()),"owned metadata failed after original source destruction");
        const auto ticket=Select(registry,owner,live);
        Check(registry.Sample(ticket,Address(live),Animation(live,ticket),2,0,true)==Result::Ready,"published final frame rejected");
        Check(!registry.CompleteLoad(owner,token,prepared,Address(live),live.size()),"same token completed twice");
        registry.CancelLoad(owner,token);
        Check(registry.Sample(ticket,Address(live),Animation(live,ticket),0,0,true)==Result::Ready,"completed token cancel removed publication");
    }
    Check(Prepared::Read(Packed(false,3,128)).Tagged(),"stock128 bone limit rejected");
    const auto manyBones=Packed(false,3,129);
    Throws([&]{Prepared::Read(manyBones);},"tagged model exceeded stock128 bone scratch limit");
    Throws([&]{Prepared::Read(std::span<const std::byte>{});},"empty prepared source accepted");
    Registry registry;Throws([&]{registry.BeginLoad(0);},"null owner accepted");
    std::cout<<"PASS owned preparation, actual allocation receipts, complete prefix and publication contract\n";
}

void RetirementAndNesting()
{
    const auto file=Packed();auto live=Prefix(file),other=Prefix(file);constexpr std::uintptr_t owner=0x201,second=0x202;
    Registry registry;auto prepared=Prepared::Read(file);Ticket empty;
    registry.Allocating(owner,live.size());
    Check(registry.Select(owner,Address(live),live.data(),Descriptor(live),empty)==Result::MissingOwner,"untracked allocation registered owner");
    const auto outer=registry.BeginLoad(owner);registry.Allocating(owner,live.size());
    const auto inner=registry.BeginLoad(owner);registry.Allocating(owner,live.size());
    Check(!registry.CompleteLoad(owner,outer,prepared,Address(live),live.size()),"outer load overrode pending inner load");
    registry.CancelLoad(owner,outer);
    Check(registry.CompleteLoad(owner,inner,prepared,Address(live),live.size()),"outer cancel erased inner pending load");
    auto old=Select(registry,owner,live);
    Check(!registry.CompleteLoad(owner,outer,prepared,Address(live),live.size()),"outer load overrode published inner load");
    registry.CancelLoad(owner,outer);
    Check(registry.Sample(old,Address(live),Animation(live,old),0,0,true)==Result::Ready,"outer cancel erased published inner load");
    auto independent=Publish(registry,second,file,other);
    registry.Allocating(owner,live.size());
    Check(registry.Sample(old,Address(live),Animation(live,old),0,0,true)==Result::MissingOwner,"allocation failed to retire old ticket");
    Check(registry.Select(owner,Address(live),live.data(),Descriptor(live),empty)==Result::MissingOwner,"retired allocation selectable");
    Check(registry.Sample(independent,Address(other),Animation(other,independent),0,0,true)==Result::Ready,"other owner's allocation retired independent owner");
    Check(!registry.CompleteLoad(owner,inner,prepared,Address(live),live.size()),"retired completed token republished");
    const auto conflicting=registry.BeginLoad(owner);registry.Allocating(owner,live.size());
    // Same size does not establish same content after a second allocation.
    registry.Allocating(owner,live.size());
    const auto conflictAnim=Word(live,Word(live,168)+124),conflictTables=Word(live,140)*2*12;
    const auto runByte=live[conflictAnim+conflictTables+1];live[conflictAnim+conflictTables+1]=std::byte{};
    Check(!registry.CompleteLoad(owner,conflicting,prepared,Address(live),live.size()),"second allocation in same token published old proof");
    live[conflictAnim+conflictTables+1]=runByte;
    registry.Allocating(owner,live.size());
    Check(!registry.CompleteLoad(owner,conflicting,prepared,Address(live),live.size()),"third allocation cleared permanent conflict");
    const auto pending=registry.BeginLoad(owner);registry.Allocating(owner,live.size());registry.CancelLoad(owner,pending);
    Check(!registry.CompleteLoad(owner,pending,prepared,Address(live),live.size()),"cancelled load republished");
    const auto resetting=registry.BeginLoad(owner);registry.Allocating(owner,live.size());registry.Reset();
    Check(!registry.CompleteLoad(owner,resetting,prepared,Address(live),live.size()),"pre-reset pending load republished");
    Check(registry.Sample(independent,Address(other),Animation(other,independent),0,0,true)==Result::MissingOwner,"reset preserved old owner proof");
    auto newer=Publish(registry,owner,file,live);
    Check(newer.generation!=old.generation,"reset reused old generation");
    Check(registry.Sample(old,Address(live),Animation(live,old),0,0,true)==Result::MissingOwner,"same owner/base after reset accepted old ticket");
    // Same allocation address, new content under observed Begin/Alloc/Complete.
    auto replacement=file;const auto retained=Prefix(file).size();
    replacement[Word(replacement,168)+32]=std::byte{0x31};
    auto freshPrepared=Prepared::Read(replacement);const auto fresh=registry.BeginLoad(owner);registry.Allocating(owner,live.size());
    std::memcpy(live.data(),replacement.data(),retained);
    Check(registry.CompleteLoad(owner,fresh,std::move(freshPrepared),Address(live),live.size()),"same-address new content publication failed");
    Check(registry.Sample(newer,Address(live),Animation(live,newer),0,0,true)==Result::MissingOwner,"same-address new allocation accepted previous content ticket");
    Select(registry,owner,live);
    std::cout<<"PASS nested same/independent owners, Alloc retirement, cancellation, Reset and same-address content generation\n";
}

void LivePointersAndMovement()
{
    const auto file=Packed();auto live=Prefix(file);Registry registry;constexpr std::uintptr_t owner=0x301;
    const auto old=Publish(registry,owner,file,live);Ticket output=old;
    Check(registry.Select(owner,0,Pointer(1),Pointer(1),output)==Result::WrongHeader && Empty(output),"null current owner cache not rejected before header access");
    output=old;
    Check(registry.Select(owner,Address(live),Pointer(1),Pointer(1),output)==Result::WrongHeader && Empty(output),"foreign header not rejected before dereference");
    for(std::uintptr_t descriptor:{std::uintptr_t(1),Address(live),Address(live)+Word(live,168)+1,Address(live)+live.size()})
    {
        output=old;
        Check(registry.Select(owner,Address(live),live.data(),Pointer(descriptor),output)==Result::InvalidDescriptor && Empty(output),"outside/misaligned descriptor accepted");
    }
    const auto count=Word(live,164);Put(live,164,count+1);
    Check(registry.Select(owner,Address(live),live.data(),Descriptor(live),output)==Result::WrongHeader,"changed live layout enlarged owned bound");Put(live,164,count);
    Bytes moved(live.size());std::memcpy(moved.data(),live.data(),live.size());
    Check(Address(moved)!=Address(live),"move fixture reused live allocation");
    Check(registry.Select(owner,Address(moved),live.data(),Descriptor(live),output)==Result::WrongHeader,"old header accepted after content move");
    Check(registry.Sample(old,Address(moved),Animation(moved,old),0,0,true)==Result::WrongHeader,"old base ticket accepted after move");
    std::fill(live.begin(),live.end(),std::byte{0xdd});Bytes{}.swap(live);
    const auto current=Select(registry,owner,moved);
    Check(current.generation==old.generation,"same content move created new content generation");
    Check(registry.Sample(current,Address(moved),Animation(moved,current),2,0,true)==Result::Ready,"new selection rejected memcpy-preserved moved content");
    Check(registry.Sample(current,0,Pointer(1),0,0,true)==Result::WrongHeader,"cleared live cache sampled stale storage");
    std::cout<<"PASS pointer mismatch precedes dereference, descriptor bounds, actual memcpy relocation and re-selection\n";
}

void AliasAndFrames()
{
    for(int frames:{1,3})
    {
        const auto file=Packed(true,frames);auto live=Prefix(file);Registry registry;constexpr std::uintptr_t owner=0x401;
        const auto original=Publish(registry,owner,file,live);
        const auto descriptors=Word(live,168);
        std::array<std::byte,176> old99{},old100{};
        std::memcpy(old99.data(),live.data()+descriptors+99*176,176);std::memcpy(old100.data(),live.data()+descriptors+100*176,176);
        std::memcpy(live.data()+descriptors+99*176,live.data()+descriptors+101*176,176);
        std::memcpy(live.data()+descriptors+100*176,live.data()+descriptors+102*176,176);
        auto previous=Select(registry,owner,live,99),current=Select(registry,owner,live,100);
        Check(previous.selection.animation!=current.selection.animation,"distinct aliases collided");
        std::memcpy(live.data()+descriptors+100*176,live.data()+descriptors+101*176,176);
        auto changed=Select(registry,owner,live,100);Check(changed.selection.animation==previous.selection.animation,"alias selection used stale slot cache");
        std::memcpy(live.data()+descriptors+99*176,old99.data(),176);std::memcpy(live.data()+descriptors+100*176,old100.data(),176);
        Select(registry,owner,live,99);Select(registry,owner,live,100);
        for(std::size_t offset:{std::size_t(56),std::size_t(120),std::size_t(124),std::size_t(156)})
        {
            const auto old=Word(live,descriptors+offset);Put(live,descriptors+offset,old+1);Ticket cleared=original;
            Check(registry.Select(owner,Address(live),live.data(),Descriptor(live),cleared)==Result::UnknownTrack && Empty(cleared),"shape mutation accepted");Put(live,descriptors+offset,old);
        }
        const auto ticket=Select(registry,owner,live);
        const auto stride=std::size_t(ticket.selection.bones)*12;
        for(int field=0;field<3;++field)
        {
            auto missingShape=ticket;
            if(field==0)missingShape.selection.frames=0;
            if(field==1)missingShape.selection.bones=0;
            if(field==2)missingShape.selection.blends=0;
            Check(registry.Sample(missingShape,Address(live),Animation(live,ticket),0,0,true)==Result::InvalidTrack,"zero ticket dimension accepted");
        }
        for(std::size_t offset:{std::size_t(0),stride})
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket,offset),float(frames-1),0,true)==Result::Ready,"valid blend start rejected");
        for(std::size_t offset:{std::size_t(0),std::size_t(12),stride,stride+12})
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket,offset),0,0,false)==Result::Ready,"valid per-bone anim offset rejected");
        for(std::size_t offset:{std::size_t(1),std::size_t(12),stride*2})
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket,offset),0,0,true)==Result::InvalidAnimation,"invalid whole-blend address accepted");
        Check(registry.Sample(ticket,Address(live),Pointer(1),0,0,true)==Result::InvalidAnimation,"foreign panim accepted");
        Check(registry.Sample(ticket,Address(live),Animation(live,ticket,1),0,0,false)==Result::InvalidAnimation,"misaligned bone panim accepted");
        Check(registry.Sample(ticket,Address(live),Animation(live,ticket,stride*2),0,0,false)==Result::InvalidAnimation,"out-of-table bone panim accepted");
        for(float fraction:{-.01f,-.005f,-0.f,0.f})
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket),0,fraction,true)==Result::Ready,"allowed small negative interpolation rejected");
        for(float frame:{-1.f,.5f,float(frames),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket),frame,0,true)==Result::InvalidFrame,"invalid integral frame accepted");
        for(float fraction:{std::nextafter(-.01f,-1.f),1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket),0,fraction,true)==Result::InvalidFrame,"invalid fraction accepted");
        Check(registry.Sample(ticket,Address(live),Animation(live,ticket),float(frames-1),.001f,true)==Result::InvalidFrame,"positive last-frame fraction accepted");
        if(frames>1)
        {
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket),1,-.005f,true)==Result::InvalidFrame,"negative fraction after frame0 accepted");
            Check(registry.Sample(ticket,Address(live),Animation(live,ticket),1,.5f,true)==Result::Ready,"legal normalized fractional sample rejected");
        }
    }
    std::cout<<"PASS aliases99/100, selected shape, whole-blend/per-bone alignment and normalized frame/fraction\n";
}

void OrdinaryAndAllocations()
{
    auto ordinary=Model(true);auto live=Prefix(ordinary);auto prepared=Prepared::Read(ordinary);
    Check(!prepared.Tagged(),"ordinary model tagged");Registry registry;constexpr std::uintptr_t owner=0x501;
    const auto token=registry.BeginLoad(owner);registry.Allocating(owner,live.size());
    Check(registry.CompleteLoad(owner,token,std::move(prepared),Address(live),live.size()),"ordinary load registration failed");
    Ticket empty;
    Check(registry.Select(owner,Address(live),live.data(),Pointer(1),empty)==Result::Ordinary && Empty(empty),"ordinary model not explicit safe bypass");
    Check(registry.Sample(empty,Address(live),Pointer(1),0,0,true)==Result::MissingOwner,"ordinary empty ticket accepted as tagged sample");
    const auto tagged=Packed();auto active=Prefix(tagged);auto proof=Publish(registry,owner,tagged,active);
    const auto header=active.data();
    const auto descriptor=Descriptor(active),animation=Animation(active,proof);const auto base=Address(active);
    bool passed=true;allocation_probe::calls=0;allocation_probe::enabled=true;
    for(int i=0;i<10000;++i)
    {
        Ticket ticket;
        passed&=registry.Select(owner,base,header,descriptor,ticket)==Result::Ready;
        passed&=registry.Sample(ticket,base,animation,0,-.005f,true)==Result::Ready;
        passed&=registry.Sample(ticket,base,animation,2,.1f,true)==Result::InvalidFrame;
        passed&=registry.Select(0x999,0,Pointer(1),Pointer(1),ticket)==Result::MissingOwner;
    }
    allocation_probe::enabled=false;
    Check(passed,"query allocation loop semantics failed");Check(!allocation_probe::calls,"Select/Sample allocated C++ storage");
    std::cout<<"PASS ordinary registration and40000 Select/Sample calls with C++ allocations=0\n";
}
void FrameNormalization()
{
    struct Example{float raw;std::uint32_t frames;float integer,fraction;};
    const Example examples[]{
        {-100,1,-0.f,-.01f},{std::nextafter(-.01f,-1.f),1,-0.f,-.01f},
        {-.01f,1,-0.f,-.01f},{-.005f,1,-0.f,-.005f},{-0.f,1,-0.f,0},
        {0,1,0,0},{.1f,1,0,0},{1.5f,3,1,.5f},{2,3,2,0},
        {std::nextafter(2.f,3.f),3,0,0},{std::numeric_limits<float>::max(),3,0,0},
        {-std::numeric_limits<float>::max(),3,-0.f,-.01f},{65535,65536,65535,0}
    };
    for(const auto& example:examples)
    {
        float integer=-123,fraction=-456;
        Check(csol::ClientSequenceFrame(example.raw,example.frames,integer,fraction),"finite normalization rejected");
        Check(integer==example.integer && fraction==example.fraction,"normalization differs from pinned frame contract");
    }
    for(float raw:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
    {float integer{},fraction{};Check(!csol::ClientSequenceFrame(raw,3,integer,fraction),"nonfinite raw frame normalized");}
    for(std::uint32_t frames:{0u,65537u,std::numeric_limits<std::uint32_t>::max()})
    {float integer{},fraction{};Check(!csol::ClientSequenceFrame(0,frames,integer,fraction),"invalid frame count normalized");}
    std::cout<<"PASS ClientSequenceFrame original negative threshold, truncation, exact-last/over-last and nonfinite boundaries\n";
}
void ModelStates()
{
    Registry registry;constexpr std::uintptr_t owner=0x601;
    Check(registry.ModelState(owner,0,Pointer(1))==Result::MissingOwner,"missing owner dereferenced header");
    for(bool tagged:{false,true})
    {
        const auto file=tagged?Packed():Model(true);auto live=Prefix(file);
        auto prepared=Prepared::Read(file);const auto token=registry.BeginLoad(owner);
        Check(registry.ModelState(owner,Address(live),live.data())==Result::MissingOwner,"pending ModelState ready");
        registry.Allocating(owner,live.size());
        Check(registry.CompleteLoad(owner,token,std::move(prepared),Address(live),live.size()),"ModelState fixture publication failed");
        Check(registry.ModelState(owner,Address(live),live.data())==(tagged?Result::Ready:Result::Ordinary),"ModelState category differs");
        Check(registry.ModelState(owner,0,Pointer(1))==Result::WrongHeader,"zero liveBase passed ModelState");
        Check(registry.ModelState(owner,Address(live),Pointer(1))==Result::WrongHeader,"mismatched header not rejected before dereference");
        const auto count=Word(live,164);Put(live,164,count+1);
        Check(registry.ModelState(owner,Address(live),live.data())==Result::WrongHeader,"live header mismatch passed ModelState");Put(live,164,count);
        registry.Allocating(owner,live.size());
        Check(registry.ModelState(owner,Address(live),live.data())==Result::MissingOwner,"retired ModelState remained ready");
    }
    std::cout<<"PASS ModelState missing/pending/tagged/ordinary, mismatched/live0 before read and allocation retirement\n";
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==2 && std::string_view(argv[1])=="--modelstate")
        {
            ModelStates();std::cout<<"PASS modelstate-only checks="<<checks<<"; previous owner groups not rerun\n";return 0;
        }
        Check(argc==1,"usage: sequence-owners [--modelstate]");
        PreparationAndPublishing();RetirementAndNesting();LivePointersAndMovement();AliasAndFrames();OrdinaryAndAllocations();FrameNormalization();
        std::cout<<"PASS owner checks="<<checks<<"; independent ownership simulator, no engine hooks or live gameplay\n";return 0;
    }
    catch(const std::exception& error){allocation_probe::enabled=false;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
