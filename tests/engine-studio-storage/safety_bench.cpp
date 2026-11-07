// SPDX-License-Identifier: GPL-3.0-or-later
// Offline warm-buffer measurements of three real loading stages. No engine
// replacement, reduced validation, source snapshot or retained model cache.
#include "precompiled.h"
#include "../../native/csol/sequence_owners.hpp"
#include "../../native/engine/csol_studio_storage.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "animation-candidate.inc"
#include "storage-candidate.inc"

namespace
{
using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::byte>;

__declspec(noinline) void ReadSafety(std::span<const std::byte> bytes)
{
    csol::SequenceSafetyView view;
    if(csol::ReadSequenceSafety(bytes,view,csol::SequenceReadPolicy::AllowUnsupportedTracks)
        ==csol::SequenceSafetyStatus::Invalid)
        throw std::runtime_error("invalid character safety data");
}

__declspec(noinline) unsigned Prepare(std::span<const std::byte> bytes)
{
    const auto prepared=csol::PreparedSequenceModel::Read(bytes,csol::SequenceReadPolicy::AllowUnsupportedTracks);
    return prepared.RetainedBytes();
}

__declspec(noinline) unsigned CandidatePlan(std::span<const std::byte> bytes)
{
    const auto plan=csol::CandidatePlanStudioStorage(bytes,true);
    if(!plan.prefix)throw std::runtime_error("candidate changed dedicated prefix eligibility");
    return unsigned(plan.bytes);
}

void Boundaries()
{
    unsigned cases=0;
    const auto check=[&](Bytes bytes,unsigned frames,bool expected)
    {
        const bool original=csol::sequence_safety_detail::Animation(bytes,1,1,frames);
        const bool candidate=csol::sequence_safety_detail::CandidateAnimation(bytes,1,1,frames);
        if(original!=expected || candidate!=expected)throw std::runtime_error("RLE boundary result changed");
        ++cases;
    };
    const auto offset=[](Bytes& bytes,unsigned axis,std::uint16_t value)
    {std::memcpy(bytes.data()+axis*2,&value,2);};
    Bytes empty(16);check(empty,1,true);
    auto shortTable=empty;shortTable.pop_back();check(shortTable,1,false);
    Bytes run(20);offset(run,0,12);run[12]=run[13]=std::byte{1};check(run,1,true);
    auto odd=run;odd.insert(odd.begin()+12,std::byte{});offset(odd,0,13);check(odd,1,true);
    auto shared=run;offset(shared,1,12);check(shared,1,true);
    auto intoTable=run;offset(intoTable,0,11);check(intoTable,1,false);
    auto outside=run;offset(outside,0,65535);check(outside,1,false);
    auto zeroValid=run;zeroValid[12]=std::byte{};check(zeroValid,1,false);
    auto invalidTotal=run;invalidTotal[12]=std::byte{2};check(invalidTotal,1,false);
    auto shortEndpoint=run;shortEndpoint.pop_back();check(shortEndpoint,1,false);
    check(run,2,false);
    Bytes overlap(22);offset(overlap,0,12);offset(overlap,1,14);
    overlap[12]=overlap[13]=std::byte{2};overlap[14]=overlap[15]=std::byte{1};
    check(overlap,2,false);
    std::printf("boundaries\t%u\n",cases);
}

__declspec(noinline) unsigned Plan(std::span<const std::byte> bytes)
{
    const auto plan=csol::PlanStudioStorage(bytes,true);
    if(!plan.prefix)throw std::runtime_error("character did not qualify for dedicated prefix storage");
    return unsigned(plan.bytes);
}

double Elapsed(Clock::time_point begin)
{return std::chrono::duration<double>(Clock::now()-begin).count();}
}

int main(int argc,char** argv)
{
    try
    {
        if(argc!=3)throw std::runtime_error("expected model-list and iteration count");
        Boundaries();
        const int rounds=std::stoi(argv[2]);
        if(rounds<1 || rounds>10)throw std::runtime_error("iterations must be 1..10");
        std::ifstream list(std::filesystem::u8path(argv[1]));
        if(!list)throw std::runtime_error("cannot open model list");
        std::string path;
        unsigned index=0;
        while(std::getline(list,path))
        {
            if(!path.empty() && path.back()=='\r')path.pop_back();
            if(path.empty())throw std::runtime_error("empty model path");
            const auto started=Clock::now();
            std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
            if(!file)throw std::runtime_error("cannot open model");
            const auto size=file.tellg();
            if(size<244 || size>128*1024*1024)throw std::runtime_error("invalid model size");
            Bytes bytes(static_cast<std::size_t>(size));
            file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)))
                throw std::runtime_error("short model read");
            file.close();
            const double readSeconds=Elapsed(started);
            const auto count=csol::sequence_safety_detail::Word(bytes,164);
            std::printf("model\t%u\t%zu\t%u\t%.9f\n",index,bytes.size(),count,readSeconds);
            for(int round=0;round<rounds;++round)
            {
                auto begin=Clock::now();
                ReadSafety(bytes);
                const double safety=Elapsed(begin);
                begin=Clock::now();
                const unsigned prepared=Prepare(bytes);
                const double prepare=Elapsed(begin);
                unsigned storage{},candidateStorage{};
                double plan{},candidate{};
                // Alternate these two complete plans to avoid always granting
                // the candidate the immediately warmed animation buffer.
                const auto original=[&]{begin=Clock::now();storage=Plan(bytes);plan=Elapsed(begin);};
                const auto proposed=[&]{begin=Clock::now();candidateStorage=CandidatePlan(bytes);candidate=Elapsed(begin);};
                if((index+round)&1){proposed();original();}else{original();proposed();}
                if(prepared!=storage)throw std::runtime_error("owner/storage retained-byte mismatch");
                if(candidateStorage!=storage)throw std::runtime_error("candidate changed retained-byte count");
                std::printf("sample\t%u\t%d\t%.9f\t%.9f\t%.9f\t%u\t%.9f\n",
                    index,round,safety,prepare,plan,storage,candidate);
            }
            ++index;
        }
        if(!index)throw std::runtime_error("empty model list");
        std::printf("complete\t%u\t%d\n",index,rounds);
        return 0;
    }
    catch(const std::exception& error)
    {
        std::fprintf(stderr,"safety benchmark failed: %s\n",error.what());
        return 1;
    }
}
