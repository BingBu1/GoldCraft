// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only mapping of independently verified native function/data spans.
// Never calls the DLL entry point, resolves imports or modifies the source PE.
#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace native_random_test {
struct Span {std::uint32_t rva,size;bool executable,writable;};
class Image {
    std::vector<unsigned char> file;
    unsigned char* base{};
    std::uint32_t imageSize{};
    void Require(bool value,const char* why)const{if(!value)throw std::runtime_error(why);}
    template<class T>const T& At(std::size_t offset)const{
        Require(offset<=file.size()&&sizeof(T)<=file.size()-offset,"native PE header outside file");
        return *reinterpret_cast<const T*>(file.data()+offset);
    }
public:
    Image(const char* path,std::span<const Span> spans){
        std::ifstream stream(path,std::ios::binary);Require(bool(stream),"native PE unreadable");
        file.assign(std::istreambuf_iterator<char>(stream),{});
        const auto& dos=At<IMAGE_DOS_HEADER>(0);Require(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0,"native DOS header invalid");
        const auto& nt=At<IMAGE_NT_HEADERS32>(dos.e_lfanew);
        Require(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&
            nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC,"expected original x86 PE32");
        imageSize=nt.OptionalHeader.SizeOfImage;
        Require(imageSize>0&&imageSize<64*1024*1024,"native image size invalid");
        base=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(nt.OptionalHeader.ImageBase),imageSize,MEM_RESERVE,PAGE_NOACCESS));
        Require(base&&reinterpret_cast<std::uintptr_t>(base)==nt.OptionalHeader.ImageBase,"native preferred image base unavailable");
        std::map<std::uint32_t,DWORD> pages;
        try{
            for(const auto& span:spans){
                Require(span.rva<imageSize&&span.size<=imageSize-span.rva&&span.size,"native span outside image");
                const DWORD protection=span.executable?PAGE_EXECUTE_READ:span.writable?PAGE_READWRITE:PAGE_READONLY;
                for(auto page=span.rva&~4095u;page<((span.rva+span.size+4095u)&~4095u);page+=4096){
                    auto [it,inserted]=pages.emplace(page,protection);
                    Require(inserted||it->second==protection,"mixed native page protection unsupported");
                    if(inserted)Require(VirtualAlloc(base+page,4096,MEM_COMMIT,PAGE_READWRITE)==base+page,"native page commit failed");
                }
                const auto table=std::size_t(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
                bool found=false;
                for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){
                    const auto& section=At<IMAGE_SECTION_HEADER>(table+i*sizeof(IMAGE_SECTION_HEADER));
                    const auto length=std::max(section.Misc.VirtualSize,section.SizeOfRawData);
                    if(span.rva<section.VirtualAddress||span.rva-section.VirtualAddress>=length)continue;
                    const auto offset=span.rva-section.VirtualAddress;
                    Require(span.size<=length-offset,"native span crosses section");
                    if(offset<section.SizeOfRawData){
                        const auto count=std::min<std::uint32_t>(span.size,section.SizeOfRawData-offset);
                        const auto raw=std::size_t(section.PointerToRawData)+offset;
                        Require(raw<=file.size()&&count<=file.size()-raw,"native span outside raw file");
                        std::memcpy(base+span.rva,file.data()+raw,count);
                    }
                    found=true;break;
                }
                Require(found,"native span has no original section");
            }
            for(const auto& [page,protection]:pages){DWORD previous{};Require(VirtualProtect(base+page,4096,protection,&previous)!=0,"native protection failed");}
            FlushInstructionCache(GetCurrentProcess(),base,imageSize);
        }catch(...){VirtualFree(base,0,MEM_RELEASE);base=nullptr;throw;}
    }
    ~Image(){if(base)VirtualFree(base,0,MEM_RELEASE);}
    Image(const Image&)=delete;Image& operator=(const Image&)=delete;
    void* Base()const{return base;}
    template<class T>T Address(std::uint32_t rva)const{return reinterpret_cast<T>(base+rva);}
};
}
