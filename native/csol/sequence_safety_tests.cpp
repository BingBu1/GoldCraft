// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequence_safety.hpp"
#include "sequences.hpp"
#include "model_codec.hpp"
#include "scenario/monster_sequences.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <malloc.h>

namespace allocation_probe
{
bool enabled{};
std::size_t calls{};
void Count() { if(enabled)++calls; }
}
void* operator new(std::size_t size)
{
    allocation_probe::Count();
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }
void operator delete[](void* value,std::size_t) noexcept { std::free(value); }
void* operator new(std::size_t size,std::align_val_t alignment)
{
    allocation_probe::Count();
    if(auto* value=_aligned_malloc(size?size:1,static_cast<std::size_t>(alignment)))return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size,std::align_val_t alignment) { return ::operator new(size,alignment); }
void operator delete(void* value,std::align_val_t) noexcept { _aligned_free(value); }
void operator delete[](void* value,std::align_val_t) noexcept { _aligned_free(value); }
void operator delete(void* value,std::size_t,std::align_val_t) noexcept { _aligned_free(value); }
void operator delete[](void* value,std::size_t,std::align_val_t) noexcept { _aligned_free(value); }

namespace
{
using Bytes=std::vector<std::byte>;
using csol::SequenceCheck;
using csol::SequenceSafetyStatus;
using csol::SequenceSafetyView;
using csol::SequenceSelection;
std::size_t checks{};
void Require(bool condition,const char* message)
{
    ++checks;
    if(!condition)throw std::runtime_error(message);
}
template<class T> T Read(std::span<const std::byte> data,std::size_t at)
{
    Require(at<=data.size() && sizeof(T)<=data.size()-at,"test read outside fixture");
    T value;std::memcpy(&value,data.data()+at,sizeof(value));return value;
}
template<class T> void Put(Bytes& data,std::size_t at,T value)
{
    Require(at<=data.size() && sizeof(T)<=data.size()-at,"test write outside fixture");
    std::memcpy(data.data()+at,&value,sizeof(value));
}
std::uint32_t Word(std::span<const std::byte> data,std::size_t at) { return Read<std::uint32_t>(data,at); }
Bytes Animation(int bones,int blends,int frames)
{
    const std::size_t tables=std::size_t(bones)*blends*12;
    Bytes data(tables);
    for(int blend=0;blend<blends;++blend)for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
    {
        const std::size_t table=(std::size_t(blend)*bones+bone)*12,at=data.size();
        Put(data,table+axis*2,static_cast<std::uint16_t>(at-table));
        data.resize(at+(frames==1?4:8));
        data[at]=std::byte{1};data[at+1]=std::byte(frames==1?1:2);
        Put(data,at+2,static_cast<std::int16_t>(100+axis+bone+blend));
        if(frames!=1)
        {
            data[at+4]=data[at+5]=std::byte{1};
            Put(data,at+6,static_cast<std::int16_t>(-100-axis-bone-blend));
        }
    }
    return data;
}
Bytes Model(int count,int frames=3,int bones=2,int blends=2,bool texture=true)
{
    const auto animation=Animation(bones,blends,frames);
    const std::size_t textureIndex=244+std::size_t(bones)*112;
    const std::size_t descriptors=textureIndex+(texture?80:0),anim=descriptors+count*176;
    const std::size_t textureData=anim+animation.size();
    Bytes data(textureData+(texture?40:0));
    Put(data,0,0x54534449u);Put(data,4,10u);Put(data,72,static_cast<std::uint32_t>(data.size()));
    Put(data,140,bones);Put(data,144,244);Put(data,164,count);Put(data,168,static_cast<std::uint32_t>(descriptors));
    Put(data,180,texture?1:0);Put(data,184,static_cast<std::uint32_t>(texture?textureIndex:0));
    Put(data,188,static_cast<std::uint32_t>(texture?textureData:0));
    for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
        Put(data,244+std::size_t(bone)*112+88+axis*4,axis<3?.01f:.0003f);
    for(int i=0;i<count;++i)
    {
        const auto at=descriptors+std::size_t(i)*176;
        Put(data,at+56,frames);Put(data,at+120,blends);Put(data,at+124,static_cast<std::uint32_t>(anim));
    }
    std::copy(animation.begin(),animation.end(),data.begin()+anim);
    if(texture)
    {
        Put(data,textureIndex+76,static_cast<std::uint32_t>(textureData));
        for(std::size_t i=textureData;i<data.size();++i)data[i]=std::byte(i&255);
    }
    return data;
}
Bytes Seq(int count,int frames=3,int bones=2,int blends=2)
{
    const auto animation=Animation(bones,blends,frames);
    const auto descriptors=8+std::size_t(count)*animation.size();
    Bytes data(descriptors+count*176);
    Put(data,0,count);Put(data,4,static_cast<std::uint32_t>(descriptors));
    for(int i=0;i<count;++i)
    {
        std::copy(animation.begin(),animation.end(),data.begin()+8+i*animation.size());
        const auto at=descriptors+std::size_t(i)*176;
        Put(data,at+56,frames);Put(data,at+120,blends);
        Put(data,at+124,static_cast<std::uint32_t>(10000+i*animation.size()));
    }
    return data;
}
SequenceSelection Select(std::span<const std::byte> bytes,std::size_t index)
{
    const auto descriptor=Word(bytes,168)+index*176;
    return {Word(bytes,descriptor+124),Word(bytes,descriptor+56),Word(bytes,descriptor+120),
        Word(bytes,descriptor+156),Word(bytes,140)};
}
std::size_t Footer(const Bytes& bytes) { return Word(bytes,168)-csol::SequenceSafetyFooterBytes; }
std::size_t Records(const Bytes& bytes) { return Word(bytes,Footer(bytes)+20); }
void Ready(const Bytes& bytes,SequenceSafetyView& view)
{
    Require(csol::ReadSequenceSafety(bytes,view)==SequenceSafetyStatus::Ready,"valid generated metadata rejected");
}
void Bad(const Bytes& bytes,const char* label)
{
    SequenceSafetyView view;
    if(csol::ReadSequenceSafety(bytes,view)!=SequenceSafetyStatus::Invalid)
        throw std::runtime_error(std::string("invalid metadata accepted: ")+label);
    ++checks;
}
template<class Mutation> void Corrupt(const Bytes& valid,const char* label,Mutation mutation)
{
    auto bytes=valid;mutation(bytes);Bad(bytes,label);
}

void WriterAndFrames()
{
    for(bool texture:{false,true})for(int nativeCount:{1,198})for(int frames:{1,3})
    {
        const auto input=Model(nativeCount,frames,2,2,texture);
        const auto bytes=csol::AppendSequences(input,{Seq(2,frames)});
        SequenceSafetyView view;Ready(bytes,view);
        const auto count=Word(bytes,164),descriptors=Word(bytes,168);
        const auto footer=Footer(bytes),records=Records(bytes);
        Require(count==unsigned(nativeCount+2),"writer lost native or appended descriptors");
        Require(records+count*csol::SequenceSafetyRecordBytes==footer,"records are not contiguous before footer");
        Require(footer+csol::SequenceSafetyFooterBytes==descriptors,"footer moved away from descriptors");
        Require(Word(bytes,72)==bytes.size(),"writer header length mismatch");
        for(std::uint32_t i=0;i<count;++i)
        {
            const auto selected=Select(bytes,i);
            const auto at=records+i*csol::SequenceSafetyRecordBytes;
            Require(Word(bytes,at)==selected.animation && Word(bytes,at+24)==1,"writer omitted valid track record");
            Require(view.Check(selected,0)==SequenceCheck::Ready,"first frame rejected");
            Require(view.Check(selected,-0.f)==SequenceCheck::Ready,"negative zero frame rejected");
            Require(view.Check(selected,float(frames-1))==SequenceCheck::Ready,"exact final frame rejected");
            if(frames>1)Require(view.Check(selected,float(frames)-1.5f)==SequenceCheck::Ready,"last fractional frame rejected");
            for(float invalid:{-1.f,-.0001f,float(frames)-.5f,float(frames),
                std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()})
                Require(view.Check(selected,invalid)==SequenceCheck::InvalidFrame,"invalid actual frame accepted");
        }
        if(texture)
        {
            const auto oldTexture=Word(input,188),newTexture=Word(bytes,188);
            Require(newTexture==descriptors+count*176,"metadata entered texture image region");
            Require(std::equal(input.begin()+oldTexture,input.end(),bytes.begin()+newTexture,bytes.end()),"texture payload changed");
            // Stock texture repacking retains the complete prefix, not necessarily header.length bytes.
            SequenceSafetyView retained;
            Require(csol::ReadSequenceSafety(std::span<const std::byte>(bytes).first(newTexture),retained)==SequenceSafetyStatus::Ready,
                "actual retained prefix rejected");
            Require(retained.Check(Select(bytes,count-1),float(frames-1))==SequenceCheck::Ready,"prefix view lost last track");
        }
    }
    std::cout<<"PASS writer native/SEQ, equal/requantized units, texture prefix and finite legal frames\n";
}

void Structure(const Bytes& valid)
{
    const auto footer=Footer(valid),records=Records(valid);
    const auto descriptor=Word(valid,168);
    for(std::size_t at:{std::size_t(0),std::size_t(4),footer,footer+4,footer+8,footer+24,footer+28})
        Corrupt(valid,"tag/version/stride/reserved",[&](auto& b){Put(b,at,0xffffffffu);});
    for(std::uint32_t value:{0u,2049u,0xffffffffu})
        Corrupt(valid,"record count",[&](auto& b){Put(b,footer+12,value);});
    for(std::uint32_t value:{0u,257u,0xffffffffu})
        Corrupt(valid,"bones",[&](auto& b){Put(b,footer+16,value);});
    Corrupt(valid,"header count mismatch",[&](auto& b){Put(b,164,Word(b,164)-1);});
    Corrupt(valid,"header bones mismatch",[&](auto& b){Put(b,140,Word(b,140)+1);});
    for(std::uint32_t value:{0u,243u,128u*1024*1024+1,0xffffffffu})
        Corrupt(valid,"invalid declared model length",[&](auto& b){Put(b,72,value);});
    Corrupt(valid,"declared length before required prefix",[&](auto& b){Put(b,72,Word(b,188)-1);});
    Corrupt(valid,"declared prefix before descriptor end",[&](auto& b){Put(b,188,descriptor+Word(b,164)*176-1);});
    Corrupt(valid,"complete descriptors but missing retained prefix",[&](auto& b)
    { const auto prefix=Word(b,188);b.resize(prefix);Put(b,188,prefix+1); });
    Corrupt(valid,"bone records outside prefix",[&](auto& b){Put(b,144,Word(b,188)-1);});
    Corrupt(valid,"bone records overlap header",[&](auto& b){Put(b,144,243u);});
    Corrupt(valid,"texture records outside prefix",[&](auto& b){Put(b,184,Word(b,188)-1);});
    Corrupt(valid,"texture records overlap header",[&](auto& b){Put(b,184,243u);});
    Corrupt(valid,"texture count out of supported range",[&](auto& b){Put(b,180,4097u);});
    for(std::uint32_t value:{0u,243u,static_cast<std::uint32_t>(records+1),0xffffffffu})
        Corrupt(valid,"record offset",[&](auto& b){Put(b,footer+20,value);});
    Corrupt(valid,"truncated final descriptor",[&](auto& b){b.resize(descriptor+Word(b,164)*176-1);});
    for(std::uint32_t value:{0u,243u,static_cast<std::uint32_t>(records),0xffffffffu})
        Corrupt(valid,"animation offset",[&](auto& b){Put(b,records,value);});
    for(std::uint32_t value:{0u,15u,0xffffffffu})
        Corrupt(valid,"animation byte length",[&](auto& b){Put(b,records+4,value);});
    Corrupt(valid,"animation reaches metadata",[&](auto& b){Put(b,records+4,static_cast<std::uint32_t>(records-Word(b,records)+1));});
    Corrupt(valid,"overlapping records",[&](auto& b){Put(b,records+32,Word(b,records));});
    Corrupt(valid,"reverse record order",[&](auto& b){Put(b,records,Word(b,records+32)+1);});
    for(std::uint32_t value:{0u,65537u,0xffffffffu})
        Corrupt(valid,"record frames",[&](auto& b){Put(b,records+8,value);});
    for(std::uint32_t value:{0u,17u,0xffffffffu})
        Corrupt(valid,"record blends",[&](auto& b){Put(b,records+12,value);});
    for(std::size_t at:{records+16,records+20,records+24,records+28})
        Corrupt(valid,"group/bones/status/reserved",[&](auto& b){Put(b,at,0xffffffffu);});
    Corrupt(valid,"unverified status",[&](auto& b){Put(b,records+24,0u);});
    for(std::size_t at:{descriptor+56,descriptor+120,descriptor+124,descriptor+156})
        Corrupt(valid,"descriptor differs from record",[&](auto& b){Put(b,at,Word(b,at)+1);});
    std::cout<<"PASS malformed footer, table, record ranges and descriptor shapes rejected\n";
}

void RleNotStatus(const Bytes& valid)
{
    const auto records=Records(valid);
    const auto anim=Word(valid,records);
    const auto tables=Select(valid,0).bones*Select(valid,0).blends*12;
    const auto first=anim+Read<std::uint16_t>(valid,anim);
    Corrupt(valid,"status1 total0",[&](auto& b){b[first+1]=std::byte{};});
    Corrupt(valid,"status1 valid0",[&](auto& b){b[first]=std::byte{};});
    Corrupt(valid,"status1 valid greater than total",[&](auto& b){b[first]=std::byte{3};});
    Corrupt(valid,"status1 channel offset inside tables",[&](auto& b){Put(b,anim,std::uint16_t{1});});
    Corrupt(valid,"status1 channel offset outside animation",[&](auto& b){Put(b,anim,std::uint16_t{65535});});
    for(std::size_t overlap:{std::size_t(2),std::size_t(3)})
        Corrupt(valid,"status1 sample/header overlap",[&](auto& b)
        {
            // Axis0 consumes first+2/3 as sample; axis1 treats the same bytes as its header.
            Put(b,anim+2,static_cast<std::uint16_t>(tables+overlap));
            b[first+overlap]=std::byte{1};b[first+overlap+1]=std::byte{3};
        });
    Corrupt(valid,"status1 omitted final lookahead",[&](auto& b){Put(b,records+4,Word(b,records+4)-4);});
    Corrupt(valid,"status1 omitted required sample",[&](auto& b){Put(b,records+4,tables+2);});
    // Compatible sharing is legal; the unused final lookahead can alias another header.
    auto shared=valid;Put(shared,anim+2,Read<std::uint16_t>(shared,anim));
    SequenceSafetyView sharedView;Ready(shared,sharedView);
    Require(sharedView.Check(Select(shared,0),2)==SequenceCheck::Ready,"compatible RLE sharing rejected");
    auto zero=valid;
    std::fill(zero.begin()+anim,zero.begin()+anim+tables,std::byte{});
    SequenceSafetyView zeroView;Ready(zero,zeroView);
    Require(zeroView.Check(Select(zero,0),2)==SequenceCheck::Ready,"all-zero channels rejected");
    std::cout<<"PASS status1 never substitutes for bounded RLE validation; compatible sharing remains legal\n";
}

void SelectionsAndAliases(Bytes bytes)
{
    SequenceSafetyView view;Ready(bytes,view);
    const auto expectedA=Select(bytes,101),expectedB=Select(bytes,102);
    const auto descriptors=Word(bytes,168);
    std::array<std::byte,176> old99{},old100{};
    std::memcpy(old99.data(),bytes.data()+descriptors+99*176,176);
    std::memcpy(old100.data(),bytes.data()+descriptors+100*176,176);
    std::memcpy(bytes.data()+descriptors+99*176,bytes.data()+descriptors+102*176,176);
    std::memcpy(bytes.data()+descriptors+100*176,bytes.data()+descriptors+101*176,176);
    Require(Select(bytes,99).animation==expectedB.animation && Select(bytes,100).animation==expectedA.animation,"alias fixture not copied");
    Require(view.Check(Select(bytes,99),2)==SequenceCheck::Ready,"previous descriptor alias rejected");
    Require(view.Check(Select(bytes,100),1.5f)==SequenceCheck::Ready,"current descriptor alias rejected");
    std::memcpy(bytes.data()+descriptors+100*176,bytes.data()+descriptors+102*176,176);
    Require(view.Check(Select(bytes,100),2)==SequenceCheck::Ready,"alias reused stale slot identity");
    std::memcpy(bytes.data()+descriptors+99*176,old99.data(),176);
    std::memcpy(bytes.data()+descriptors+100*176,old100.data(),176);
    Require(view.Check(Select(bytes,99),0)==SequenceCheck::Ready && view.Check(Select(bytes,100),0)==SequenceCheck::Ready,"restored aliases rejected");
    for(int field=0;field<5;++field)
    {
        auto selected=expectedA;
        if(field==0)++selected.animation;
        if(field==1)++selected.frames;
        if(field==2)++selected.blends;
        if(field==3)++selected.group;
        if(field==4)++selected.bones;
        Require(view.Check(selected,0)==SequenceCheck::UnknownTrack,"selection shape mismatch accepted");
    }
    for(std::uint32_t animation:{0u,0xffffffffu,static_cast<std::uint32_t>(Records(bytes))})
    {
        auto selected=expectedA;selected.animation=animation;
        Require(view.Check(selected,0)==SequenceCheck::UnknownTrack,"unknown animation accepted");
    }
    std::cout<<"PASS actual descriptor fields, copied slots99/100 and restore; no slot-based cache identity\n";
}

void FailedReadClearsView(const Bytes& bytes)
{
    const auto selected=Select(bytes,0);
    SequenceSafetyView view;Ready(bytes,view);
    Require(csol::ReadSequenceSafety({},view)==SequenceSafetyStatus::Invalid,"short header not invalid");
    Require(view.Check(selected,0)==SequenceCheck::UnknownTrack,"failed parse retained old valid view");
    Ready(bytes,view);
    const auto ordinary=Model(1);
    Require(csol::ReadSequenceSafety(ordinary,view)==SequenceSafetyStatus::Absent,"ordinary unmarked model not absent");
    Require(view.Check(selected,0)==SequenceCheck::UnknownTrack,"absent metadata retained old valid view");
    Ready(bytes,view);
    auto bad=bytes;Put(bad,Footer(bad)+8,0xffffffffu);
    Require(csol::ReadSequenceSafety(bad,view)==SequenceSafetyStatus::Invalid,"bad version not invalid");
    Require(view.Check(selected,0)==SequenceCheck::UnknownTrack,"late failed parse retained old valid view");
    SequenceSafetyView fresh;
    Require(fresh.Check(selected,0)==SequenceCheck::UnknownTrack,"uninitialized view accepted track");
    std::cout<<"PASS Invalid/Absent reads clear prior view; uninitialized query rejects\n";
}

void QueriesAllocateNothing()
{
    const auto bytes=csol::AppendSequences(Model(2047,1,1,1,false),{Seq(1,1,1,1)});
    SequenceSafetyView view;Ready(bytes,view);
    const std::array selections{Select(bytes,0),Select(bytes,1024),Select(bytes,2047)};
    auto unknown=selections[0];unknown.animation=0xffffffffu;
    allocation_probe::calls=0;allocation_probe::enabled=true;
    bool success=true;
    for(int round=0;round<10000;++round)
    {
        for(const auto& selected:selections)success&=view.Check(selected,0)==SequenceCheck::Ready;
        success&=view.Check(unknown,0)==SequenceCheck::UnknownTrack;
        success&=view.Check(selections[0],std::numeric_limits<float>::quiet_NaN())==SequenceCheck::InvalidFrame;
    }
    allocation_probe::enabled=false;
    Require(success,"max-count query returned wrong result");
    Require(allocation_probe::calls==0,"Check allocated C++ storage");
    std::cout<<"PASS 50000 queries over2048 tracks: first/middle/last/unknown/nonfinite, C++ allocations=0\n";
}

Bytes ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    Require(bool(file),"real fixture open failed");
    const auto length=file.tellg();
    Require(length>=0,"real fixture size failed");
    Bytes bytes(static_cast<std::size_t>(length));
    file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    Require(bool(file),"real fixture read failed");return bytes;
}
void RealMetadata(const std::filesystem::path& common)
{
    std::size_t tracks{},queries{},realFiles{};
    for(const char* name:{"leet","urban"})
    {
        const auto path=std::string("models/player/")+name+"/"+name+".mdl";
        auto source=ReadFile(common/path);csol::DecodeStudioModel(source);++realFiles;
        const auto sequenceFiles=csol::SequenceFiles(path,[&](const auto& relative){return std::filesystem::exists(common/relative);});
        Require(sequenceFiles.size()==13,"unexpected real sequence source count");
        std::vector<Bytes> sequences;
        for(const auto& relative:sequenceFiles){sequences.push_back(ReadFile(common/relative));++realFiles;}
        const auto bytes=csol::AppendSequences(source,sequences);
        SequenceSafetyView full,prefix;Ready(bytes,full);
        const auto retained=Word(bytes,184)?Word(bytes,188):Word(bytes,72);
        Require(csol::ReadSequenceSafety(std::span<const std::byte>(bytes).first(retained),prefix)==SequenceSafetyStatus::Ready,
            "real retained prefix metadata rejected");
        const auto count=Word(bytes,164);Require(count==353,"unexpected real descriptor count");
        for(std::uint32_t i=0;i<count;++i)
        {
            const auto selected=Select(bytes,i);
            for(float frame:{0.f,float(selected.frames-1)})
            {
                Require(full.Check(selected,frame)==SequenceCheck::Ready,"real full-span selected track rejected");
                Require(prefix.Check(selected,frame)==SequenceCheck::Ready,"real prefix selected track rejected");
                queries+=2;
            }
            if(selected.frames>1)
            {
                Require(full.Check(selected,float(selected.frames)-1.5f)==SequenceCheck::Ready,"real fractional frame rejected");
                Require(prefix.Check(selected,float(selected.frames)-1.5f)==SequenceCheck::Ready,"real prefix fractional frame rejected");
                queries+=2;
            }
            ++tracks;
        }
        std::cout<<"PASS real metadata "<<name<<" seqFiles="<<sequenceFiles.size()<<" tracks="<<count
            <<" fullBytes="<<bytes.size()<<" retainedBytes="<<retained<<'\n';
        const auto unchanged=csol::AppendSequences(source,{});
        Require(unchanged==source,"empty SEQ path unexpectedly changed original decoded model");
        SequenceSafetyView absent;
        Require(csol::ReadSequenceSafety(unchanged,absent)==SequenceSafetyStatus::Absent,"empty SEQ early return unexpectedly gained metadata");
    }
    Require(tracks==706 && realFiles==28,"real metadata fixture totals differ");
    std::cout<<"PASS real metadata files="<<realFiles<<" tracks="<<tracks<<" full/prefix queries="<<queries
        <<"; empty SEQ preserves unmarked model and is outside sampled-safety acceptance\n";
}
void RejectedAppend(const Bytes& bytes,const std::vector<Bytes>& sequences,const char* message)
{
    try { csol::AppendSequences(bytes,sequences); }
    catch(const std::exception&) { ++checks;return; }
    throw std::runtime_error(message);
}
void PackedIdempotence(const std::filesystem::path& common)
{
    for(bool texture:{false,true})
    {
        const auto source=Model(2,3,2,2,texture);
        const std::vector<Bytes> sequences{Seq(2)};
        const auto packed=csol::AppendSequences(source,sequences);
        Require(csol::AppendSequences(packed,sequences)==packed,"packed plus same SEQ grew or changed");
        Require(csol::AppendSequences(packed,{})==packed,"packed plus empty SEQ changed");
        Require(csol::AppendSequences(source,{})==source,"ordinary empty SEQ path changed");
        auto marker=packed;Put(marker,Footer(marker),csol::SequenceSafetyMagic0^1u);
        RejectedAppend(marker,{},"empty SEQ bypassed malformed marker");
        RejectedAppend(marker,sequences,"same SEQ bypassed malformed marker");
        auto invalidRle=packed;
        const auto anim=Word(invalidRle,Records(invalidRle));
        const auto run=anim+Read<std::uint16_t>(invalidRle,anim);
        invalidRle[run+1]=std::byte{};
        RejectedAppend(invalidRle,{},"empty SEQ trusted packed status with invalid RLE");
        RejectedAppend(invalidRle,sequences,"same SEQ trusted packed status with invalid RLE");
    }
    const std::string path="models/player/leet/leet.mdl";
    auto source=ReadFile(common/path);csol::DecodeStudioModel(source);
    const auto files=csol::SequenceFiles(path,[&](const auto& relative){return std::filesystem::exists(common/relative);});
    Require(files.size()==13,"real packed source count differs");
    std::vector<Bytes> sequences;
    for(const auto& file:files)sequences.push_back(ReadFile(common/file));
    const auto packed=csol::AppendSequences(source,sequences);
    Require(Word(packed,164)==353,"real initial sequence count differs");
    Require(csol::AppendSequences(packed,sequences)==packed,"real same-SEQ packing changed bytes or numbering");
    Require(csol::AppendSequences(packed,{})==packed,"real empty-SEQ packing changed bytes or numbering");
    std::cout<<"PASS packed synthetic textured/untextured, same/empty SEQ byte identity, marker/RLE rejection before empty return\n";
    std::cout<<"PASS packed real leet files=14 tracks=353 bytes="<<packed.size()<<"; same/empty SEQ exact byte identity\n";
}

