// SPDX-License-Identifier: GPL-3.0-or-later
// Public IFileSystem and optional interface only. No private provider layouts.
#include "osconfig.h"
#include "FileSystem.h"
#include "goldcraft/resource_stamp.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <string>
#include <stdexcept>
#include <cstdint>

namespace {
using goldcraft::filesystem::ResourceStamp;
using goldcraft::filesystem::ResourceStampAPI;
IFileSystem* fs;
const ResourceStampAPI* api;
unsigned checks;
void Check(bool ok,const char* expression,unsigned line)
{++checks;if(!ok)throw std::runtime_error(std::string(expression)+" at line "+std::to_string(line)+" winerr "+std::to_string(GetLastError()));}
#define CHECK(x) Check(bool(x),#x,__LINE__)
void Write(const std::filesystem::path& name,const std::string& bytes)
{std::ofstream file(name,std::ios::binary|std::ios::trunc);CHECK(bool(file));file.write(bytes.data(),bytes.size());CHECK(bool(file));file.close();CHECK(!file.fail());}
std::string Read(const char* name)
{
    const auto h=fs->Open(name,"rb");CHECK(h);const auto length=fs->Size(h);std::string bytes(length,'\0');
    CHECK(fs->Read(bytes.data(),int(length),h)==int(length));fs->Close(h);return bytes;
}
ResourceStamp Query(const char* name)
{ResourceStamp value{};CHECK(api->query(fs,name,&value,sizeof(value))==1);return value;}
ResourceStamp Opened(FileHandle_t file)
{ResourceStamp value{};CHECK(api->opened(fs,file,&value,sizeof(value))==1);return value;}
void Paths(const std::filesystem::path& a,const std::filesystem::path& b={})
{fs->RemoveAllSearchPaths();fs->AddSearchPath(a.string().c_str(),"GAME");if(!b.empty())fs->AddSearchPath(b.string().c_str(),"FALLBACK");}
FILETIME Mtime(const std::filesystem::path& path)
{
    const auto h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(h!=INVALID_HANDLE_VALUE);FILETIME time{};CHECK(GetFileTime(h,nullptr,nullptr,&time));CHECK(CloseHandle(h));return time;
}
void SetMtime(const std::filesystem::path& path,FILETIME value)
{
    const auto h=CreateFileW(path.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(h!=INVALID_HANDLE_VALUE);CHECK(SetFileTime(h,nullptr,nullptr,&value));CHECK(CloseHandle(h));
}
struct HeapState {std::uint64_t bytes{},blocks{};DWORD handles{};};
HeapState Heap()
{
    HANDLE heaps[128]{};const auto count=GetProcessHeaps(128,heaps);CHECK(count>0 && count<=128);HeapState state;
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&state.handles));
    for(unsigned i=0;i<count;++i){CHECK(HeapLock(heaps[i]));PROCESS_HEAP_ENTRY entry{};
        while(HeapWalk(heaps[i],&entry))if(entry.wFlags&PROCESS_HEAP_ENTRY_BUSY){state.bytes+=entry.cbData;++state.blocks;}
        const auto error=GetLastError();CHECK(HeapUnlock(heaps[i]));CHECK(error==ERROR_NO_MORE_ITEMS);}
    return state;
}
void Loose(const std::filesystem::path& root)
{
    auto a=root/"a",b=root/"b";std::filesystem::create_directories(a);std::filesystem::create_directories(b);
    Write(a/"same.bin","ABCD");Write(b/"same.bin","WXYZ");Paths(a,b);
    auto initial=Query("same.bin");CHECK(!initial.packed && initial.offset==0 && initial.size==4 && initial.length==4);
    auto file=fs->Open("same.bin","rb");CHECK(file);CHECK(Opened(file)==initial);char bytes[4]{};
    CHECK(fs->Read(bytes,2,file)==2 && std::string(bytes,2)=="AB");CHECK(fs->Tell(file)==2);
    CHECK(Query("same.bin")==initial);CHECK(Opened(file)==initial);CHECK(fs->Tell(file)==2);
    CHECK(fs->Read(bytes,2,file)==2 && std::string(bytes,2)=="CD");fs->Close(file);

    const auto mtime=Mtime(a/"same.bin");Write(a/"same.bin","EFGH");SetMtime(a/"same.bin",mtime);
    const auto updated=Query("same.bin");CHECK(updated.size==initial.size && updated.modified==initial.modified);
    CHECK(updated.fileLow==initial.fileLow && updated.fileHigh==initial.fileHigh);CHECK(updated.changed!=initial.changed);
    CHECK(updated!=initial);CHECK(Read("same.bin")=="EFGH");
    Write(a/"replacement.bin","QRST");SetMtime(a/"replacement.bin",mtime);
    CHECK(MoveFileExW((a/"replacement.bin").c_str(),(a/"same.bin").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));
    const auto renamed=Query("same.bin");CHECK(renamed.fileLow!=updated.fileLow || renamed.fileHigh!=updated.fileHigh);
    CHECK(renamed.modified==updated.modified);CHECK(Read("same.bin")=="QRST");

    // Open failures must select the same fallback as ordinary Open.
    const auto locked=CreateFileW((a/"same.bin").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);CHECK(locked!=INVALID_HANDLE_VALUE);
    const auto fallback=Query("same.bin");CHECK(Read("same.bin")=="WXYZ");CHECK(fallback!=renamed);CHECK(CloseHandle(locked));
    Paths(b,a);CHECK(Query("same.bin")==fallback);CHECK(Read("same.bin")=="WXYZ");Paths(a,b);
    CHECK(Query("same.bin")==renamed);

    ResourceStamp out{};CHECK(!api->query(fs,"optional.json",&out,sizeof(out)));
    CHECK(!fs->FileExists("optional.json"));Write(a/"optional.json","{}");CHECK(fs->FileExists("optional.json"));CHECK(Query("optional.json").length==2);
    CHECK(DeleteFileW((a/"optional.json").c_str()));CHECK(!api->query(fs,"optional.json",&out,sizeof(out)));
    CHECK(!api->query(nullptr,"same.bin",&out,sizeof(out)));CHECK(!api->query(reinterpret_cast<void*>(1),"same.bin",&out,sizeof(out)));
    CHECK(!api->query(fs,nullptr,&out,sizeof(out)));CHECK(!api->query(fs,"",&out,sizeof(out)));
    CHECK(!api->query(fs,"same.bin",nullptr,sizeof(out)));CHECK(!api->query(fs,"same.bin",&out,sizeof(out)-1));
    CHECK(!api->query(fs,"same.bin",&out,sizeof(out)+1));CHECK(!api->opened(fs,nullptr,&out,sizeof(out)));
    file=fs->Open("same.bin","rb");CHECK(file);CHECK(!api->opened(nullptr,file,&out,sizeof(out)));
    CHECK(!api->opened(fs,file,&out,sizeof(out)-1));CHECK(!api->opened(fs,file,nullptr,sizeof(out)));fs->Close(file);
    std::filesystem::create_directory(a/"folder");CHECK(!api->query(fs,"folder",&out,sizeof(out)));
    // Warm all internal tables before checking live allocation and OS handle balance.
    for(unsigned i=0;i<64;++i)CHECK(Query("same.bin")==renamed);
    const auto before=Heap();for(unsigned i=0;i<10000;++i)CHECK(Query("same.bin")==renamed);const auto after=Heap();
    std::printf("loose-balance bytes=%llu->%llu blocks=%llu->%llu handles=%lu->%lu\n",before.bytes,after.bytes,before.blocks,after.blocks,before.handles,after.handles);
    CHECK(after.bytes==before.bytes && after.blocks==before.blocks && after.handles==before.handles);
}
void U32(std::string& bytes,std::size_t at,std::uint32_t value){std::memcpy(bytes.data()+at,&value,4);}
void U64(std::string& bytes,std::size_t at,std::uint64_t value){std::memcpy(bytes.data()+at,&value,8);}
struct PackMade {std::uint64_t offset,size;};
PackMade MakePack(const std::filesystem::path& path,bool wide,bool appended,bool invalid=false)
{
    const std::size_t prefix=appended?97:0,header=wide?20:12,dirItem=wide?128:64;
    const std::string first="0123456789",second="abcdefghijkl";
    const auto directory=header+first.size()+second.size();
    std::string bytes(prefix+directory+2*dirItem+(appended?24:0),'\0');
    std::memcpy(bytes.data()+prefix,wide?"PK64":"PACK",4);
    if(wide){U64(bytes,prefix+4,directory);U64(bytes,prefix+12,2*dirItem);}
    else {U32(bytes,prefix+4,unsigned(directory));U32(bytes,prefix+8,unsigned(2*dirItem));}
    bytes.replace(prefix+header,first.size(),first);bytes.replace(prefix+header+first.size(),second.size(),second);
    for(unsigned i=0;i<2;++i){const auto at=prefix+directory+i*dirItem;const std::string name=i?"folder/second.bin":"first.bin";
        std::memcpy(bytes.data()+at,name.data(),name.size());const auto position=header+(i?first.size():0);
        const auto length=i?second.size():first.size();
        if(wide){U64(bytes,at+112,invalid && !i?UINT64_MAX:position);U64(bytes,at+120,length);}
        else {U32(bytes,at+56,invalid && !i?0x7fffffffu:unsigned(position));U32(bytes,at+60,unsigned(length));}}
    if(appended){const auto at=bytes.size()-24;std::memcpy(bytes.data()+at,"PACKAPPE",8);U64(bytes,at+8,prefix);U64(bytes,at+16,prefix);}
    Write(path,bytes);return {prefix+header,bytes.size()};
}
void Pack(const std::filesystem::path& root,bool wide,bool appended,bool invalid)
{
    const auto a=root/"pack";std::filesystem::create_directories(a);
    const auto path=a/(appended?"appended.bin":"pak0.pak");const auto made=MakePack(path,wide,appended,invalid);
    fs->RemoveAllSearchPaths();
    std::printf("mounting %s\n",path.string().c_str());std::fflush(stdout);
    if(appended)CHECK(fs->AddPackFile(path.string().c_str(),"GAME"));else fs->AddSearchPath(a.string().c_str(),"GAME");
    std::printf("mounted\n");std::fflush(stdout);
    ResourceStamp value{};
    if(invalid){CHECK(!api->query(fs,"first.bin",&value,sizeof(value)));return;}
    const auto first=Query("first.bin"),second=Query("folder/second.bin");
    CHECK(first.packed==1 && first.offset==made.offset && first.length==10 && first.size==made.size);
    CHECK(second.offset==first.offset+10 && second.length==12);CHECK(second.fileLow==first.fileLow && second.fileHigh==first.fileHigh);
    CHECK(Query("FOLDER\\SECOND.BIN")==second);
    auto file=fs->Open("first.bin","rb");CHECK(file);CHECK(Opened(file)==first);char bytes[16]{};
    CHECK(fs->Read(bytes,3,file)==3 && std::string(bytes,3)=="012");CHECK(fs->Tell(file)==3);
    for(unsigned i=0;i<64;++i)CHECK(Query("folder/second.bin")==second);
    CHECK(fs->Tell(file)==3 && !fs->EndOfFile(file));CHECK(Opened(file)==first);
    const auto before=Heap();for(unsigned i=0;i<10000;++i)CHECK(Query("folder/second.bin")==second);const auto after=Heap();
    CHECK(fs->Tell(file)==3);CHECK(fs->Read(bytes,7,file)==7 && std::string(bytes,7)=="3456789");CHECK(fs->EndOfFile(file));
    CHECK(Query("folder/second.bin")==second);CHECK(fs->EndOfFile(file));CHECK(fs->Tell(file)==10);
    std::printf("pack-balance bytes=%llu->%llu blocks=%llu->%llu handles=%lu->%lu\n",before.bytes,after.bytes,before.blocks,after.blocks,before.handles,after.handles);
    CHECK(after.bytes==before.bytes && after.blocks==before.blocks && after.handles==before.handles);fs->Close(file);
    CHECK(Read("folder/second.bin")=="abcdefghijkl");
    // A newly present loose file in this mount takes the same winner as Open.
    if(!appended){Write(a/"first.bin","LOOSE");const auto loose=Query("first.bin");CHECK(!loose.packed && loose.offset==0 && loose.length==5);
        CHECK(Read("first.bin")=="LOOSE");CHECK(DeleteFileW((a/"first.bin").c_str()));CHECK(Query("first.bin")==first);}
}
}
int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    try{
        CHECK(argc==4);const std::string mode=argv[2];const auto root=std::filesystem::absolute(argv[3]);
        CHECK(!std::filesystem::exists(root));CHECK(std::filesystem::create_directories(root));
        const auto library=LoadLibraryW(std::filesystem::absolute(argv[1]).c_str());CHECK(library);
        const auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(library,"CreateInterface"));CHECK(factory);
        fs=static_cast<IFileSystem*>(factory(FILESYSTEM_INTERFACE_VERSION,nullptr));CHECK(fs);
        api=reinterpret_cast<const ResourceStampAPI*>(factory(goldcraft::filesystem::ResourceStampVersion,nullptr));
        if(mode=="legacy"){CHECK(!api);CHECK(FreeLibrary(library));std::printf("legacy-provider checks=%u\n",checks);return 0;}
        CHECK(api && api->size==sizeof(ResourceStampAPI) && api->query && api->opened);fs->Mount();
        if(mode=="loose")Loose(root);
        else Pack(root,mode.find("64")!=std::string::npos,mode.find("appended")!=std::string::npos,mode.find("invalid")!=std::string::npos);
        fs->RemoveAllSearchPaths();fs->Unmount();CHECK(FreeLibrary(library));
        std::printf("provider mode=%s checks=%u failures=0\n",mode.c_str(),checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"provider failure: %s checks=%u\n",e.what(),checks);return 1;}
}
