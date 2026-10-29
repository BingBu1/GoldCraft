// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../native/csol/scenario/difficulty.hpp"
#include "../../native/csol/scenario/drop_items.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace difficulty_test
{
inline int checks{}, failures{};
inline void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
inline void Require(bool value, const char* label)
{ Check(value,label); if (!value) throw std::runtime_error(label); }
inline std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("required frozen resource missing: "+path.string());
    return {std::istreambuf_iterator<char>(stream),{}};
}
inline std::span<const std::byte> Bytes(const std::string& value)
{ return std::as_bytes(std::span(value.data(),value.size())); }
inline bool Bits(float value, std::uint32_t expected)
{ return std::bit_cast<std::uint32_t>(value)==expected; }

// Independent fixture writer, never used as a numeric or parser oracle.
inline std::string Encrypt(std::string value)
{
    while (value.size()%8) value.push_back('\0');
    constexpr std::uint32_t k[]{0xb38520fd,0x5f30e75f,0xe2eb67e0,0xfc67a521};
    const auto get=[](const char* p) {
        std::uint32_t v{}; for(int i=0;i<4;++i)v|=std::uint32_t(static_cast<unsigned char>(p[i]))<<(8*i); return v;
    };
    const auto put=[](char* p,std::uint32_t v) {for(int i=0;i<4;++i)p[i]=static_cast<char>(v>>(8*i));};
    for(std::size_t at=0;at<value.size();at+=8) {
        auto a=get(value.data()+at),b=get(value.data()+at+4);std::uint32_t sum{};
        for(int round=0;round<32;++round) {
            sum+=0x9e3779b9u;
            a+=((b<<4)+k[0])^(b+sum)^((b>>5)+k[1]);
            b+=((a<<4)+k[2])^(a+sum)^((a>>5)+k[3]);
        }
        put(value.data()+at,a);put(value.data()+at+4,b);
    }
    return value;
}

struct Files
{
    inline static Files* current{};
    struct Buffer { std::string path, contents; };
    std::map<std::string,std::string> contents;
    std::map<void*,Buffer> buffers;
    std::vector<std::string> order;
    std::function<void(std::string_view)> onLoad,onFree;
    std::optional<int> reportedLength;
    Files() { Require(!current,"single file callback owner"); current=this; }
    ~Files()
    {
        Check(buffers.empty(),"all callback buffers released");
        for(const auto& [p,unused]:buffers)delete[] static_cast<unsigned char*>(p);
        current=nullptr;
    }
    static unsigned char* Load(const char* path,int* length)
    {
        auto& f=*current;Require(length&&*length==0,"load begins with length zero");
        f.order.push_back(std::string("load:")+path);
        const auto callback=f.onLoad;if(callback)callback(path);
        const auto found=f.contents.find(path);if(found==f.contents.end())return nullptr;
        auto* data=new unsigned char[found->second.size()+1];
        std::memcpy(data,found->second.data(),found->second.size());data[found->second.size()]=0;
        *length=f.reportedLength.value_or(static_cast<int>(found->second.size()));
        f.buffers.emplace(data,Buffer{path,found->second});return data;
    }
    static void Free(void* data)
    {
        auto& f=*current;const auto found=f.buffers.find(data);
        Require(found!=f.buffers.end(),"exact buffer released once");
        const auto record=found->second;
        Check(std::memcmp(data,record.contents.data(),record.contents.size())==0 &&
            static_cast<unsigned char*>(data)[record.contents.size()]==0,"loader preserves ciphertext and engine sentinel");
        f.order.push_back("free:"+record.path);
        const auto callback=f.onFree;if(callback)callback(record.path);
        f.buffers.erase(data);std::memset(data,0xcc,record.contents.size()+1);delete[] static_cast<unsigned char*>(data);
    }
    csol::scenario::ResourceFileApi Api() const { return {Load,Free}; }
};
inline constexpr const char* DefaultPath="maps/ZBS_Difficulty_Default.cso";
inline constexpr const char* MapPath="maps/ZBS_Difficulty_map_session_test.cso";
inline constexpr const char* DropPath="maps/ZBS_DropItem.cso";
inline constexpr const char* TinyDifficulty="id,name,value\r\n99,first,2\r\n1,second,3\r\n";
inline constexpr const char* TinyDrops="id,diff,effect,p,owner,count,remove,reward\r\n1,0,0,1,0,1,8,10\r\n1,1,0,2,0,1,8,11\r\n1,2,0,3,0,1,8,12\r\n";
}
