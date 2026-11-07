// SPDX-License-Identifier: GPL-3.0-or-later
// Real ReHLDS types and exact Git/current function extracts, no replacement SDK.
#include "precompiled.h"
#include "../../native/engine/crc32_ieee.h"
#include "legacy-crc.inc"
#include "current-crc.inc"
#include <chrono>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock=std::chrono::steady_clock;
using Bytes=std::vector<unsigned char>;
constexpr std::array<std::uint32_t,8> seeds{0,1,0xffffffffu,0x12345678u,
    0x80000000u,0xdeadbeefu,0x55555555u,0xaaaaaaaau};
unsigned long long assertions{};

void Require(bool condition,const char* reason)
{
    ++assertions;
    if(!condition)throw std::runtime_error(reason);
}

// Deliberately independent of all lookup tables and generated slices.
__declspec(noinline) std::uint32_t Bitwise(std::uint32_t crc,const void* pointer,int length)
{
    const auto* bytes=static_cast<const unsigned char*>(pointer);
    for(int i=0;i<length;++i)
    {
        crc^=bytes[i];
        for(int bit=0;bit<8;++bit)
        {
            const bool carry=(crc&1)!=0;
            crc>>=1;
            if(carry)crc^=0xedb88320u;
        }
    }
    return crc;
}

__declspec(noinline) std::uint32_t Legacy(std::uint32_t crc,const void* bytes,int size)
{
    legacy_crc::CRC32_ProcessBuffer(&crc,const_cast<void*>(bytes),size);
    return crc;
}
__declspec(noinline) std::uint32_t Current(std::uint32_t crc,const void* bytes,int size)
{
    current_crc::CRC32_ProcessBuffer(&crc,const_cast<void*>(bytes),size);
    return crc;
}

void Compare(std::uint32_t seed,const void* bytes,int size)
{
    const auto reference=Bitwise(seed,bytes,size);
    Require(Legacy(seed,bytes,size)==reference,"legacy differs from bitwise oracle");
    Require(Current(seed,bytes,size)==reference,"current differs from bitwise oracle");
}

std::uint32_t Next(std::uint32_t& state)
{state^=state<<13;state^=state>>17;state^=state<<5;return state;}

void Fill(Bytes& bytes)
{
    std::uint32_t state=0x613730ed;
    for(auto& value:bytes)value=static_cast<unsigned char>(Next(state));
}

