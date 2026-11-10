// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "resource_refresh.hpp"
#if CSOL_RESOURCE_REFRESH
#include "resource_font_memory.hpp"
#include <algorithm>
#include <limits>

namespace csol::client {
namespace {
std::vector<ResourceFontMemory> registrations;
void ForgetRetired()
{std::erase_if(registrations,[](const auto& row){return !row.owner||*row.owner!=row.handle;});}
void Record(const ResourceFontMemory& value)
{
    const auto found=std::find_if(registrations.begin(),registrations.end(),[&](const auto& row){return row.owner==value.owner;});
    if(found==registrations.end())registrations.push_back(value);else *found=value;
}
}
void* RegisterResourceFont(void*& owner,const char* resource,const char* family,
                           std::span<const std::byte> bytes,unsigned long* count)
{
    if(count)*count=0;
    if(bytes.empty()||bytes.size()>std::numeric_limits<DWORD>::max())return nullptr;
    ForgetRetired();
    // Several HUD owners register the same TTF. Retain one exact byte copy,
    // independent of later filesystem changes, for transaction rollback.
    std::shared_ptr<const std::vector<std::byte>> saved;
    for(const auto& row:registrations)
        if(row.bytes->size()==bytes.size()&&std::equal(bytes.begin(),bytes.end(),row.bytes->begin())){saved=row.bytes;break;}
    if(!saved)saved=std::make_shared<const std::vector<std::byte>>(bytes.begin(),bytes.end());
    DWORD added{};auto handle=AddFontMemResourceEx(const_cast<std::byte*>(saved->data()),DWORD(saved->size()),nullptr,&added);
    if(!handle||!added){if(handle)RemoveFontMemResourceEx(handle);return nullptr;}
    try{Record({&owner,handle,resource?resource:"",family?family:"",std::move(saved)});}
    catch(...){RemoveFontMemResourceEx(handle);throw;}
    owner=handle;if(count)*count=added;return handle;
}
std::vector<ResourceFontMemory> SnapshotResourceFonts(){ForgetRetired();return registrations;}
bool RemoveResourceFonts(std::span<const ResourceFontMemory> fonts,std::string& error)
{
    for(const auto& font:fonts){
        if(!font.owner||!*font.owner)continue;
        if(!RemoveFontMemResourceEx(*font.owner)){error="Cannot retire private font registration";return false;}
        *font.owner=nullptr;
    }
    ForgetRetired();
    return true;
}
bool InstallResourceFont(ResourceFontMemory& font,std::shared_ptr<const std::vector<std::byte>> bytes,std::string& error)
{
    if(!font.owner||*font.owner||!bytes||bytes->empty()||bytes->size()>std::numeric_limits<DWORD>::max()){
        error="Invalid private font replacement state";return false;
    }
    // Copy allocating metadata before GDI acquires a registration. From that
    // point onward every throwing operation must have a cleanup owner.
    auto candidate=font;candidate.bytes=std::move(bytes);
    DWORD count{};auto handle=AddFontMemResourceEx(const_cast<std::byte*>(candidate.bytes->data()),DWORD(candidate.bytes->size()),nullptr,&count);
    if(!handle||!count){if(handle)RemoveFontMemResourceEx(handle);error="Cannot register verified private font bytes";return false;}
    candidate.handle=handle;
    try{Record(candidate);}catch(...){RemoveFontMemResourceEx(handle);throw;}
    *font.owner=handle;font=std::move(candidate);return true;
}
}
#else
namespace csol::client {
void* RegisterResourceFont(void*& owner,const char*,const char*,
                           std::span<const std::byte> bytes,unsigned long* count)
{
    owner=AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()),DWORD(bytes.size()),nullptr,count);
    return owner;
}
}
#endif