void SelectiveTracks(const std::filesystem::path& common)
{
    using csol::SequenceReadPolicy;
    constexpr auto allow=SequenceReadPolicy::AllowUnsupportedTracks;
    const auto invalidRead=[&](const Bytes& bytes,const char* message)
    {
        SequenceSafetyView view;
        Require(csol::ReadSequenceSafety(bytes,view,allow)==SequenceSafetyStatus::Invalid,message);
        Require(view.Check(Select(bytes,0),0)==SequenceCheck::UnknownTrack,"failed selective read retained a view");
    };
    const auto invalidAppend=[&](const Bytes& model,const Bytes& seq,const char* message)
    {
        try { csol::AppendSequences(model,{seq},allow); }
        catch(const std::runtime_error&) { ++checks;return; }
        throw std::runtime_error(message);
    };

    // A mixed synthetic model isolates the policy from real-file provenance.
    auto external=Seq(2);external[8]=std::byte{1};external[9]=std::byte{};
    const auto original=Model(2);
    RejectedAppend(original,{external},"default append allowed incompatible external RLE");
    const auto mixed=csol::AppendSequences(original,{external},allow);
    SequenceSafetyView view;
    Require(csol::ReadSequenceSafety(mixed,view)==SequenceSafetyStatus::Invalid,"default reader allowed unsupported track");
    Require(csol::ReadSequenceSafety(mixed,view,allow)==SequenceSafetyStatus::Ready,"selective reader rejected mixed model");
    Require(view.Check(Select(mixed,0),2)==SequenceCheck::Ready,"unsupported external disabled native track");
    Require(view.Check(Select(mixed,2),0)==SequenceCheck::InvalidTrack,"unsupported selected track was sampleable");
    Require(view.Check(Select(mixed,3),2)==SequenceCheck::Ready,"unsupported external disabled another external track");
    auto forged=mixed;Put(forged,Records(forged)+2*32+24,1u);
    invalidRead(forged,"forged ready status bypassed actual RLE");
    forged=mixed;Put(forged,Records(forged)+24,0u);
    invalidRead(forged,"forged unsupported status accepted a valid track");
    forged=mixed;Put(forged,Records(forged)+2*32+24,2u);
    invalidRead(forged,"unknown track status accepted");
    forged=mixed;Put(forged,Records(forged)+2*32+4,0xffffffffu);
    invalidRead(forged,"unsupported block outside prefix accepted");
    forged=mixed;forged.resize(Word(forged,188)-1);
    invalidRead(forged,"truncated retained prefix accepted");
    auto bad=external;Put(bad,Word(bad,4)+56,0u);
    invalidAppend(original,bad,"selective append accepted invalid dimensions");
    bad=external;bad.pop_back();invalidAppend(original,bad,"selective append accepted truncated descriptor trailer");
    bad=external;Put(bad,Word(bad,4)+156,1u);
    invalidAppend(original,bad,"selective append accepted unsupported external group layout");
    const auto ordinary=csol::AppendSequences(original,{},allow);
    Require(ordinary==original && csol::ReadSequenceSafety(ordinary,view,allow)==SequenceSafetyStatus::Absent,
        "ordinary model compatibility changed");
    std::cout<<"PASS selective policy: strict defaults, mixed tracks, forged status/RLE, bounds, malformed source, ordinary model\n";

    const std::string relative="models/player/zombi_host/zombi_host.mdl";
    auto source=ReadFile(common/relative);csol::DecodeStudioModel(source);
    const auto files=csol::SequenceFiles(relative,[&](const auto& name){return std::filesystem::exists(common/name);});
    Require(files.size()==1 && Word(source,164)==138 && Word(source,140)==40,"unexpected zombi_host source identity");
    std::vector<Bytes> sequences{ReadFile(common/files[0])};
    Require(Word(sequences[0],0)==36,"unexpected zombi_host external count");
    RejectedAppend(source,sequences,"default append silently relaxed real model");
    const auto bytes=csol::AppendSequences(source,sequences,allow);
    Require(Word(bytes,164)==174,"real selective append removed or renumbered tracks");
    Require(csol::ReadSequenceSafety(bytes,view)==SequenceSafetyStatus::Invalid,"strict real bounds policy accepted unsupported tracks");
    Require(csol::ReadSequenceSafety(bytes,view,allow)==SequenceSafetyStatus::Ready,"real selective read failed");
    const auto records=Records(bytes);
    const auto descriptors=Word(bytes,168),nativeDescriptors=Word(source,168);
    const auto retained=Word(bytes,184)?Word(bytes,188):Word(bytes,72);
    SequenceSafetyView prefix;
    Require(csol::ReadSequenceSafety(std::span<const std::byte>(bytes).first(retained),prefix,allow)==SequenceSafetyStatus::Ready,
        "selective retained prefix failed");
    for(std::size_t i=0;i<138;++i)
    {
        const auto selected=Select(bytes,i),old=Select(source,i);
        const auto size=Word(bytes,records+i*32+4);
        Require(size>=4 && Word(bytes,records+i*32+24)==1,"native ready record changed");
        Require(std::equal(bytes.begin()+selected.animation,bytes.begin()+selected.animation+size-4,
            source.begin()+old.animation),"native authored RLE bytes changed below fixed-unit threshold");
        for(std::size_t field=0;field<176;++field)
            if(field<124 || field>=128)
                Require(bytes[descriptors+i*176+field]==source[nativeDescriptors+i*176+field],"native descriptor changed");
        for(float frame:{0.f,float(selected.frames-1)})
            Require(view.Check(selected,frame)==SequenceCheck::Ready && prefix.Check(selected,frame)==SequenceCheck::Ready,
                "real native track frame rejected");
    }
    const auto& seq=sequences[0];const auto seqDescriptors=Word(seq,4);
    std::size_t cursor=8;
    for(std::size_t i=0;i<36;++i)
        cursor+=std::size_t(Word(seq,seqDescriptors+i*176+48))*76+std::size_t(Word(seq,seqDescriptors+i*176+60))*20;
    std::size_t preserved{};
    for(std::size_t i=0;i<36;++i)
    {
        const auto at=seqDescriptors+i*176,record=records+(138+i)*32;
        const auto next=i+1==36?seqDescriptors:cursor+Word(seq,at+176+124)-Word(seq,at+124);
        Require(next>=cursor && next<=seqDescriptors,"real source external spacing invalid");
        const auto selected=Select(bytes,138+i);
        const auto size=Word(bytes,record+4);
        Require(Word(bytes,record+24)==0 && size==next-cursor,"real unsupported record changed extent");
        Require(std::equal(seq.begin()+cursor,seq.begin()+next,bytes.begin()+selected.animation),"unsupported authored block changed");
        for(std::size_t field=0;field<176;++field)
            if((field<52 || field>=56) && (field<64 || field>=68) && (field<124 || field>=128))
                Require(bytes[descriptors+(138+i)*176+field]==seq[at+field],"external descriptor number or authored field changed");
        Require(view.Check(selected,0)==SequenceCheck::InvalidTrack && prefix.Check(selected,float(selected.frames-1))==SequenceCheck::InvalidTrack,
            "real unsupported selected track reached sampler");
        forged=bytes;Put(forged,record+24,1u);
        invalidRead(forged,"real unsupported RLE accepted with forged ready status");
        preserved+=size;cursor=next;
    }
    Require(cursor==seqDescriptors,"real external preservation missed payload bytes");
    csol::scenario::MonsterSequences resolved;
    const auto lookup=[](void* context,const char* label)
    {
        const auto& data=*static_cast<const Bytes*>(context);
        const auto count=Word(data,164),at=Word(data,168);
        for(std::size_t i=0;i<count;++i)
        {
            std::array<char,33> name{};std::memcpy(name.data(),data.data()+at+i*176,32);
            if(_stricmp(name.data(),label)==0)return int(i);
        }
        return -1;
    };
    Require(csol::scenario::ResolveMonsterSequences(lookup,const_cast<Bytes*>(&bytes),resolved),"real production label resolver failed");
    std::size_t missing{};
    for(const auto index:resolved.values)
    {
        if(index==-1)++missing;
        const auto effective=index==-1?0:index; // Existing monster setter fallback.
        Require(effective>=0 && effective<138,"first-wave label selected external or invalid sequence");
        Require(view.Check(Select(bytes,effective),0)==SequenceCheck::Ready,"first-wave effective selection rejected");
    }
    Require(resolved.values.size()==33 && missing==5,"first-wave label/fallback baseline changed");
    Require(view.Check(Select(bytes,1),0)==SequenceCheck::Ready,"initial spawn sequence rejected");
    Require(csol::AppendSequences(bytes,sequences,allow)==bytes && csol::AppendSequences(bytes,{},allow)==bytes,
        "selective packed append changed bytes");
    RejectedAppend(bytes,{},"strict empty append accepted mixed packed model");
    RejectedAppend(bytes,sequences,"strict same-SEQ append accepted mixed packed model");
    std::cout<<"PASS real zombi_host: native=138 ready, external=36 preserved/refused, rawExternalBytes="<<preserved
        <<", firstWaveSlots=33 ready (28 resolved + 5 existing sequence0 fallbacks), spawn1 ready, packed policy/idempotence\n";

    // One real character checks the modified API without repeating the previous
    // 706-track/sampler groups. Both policies must produce identical bytes.
    const std::string character="models/player/leet/leet.mdl";
    auto human=ReadFile(common/character);csol::DecodeStudioModel(human);
    std::vector<Bytes> humanSequences;
    for(const auto& path:csol::SequenceFiles(character,[&](const auto& name){return std::filesystem::exists(common/name);}))
        humanSequences.push_back(ReadFile(common/path));
    const auto strictHuman=csol::AppendSequences(human,humanSequences);
    const auto allowedHuman=csol::AppendSequences(human,humanSequences,allow);
    Require(strictHuman==allowedHuman && Word(strictHuman,164)==353,"all-ready character changed under selective policy");
    Ready(strictHuman,view);
    for(std::size_t i:{0u,197u,198u,352u})
    {
        const auto selected=Select(strictHuman,i);
        Require(view.Check(selected,float(selected.frames-1))==SequenceCheck::Ready,"representative character endpoint rejected");
    }
    std::cout<<"PASS real leet: strict/selective byte equality, 353 tracks, representative 0/197/198/352 endpoints\n";
}
}

