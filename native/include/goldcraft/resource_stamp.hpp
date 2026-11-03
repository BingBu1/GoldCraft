// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace goldcraft::filesystem
{
inline constexpr char ResourceStampVersion[]="GoldCraftResourceStamp001";
// Fixed-width optional provider contract. No FILE*, CRT or private VFS layout
// crosses the module boundary. The owner is the current IFileSystem instance.
struct ResourceStamp
{
    std::uint64_t volume{},fileLow{},fileHigh{},size{},created{},modified{},changed{};
    std::uint64_t offset{},length{},packed{};
    bool operator==(const ResourceStamp&) const = default;
};
static_assert(sizeof(ResourceStamp)==80 && offsetof(ResourceStamp,length)==64);
struct ResourceStampAPI
{
    std::uint32_t size;
    int (*query)(const void* owner,const char* name,ResourceStamp* result,std::uint32_t size);
    int (*opened)(const void* owner,void* file,ResourceStamp* result,std::uint32_t size);
};
}
