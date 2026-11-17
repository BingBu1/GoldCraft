// SPDX-License-Identifier: GPL-3.0-or-later
// Compare production validation with a fixed former validator and actual files.
#include "native/csol/sequence_safety.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
namespace csol::sequence_safety_detail {
#include "former-animation.inc"
}
namespace {
using Bytes=std::vector<std::byte>;
using Clock=std::chrono::steady_clock;
std::uint64_t comparisons{};
void Compare(const Bytes& bytes,unsigned bones,unsigned blends,unsigned frames)
{
    const auto reference=csol::sequence_safety_detail::FormerAnimation(bytes,bones,blends,frames);
    const auto candidate=csol::sequence_safety_detail::Animation(bytes,bones,blends,frames);
    if(reference!=candidate)throw std::runtime_error("RLE result differs");
    ++comparisons;
}
void Boundaries()
{
    const auto check=[](Bytes bytes,unsigned bones,unsigned blends,unsigned frames,bool expected)
    {
        const auto former=csol::sequence_safety_detail::FormerAnimation(bytes,bones,blends,frames);
        const auto current=csol::sequence_safety_detail::Animation(bytes,bones,blends,frames);
        if(former!=expected || current!=expected)throw std::runtime_error("explicit boundary differs");
        ++comparisons;
    };
    const auto offset=[](Bytes& bytes,unsigned axis,unsigned value)
    {const auto shortValue=std::uint16_t(value);std::memcpy(bytes.data()+axis*2,&shortValue,2);};
    check(Bytes(16),1,1,1,true);check(Bytes(15),1,1,1,false);
    for(unsigned start: {12u,13u,62u,63u,64u,65u,126u,127u,128u,129u})
    {
        Bytes run(start+8);offset(run,0,start);
        run[start]=run[start+1]=std::byte{1};
        check(run,1,1,1,true);
        auto shared=run;offset(shared,1,start);check(shared,1,1,1,true);
        auto secondByte=run;secondByte.resize(start+10);
        secondByte[start+2]=std::byte{1};offset(secondByte,1,start+1);
        check(secondByte,1,1,1,false);
        auto sample=run;sample.resize(start+10);sample[start+2]=sample[start+3]=std::byte{1};
        offset(sample,1,start+2);check(sample,1,1,1,false);
        auto shortEnd=run;shortEnd.pop_back();check(shortEnd,1,1,1,false);
        auto invalidTotal=run;invalidTotal[start]=std::byte{2};check(invalidTotal,1,1,1,false);
        auto zero=run;zero[start]=std::byte{};check(zero,1,1,1,false);
        check(run,1,1,2,false);
    }
    Bytes reverse(80);offset(reverse,0,64);offset(reverse,1,63);
    reverse[63]=reverse[64]=reverse[65]=std::byte{1};check(reverse,1,1,1,false);
    std::printf("boundaries\t%llu\n",static_cast<unsigned long long>(comparisons));
    std::fflush(stdout);
}
void Differential()
{
    std::mt19937 rng(0x20261117);
    for(unsigned iteration=0;iteration<30000;++iteration)
    {
        const unsigned bones=1+rng()%8,blends=1+rng()%4,frames=1+rng()%512;
        const auto tables=bones*blends*12;
        Bytes bytes(tables+4);
        std::vector<unsigned> tracks;
        for(unsigned table=0;table<tables;table+=12)for(unsigned axis=0;axis<6;++axis)
        {
            if(rng()%3==0)continue;
            unsigned start{};
            if(!tracks.empty() && rng()%2==0)start=tracks[rng()%tracks.size()];
            else
            {
                start=unsigned(bytes.size());tracks.push_back(start);
                int remaining=int(frames);
                while(remaining>0)
                {
                    const unsigned total=1+rng()%255,valid=1+rng()%total;
                    bytes.push_back(std::byte(valid));bytes.push_back(std::byte(total));
                    for(unsigned value=0;value<valid*2;++value)bytes.push_back(std::byte(rng()));
                    remaining-=int(total);
                }
                bytes.resize(bytes.size()+4+rng()%2);
            }
            if(start-table>65535)continue;
            const auto offset=std::uint16_t(start-table);
            std::memcpy(bytes.data()+table+axis*2,&offset,2);
        }
        Compare(bytes,bones,blends,frames);
        for(unsigned change=0;change<8;++change)
        {
            auto mutated=bytes;
            const auto at=rng()%mutated.size();
            mutated[at]=std::byte(rng());
            if(change==0)mutated.resize(at);
            Compare(mutated,bones,blends,change==1?1+rng()%65536:frames);
        }
    }
    std::printf("differential\t%llu\n",static_cast<unsigned long long>(comparisons));
    std::fflush(stdout);
}
double Bench(std::span<const std::byte> bytes,bool candidate)
{
    using namespace csol::sequence_safety_detail;
    const auto descriptors=Word(bytes,168),footer=descriptors-32,records=Word(bytes,footer+20);
    const auto bones=Word(bytes,140),count=Word(bytes,164);
    const auto started=Clock::now();
    for(unsigned i=0;i<count;++i)
    {
        const auto at=records+i*32;
        const auto block=bytes.subspan(Word(bytes,at),Word(bytes,at+4));
        const auto frames=Word(bytes,at+8),blends=Word(bytes,at+12);
        const bool valid=candidate?Animation(block,bones,blends,frames):FormerAnimation(block,bones,blends,frames);
        if(valid!=(Word(bytes,at+24)==1))throw std::runtime_error("actual model status differs");
    }
    return std::chrono::duration<double>(Clock::now()-started).count();
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("expected actual model list");
        Boundaries();
        Differential();
        std::ifstream list(std::filesystem::u8path(argv[1]));
        if(!list)throw std::runtime_error("cannot open model list");
        std::string path;unsigned models{};
        std::array<double,3> originals{},candidates{};
        while(std::getline(list,path))
        {
            if(!path.empty() && path.back()=='\r')path.pop_back();
            std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
            if(!file)throw std::runtime_error("cannot open actual model");
            const auto size=file.tellg();if(size<244 || size>128*1024*1024)throw std::runtime_error("model size");
            Bytes bytes(static_cast<std::size_t>(size));file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("short read");
            csol::SequenceSafetyView view;
            if(csol::ReadSequenceSafety(bytes,view,csol::SequenceReadPolicy::AllowUnsupportedTracks)!=csol::SequenceSafetyStatus::Ready)
                throw std::runtime_error("actual model is not validated tagged data");
            for(unsigned round=0;round<3;++round)
            {
                const auto original=[&]{originals[round]+=Bench(bytes,false);};
                const auto candidate=[&]{candidates[round]+=Bench(bytes,true);};
                if((models+round)&1){candidate();original();}else{original();candidate();}
            }
            ++models;
        }
        if(!models)throw std::runtime_error("empty actual model set");
        for(unsigned round=0;round<3;++round)
            std::printf("round\t%u\t%.9f\t%.9f\n",round,originals[round],candidates[round]);
        std::printf("complete\t%u\n",models);
        return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