int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc==3 && std::wstring_view(argv[1])==L"--selective")
        {
            SelectiveTracks(std::filesystem::path(argv[2]));
            std::cout<<"PASS selective-only checks="<<checks<<"; prior full synthetic/706-track/sampler groups not rerun\n";
            return 0;
        }
        if(argc==3 && std::wstring_view(argv[1])==L"--real")
        {
            RealMetadata(std::filesystem::path(argv[2]));
            std::cout<<"PASS real reader checks="<<checks<<"; no prior synthetic or sampler groups rerun\n";
            return 0;
        }
        if(argc==3 && std::wstring_view(argv[1])==L"--packed")
        {
            PackedIdempotence(std::filesystem::path(argv[2]));
            std::cout<<"PASS packed-only checks="<<checks<<"; no prior synthetic/real-reader/sampler groups rerun\n";
            return 0;
        }
        Require(argc==1,"usage: sequence-metadata [--real|--packed|--selective <common>]");
        WriterAndFrames();
        const auto bytes=csol::AppendSequences(Model(101),{Seq(2)});
        Structure(bytes);RleNotStatus(bytes);SelectionsAndAliases(bytes);FailedReadClearsView(bytes);
        QueriesAllocateNothing();
        std::cout<<"PASS metadata checks="<<checks<<"; no hooks, unload/reload or live gameplay tested\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        allocation_probe::enabled=false;
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