void Functional()
{
    static_assert(sizeof(CRC32_t)==4 && sizeof(void*)==4);
    auto before=assertions;
    std::uint32_t oldState{},newState{};
    legacy_crc::CRC32_Init(&oldState);current_crc::CRC32_Init(&newState);
    Require(oldState==0xffffffffu && newState==oldState,"initial state");
    const char known[]="123456789";
    Require(legacy_crc::CRC32_Final(Legacy(oldState,known,9))==0xcbf43926u,"legacy known vector");
    Require(current_crc::CRC32_Final(Current(newState,known,9))==0xcbf43926u,"current IEEE known vector");
    Require(current_crc::CRC32_Final(Current(newState,known,0))==0,"empty known vector");
    for(unsigned i=0;i<256;++i)
        Require(goldcraft::crc32_ieee::tables[0][i]==legacy_crc::pulCRCTable[i],"legacy table identity");
    Bytes bytes(4*1024*1024+64);Fill(bytes);
    for(unsigned alignment=0;alignment<16;++alignment)
        for(int length=0;length<=1024;++length)
            for(auto seed:seeds)Compare(seed,bytes.data()+alignment,length);
    const std::array largeLengths{1025,2047,2048,2049,4095,4096,4097,65535,65536,65537,1048575,1048576,4194304};
    for(auto length:largeLengths)
        for(unsigned alignment:{0u,1u,7u,31u})
            for(auto seed:seeds)Compare(seed,bytes.data()+alignment,length);
    std::uint32_t random=0xbeef8901;
    for(unsigned i=0;i<2048;++i)
    {
        const auto seed=Next(random),alignment=Next(random)%64;
        Compare(seed,bytes.data()+alignment,int(Next(random)%4097));
    }
    for(auto seed:seeds)
    {
        oldState=newState=seed;
        for(unsigned i=0;i<1024;++i)
        {
            legacy_crc::CRC32_ProcessByte(&oldState,bytes[i]);
            current_crc::CRC32_ProcessByte(&newState,bytes[i]);
            Require(oldState==newState,"single byte state");
        }
        Require(newState==Current(seed,bytes.data(),1024),"byte and buffer equivalence");
        const auto whole=Bitwise(seed,bytes.data(),1024);
        for(unsigned split=0;split<=1024;++split)
        {
            oldState=Legacy(Legacy(seed,bytes.data(),int(split)),bytes.data()+split,int(1024-split));
            newState=Current(Current(seed,bytes.data(),int(split)),bytes.data()+split,int(1024-split));
            Require(oldState==whole && newState==whole,"all two-piece splits");
        }
        oldState=newState=seed;
        unsigned at=0;
        while(at<65537)
        {
            const unsigned count=std::min<unsigned>(65537-at,1+Next(random)%701);
            oldState=Legacy(oldState,bytes.data()+at,int(count));
            newState=Current(newState,bytes.data()+at,int(count));
            oldState=Legacy(oldState,nullptr,-1);newState=Current(newState,nullptr,INT_MIN);
            at+=count;
        }
        Require(oldState==Bitwise(seed,bytes.data(),65537) && newState==oldState,"multiple continuation calls");
        Require(legacy_crc::CRC32_Final(oldState)==current_crc::CRC32_Final(newState),"finalization equivalence");
    }
    // The input may alias the output word. Both actual wrappers must read the
    // whole original span before publishing their final state.
    for(int length=0;length<=32;++length)
    {
        struct alignas(8) Aliased{CRC32_t state;std::array<unsigned char,28> tail;};
        static_assert(sizeof(Aliased)==32);
        Aliased original{},candidate{};
        std::memcpy(&original,bytes.data(),sizeof(original));candidate=original;
        const auto expected=Bitwise(original.state,&original,length);
        legacy_crc::CRC32_ProcessBuffer(&original.state,&original,length);
        current_crc::CRC32_ProcessBuffer(&candidate.state,&candidate,length);
        Require(original.state==expected && candidate.state==expected,"input aliases state word");
        Require(!std::memcmp(&original,&candidate,sizeof(original)),"alias output bytes");
    }
    const auto unchanged=Current(0xffffffffu,bytes.data(),1024);
    for(unsigned at=0;at<1024;++at)
    {
        bytes[at]^=1;
        const auto oldValue=Legacy(0xffffffffu,bytes.data(),1024),newValue=Current(0xffffffffu,bytes.data(),1024);
        Require(oldValue==newValue && newValue!=unchanged,"single-bit change must be detected");
        bytes[at]^=1;
    }
    std::printf("checks\tfunctional\t%llu\n",assertions-before);
}

