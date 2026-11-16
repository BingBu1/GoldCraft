// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "player_actions.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace guest::cof
{
struct ActivitySequence { int activity{},weight{}; };
struct ActivityRandomHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    bool (*random)(void*,int low,int high,int&) noexcept {};
};
enum class ActivityLookupResult : unsigned char { Applied, Cancelled, MissingBackend };

// Original CoF MP0036b91c /1000F450 uses reservoir selection, unlike the
// host CS two-pass selector. Grouping is cold; original order/RNG stays exact.
class ActivityCatalog final
{
public:
    // Cold host adapter for the actual v10 descriptor table. A cache prefix
    // must end exactly at texturedataindex, as in the validated engine cache.
    // Bad replacement input leaves the previous valid catalog intact.
    bool PrepareModel(std::span<const std::byte> data,PlayerModelExtent extent=PlayerModelExtent::CompleteFile)
    {
        if(data.size()<244||data.size()>static_cast<unsigned>((std::numeric_limits<int>::max)()))return false;
        const auto word=[&](std::size_t at){
            const auto* p=reinterpret_cast<const unsigned char*>(data.data()+at);
            return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
        };
        const auto integer=[&](std::size_t at){return std::bit_cast<std::int32_t>(word(at));};
        if(word(0)!=0x54534449U||integer(4)!=10)return false;
        if(extent==PlayerModelExtent::CompleteFile)
        {if(integer(72)!=static_cast<int>(data.size()))return false;}
        else if(extent!=PlayerModelExtent::ValidatedCachePrefix||integer(72)<static_cast<int>(data.size())||
            integer(180)<=0||integer(184)<244||integer(188)!=static_cast<int>(data.size()))return false;
        const int count=integer(164),offset=integer(168);
        if(count<0||count>2048)return false; // Matched host MAXSTUDIOSEQUENCES.
        if(!count)return Prepare({});
        if(offset<244||static_cast<unsigned>(offset)>data.size()||
            static_cast<unsigned>(count)>(data.size()-offset)/176)return false;
        std::vector<ActivitySequence> sequences;sequences.reserve(count);
        for(int i=0;i<count;++i)
        {
            const auto at=std::size_t(offset)+std::size_t(i)*176;
            sequences.push_back({integer(at+40),integer(at+44)});
        }
        return Prepare(sequences);
    }
    bool Prepare(std::span<const ActivitySequence> sequences)
    {
        if(generation_==UINT64_MAX||sequences.size()>2048)return false;
        std::vector<Entry> entries;entries.reserve(sequences.size());
        for(std::size_t i=0;i<sequences.size();++i)
            entries.push_back({sequences[i].activity,sequences[i].weight,static_cast<int>(i)});
        std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){
            return a.activity<b.activity||(a.activity==b.activity&&a.sequence<b.sequence);
        });
        std::vector<Group> groups;
        for(std::size_t i=0;i<entries.size();)
        {
            const auto begin=i;const int activity=entries[i].activity;
            do{++i;}while(i<entries.size()&&entries[i].activity==activity);
            groups.push_back({activity,begin,i});
        }
        entries_.swap(entries);groups_.swap(groups);++generation_;return true;
    }
    ActivityLookupResult Lookup(int activity,const ActivityRandomHost& host,int& output) const noexcept
    {
        if(!generation_||!host.current||!host.random)return ActivityLookupResult::MissingBackend;
        const auto generation=generation_;
        const auto current=[&]{return host.current(host.context)&&generation_==generation;};
        if(!current())return ActivityLookupResult::Cancelled;
        const auto group=std::lower_bound(groups_.begin(),groups_.end(),activity,
            [](const Group& g,int value){return g.activity<value;});
        std::int32_t total{};int selected=-1;
        if(group!=groups_.end()&&group->activity==activity)
        {
            const auto end=group->end;
            for(auto i=group->begin;i<end;++i)
            {
                const auto entry=entries_[i];
                total=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(total)+
                    std::bit_cast<std::uint32_t>(entry.weight));
                if(!total){selected=entry.sequence;continue;}
                const int high=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(total)-1U);
                int random{};
                if(!current())return ActivityLookupResult::Cancelled;
                const bool delivered=host.random(host.context,0,high,random);
                if(!current())return ActivityLookupResult::Cancelled;
                if(!delivered)return ActivityLookupResult::MissingBackend;
                if(random<entry.weight)selected=entry.sequence;
            }
        }
        output=selected;return ActivityLookupResult::Applied;
    }
private:
    struct Entry {int activity,weight,sequence;};
    struct Group {int activity;std::size_t begin,end;};
    std::vector<Entry> entries_;
    std::vector<Group> groups_;
    std::uint64_t generation_{};
};
}
