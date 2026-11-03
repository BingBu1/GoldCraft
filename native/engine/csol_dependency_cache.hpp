// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace goldcraft::csol_resources
{
struct Stamp
{
    bool exists{}, read{}, reusable{true};
    std::array<unsigned char,32> digest{};
    bool operator==(const Stamp&) const = default;
};
using Inputs = std::map<std::string,Stamp>;
using Files = std::vector<std::string>;
using Result = std::shared_ptr<const Files>;

// Stores paths and VFS content fingerprints, never engine pointers.
// Every probe matters: a newly created optional JSON/SEQ must invalidate a hit.
class DependencyCache
{
public:
    template<class Probe>
    Result Find(const std::string& key, Probe&& probe)
    {
        auto found=entries_.find(key);
        if(found==entries_.end())return {};
        for(const auto& [name,stamp]:found->second.inputs)
            if(!stamp.reusable || probe(name,stamp.read)!=stamp)
            {Erase(found);return {};}
        found->second.used=++tick_;
        return found->second.files;
    }
    template<class Probe>
    void Store(std::string key, Result files, Inputs inputs, Probe&& probe)
    {
        if(auto found=entries_.find(key);found!=entries_.end())Erase(found);
        if(!files || inputs.empty() || inputs.size()>8192)return;
        std::size_t bytes=sizeof(Entry)+key.capacity()+128;
        for(const auto& [name,stamp]:inputs)
        {
            if(!stamp.reusable || probe(name,stamp.read)!=stamp)return;
            bytes+=sizeof(Inputs::value_type)+128+name.capacity();
        }
        bytes+=sizeof(Files)+files->capacity()*sizeof(std::string);
        for(const auto& name:*files)bytes+=name.capacity();
        if(bytes>Budget)return;
        while(!entries_.empty() && (entries_.size()>=512 || bytes_+bytes>Budget))
            Erase(std::min_element(entries_.begin(),entries_.end(),
                [](const auto& a,const auto& b){return a.second.used<b.second.used;}));
        entries_.emplace(std::move(key),Entry{std::move(files),std::move(inputs),bytes,++tick_});
        bytes_+=bytes;
    }
    std::size_t Size() const noexcept {return entries_.size();}
    std::size_t Bytes() const noexcept {return bytes_;}
private:
    static constexpr std::size_t Budget=16u*1024u*1024u;
    struct Entry {Result files;Inputs inputs;std::size_t bytes;std::uint64_t used;};
    using Entries=std::unordered_map<std::string,Entry>;
    void Erase(Entries::iterator found) {bytes_-=found->second.bytes;entries_.erase(found);}
    Entries entries_;
    std::size_t bytes_{};
    std::uint64_t tick_{};
};
}