void ProtectedBoundaries()
{
    const auto before=assertions;
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const std::size_t page=info.dwPageSize,readable=page*3;
    auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*5,MEM_RESERVE,PAGE_NOACCESS));
    if(!base)throw std::bad_alloc();
    struct Release{void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release{base};
    auto* start=static_cast<unsigned char*>(VirtualAlloc(base+page,readable,MEM_COMMIT,PAGE_READWRITE));
    if(start!=base+page)throw std::bad_alloc();
    Bytes content(readable);Fill(content);std::memcpy(start,content.data(),readable);
    DWORD oldProtect{};
    Require(VirtualProtect(start,readable,PAGE_READONLY,&oldProtect)!=0,"read-only input protection");
    std::vector<int> lengths;
    for(int length=0;length<=512;++length)lengths.push_back(length);
    for(int length:{1023,1024,1025,4095,4096,4097,8191,8192,8193})lengths.push_back(length);
    for(auto length:lengths)for(auto seed:seeds)
    {
        Compare(seed,start,length); // Exact first readable address.
        Compare(seed,start+readable-length,length); // Exact last byte; next page inaccessible.
    }
    for(const void* pointer:std::array<const void*,4>{nullptr,base,start+readable,
            reinterpret_cast<const void*>(std::uintptr_t(1))})
        for(int length:{INT_MIN,INT_MIN+1,-4096,-8,-1,0})
            for(auto seed:seeds)Compare(seed,pointer,length);
    Require(!std::memcmp(start,content.data(),readable),"CRC must not change input");
    std::printf("checks\tprotected\t%llu\n",assertions-before);
}

void Models(const char* listPath,int rounds)
{
    const auto before=assertions;
    std::ifstream list(std::filesystem::u8path(listPath));
    if(!list)throw std::runtime_error("cannot open model list");
    std::string path;unsigned index=0;
    while(std::getline(list,path))
    {
        if(!path.empty() && path.back()=='\r')path.pop_back();
        if(path.empty())throw std::runtime_error("empty model path");
        std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
        if(!file)throw std::runtime_error("cannot open model");
        const auto size=file.tellg();
        if(size<244 || size>128*1024*1024)throw std::runtime_error("invalid model size");
        Bytes bytes(static_cast<std::size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)))
            throw std::runtime_error("short model read");
        file.close();
        std::printf("model\t%u\t%zu\n",index,bytes.size());
        for(int round=0;round<rounds;++round)
        {
            const auto seed=seeds[std::size_t(round)%seeds.size()];
            std::uint32_t oldValue{},newValue{};double oldTime{},newTime{};
            const auto oldRun=[&]{const auto begin=Clock::now();oldValue=Legacy(seed,bytes.data(),int(bytes.size()));
                oldTime=std::chrono::duration<double>(Clock::now()-begin).count();};
            const auto newRun=[&]{const auto begin=Clock::now();newValue=Current(seed,bytes.data(),int(bytes.size()));
                newTime=std::chrono::duration<double>(Clock::now()-begin).count();};
            if((index+round)&1){newRun();oldRun();}else{oldRun();newRun();}
            Require(oldValue==newValue,"full real model CRC mismatch");
            std::printf("sample\t%u\t%d\t%08x\t%08x\t%.9f\t%.9f\n",
                index,round,seed,newValue,oldTime,newTime);
        }
        const auto baseline=Current(0xffffffffu,bytes.data(),int(bytes.size()));
        for(const std::size_t at:{std::size_t(0),bytes.size()/2,bytes.size()-1})
        {
            bytes[at]^=1;
            const auto oldValue=Legacy(0xffffffffu,bytes.data(),int(bytes.size()));
            const auto newValue=Current(0xffffffffu,bytes.data(),int(bytes.size()));
            Require(oldValue==newValue && newValue!=baseline,"changed real model not detected");
            bytes[at]^=1;
        }
        std::printf("mutations\t%u\t3\n",index);
        ++index;
    }
    Require(index!=0,"empty model set");
    std::printf("checks\tmodels\t%llu\n",assertions-before);
    std::printf("complete\t%u\t%d\t%llu\n",index,rounds,assertions);
}
}

int main(int argc,char** argv)
{
    try
    {
        if(argc!=3)throw std::runtime_error("expected model list and rounds");
        const int rounds=std::stoi(argv[2]);
        if(rounds<1 || rounds>8)throw std::runtime_error("rounds outside 1..8");
        Functional();ProtectedBoundaries();Models(argv[1],rounds);
        return 0;
    }
    catch(const std::exception& error)
    {
        std::fprintf(stderr,"CRC test failed after %llu assertions: %s\n",assertions,error.what());
        return 1;
    }
}
