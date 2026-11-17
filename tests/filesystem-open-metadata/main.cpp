// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <osconfig.h>
#include <FileSystem.h>
#include "goldcraft/open_file_metadata.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/utime.h>
#include <vector>

namespace {
namespace path=std::filesystem;
unsigned checks{};
void Check(bool condition,const char* why)
{
    ++checks;
    if(!condition)throw std::runtime_error(why);
}
void Write(const path::path& name,const std::string& bytes)
{
    path::create_directories(name.parent_path());
    std::ofstream file(name,std::ios::binary);
    Check(bool(file.write(bytes.data(),bytes.size())),"fixture write failed");
}
void Pak(const path::path& name)
{
    struct Entry {char name[56];int offset,size;};
    Entry entry{};std::strcpy(entry.name,"sound/packed.wav");entry.offset=12;entry.size=11;
    std::ofstream file(name,std::ios::binary);file.write("PACK",4);
    const int offset=23,size=sizeof(entry);
    file.write(reinterpret_cast<const char*>(&offset),4);file.write(reinterpret_cast<const char*>(&size),4);
    file.write("PACKED-DATA",11);file.write(reinterpret_cast<const char*>(&entry),sizeof(entry));
    Check(bool(file),"PAK fixture write failed");
}
std::string Read(IFileSystem* fs,const char* name,const char* id=nullptr)
{
    auto file=fs->Open(name,"rb",id);
    Check(file!=nullptr,"VFS read open failed");
    std::string bytes(fs->Size(file),'\0');
    Check(fs->Read(bytes.data(),int(bytes.size()),file)==int(bytes.size()),"VFS short read");
    fs->Close(file);return bytes;
}
void HeaderChecks(const path::path& at)
{
    Write(at,"FILE-BODY");
    struct _stat expected{},current{},sentinel{};
    std::memset(&sentinel,0x55,sizeof(sentinel));current=sentinel;
    Check(!goldcraft::filesystem::OpenFileMetadata(nullptr,current),"null stream accepted");
    Check(!std::memcmp(&sentinel,&current,sizeof(current)),"failed query changed output");
    auto* file=std::fopen(at.string().c_str(),"rb");Check(file,"header fixture open");
    Check(_stat(at.string().c_str(),&expected)==0,"header fixture stat");
    Check(std::fseek(file,3,SEEK_SET)==0,"seek failed");
    Check(goldcraft::filesystem::OpenFileMetadata(file,current),"regular stream rejected");
    Check(current.st_size==expected.st_size && current.st_ctime==expected.st_ctime &&
        current.st_mtime==expected.st_mtime,"size/time fields differ");
    Check(std::ftell(file)==3,"metadata moved stream");
    while(std::fgetc(file)!=EOF){}
    Check(std::feof(file),"EOF setup");
    Check(goldcraft::filesystem::OpenFileMetadata(file,current) && std::feof(file),"metadata reset EOF");
    std::fclose(file);
    file=std::fopen("NUL","rb");Check(file,"NUL open failed");current=sentinel;
    Check(!goldcraft::filesystem::OpenFileMetadata(file,current),"device stream bypasses fallback");
    Check(!std::memcmp(&sentinel,&current,sizeof(current)),"device query changed output");std::fclose(file);
    int descriptors[2]{};Check(_pipe(descriptors,64,_O_BINARY)==0,"pipe creation failed");
    file=_fdopen(descriptors[0],"rb");Check(file,"pipe fdopen failed");current=sentinel;
    Check(!goldcraft::filesystem::OpenFileMetadata(file,current),"pipe stream bypasses fallback");
    Check(!std::memcmp(&sentinel,&current,sizeof(current)),"pipe query changed output");
    std::fclose(file);_close(descriptors[1]);
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc!=4)throw std::runtime_error("expected filesystem DLL, fresh fixture root and actual resource list");
        const auto dll=path::absolute(argv[1]),root=path::absolute(argv[2]);
        Check(path::exists(dll) && !path::exists(root),"require current DLL and fresh fixture");
        HeaderChecks(root/"header/body.bin");
        const auto first=root/"first",second=root/"second";
        Write(first/"models/shared.mdl","FIRST");Write(second/"models/shared.mdl","SECOND");
        Write(first/"models/empty.mdl","");Pak(first/"pak0.pak");
        __utimbuf64 times{1900000000,1900000000};
        Check(!_utime64((first/"models/shared.mdl").string().c_str(),&times),"mtime fixture");
        auto module=LoadLibraryA(dll.string().c_str());Check(module,"cannot load actual filesystem");
        using Factory=void*(__cdecl*)(const char*,int*);
        auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"CreateInterface"));Check(factory,"filesystem factory missing");
        auto* fs=static_cast<IFileSystem*>(factory("VFileSystem009",nullptr));Check(fs,"IFileSystem009 missing");
        fs->Mount();fs->AddSearchPath(first.string().c_str(),"FIRST");fs->AddSearchPath(second.string().c_str(),"SECOND");
        Check(Read(fs,"models/shared.mdl")=="FIRST","search order changed");
        Check(Read(fs,"models/shared.mdl","SECOND")=="SECOND","pathID selection changed");
        Check(Read(fs,"MODELS\\SHARED.MDL","FIRST")=="FIRST","native case/slash behavior changed");
        Check(Read(fs,"sound/packed.wav")=="PACKED-DATA","PAK member changed");
        auto a=fs->Open("sound/packed.wav","rb"),b=fs->Open("sound/packed.wav","rb");
        Check(a && b,"interleaved PAK open failed");
        char part[4]{};fs->Seek(a,7,FILESYSTEM_SEEK_HEAD);Check(fs->Read(part,4,a)==4 && !std::memcmp(part,"DATA",4),"PAK offset changed");
        fs->Seek(b,0,FILESYSTEM_SEEK_HEAD);Check(fs->Read(part,4,b)==4 && !std::memcmp(part,"PACK",4),"PAK interleave changed");
        fs->Close(a);fs->Close(b);
        auto empty=fs->Open("models/empty.mdl","rb");Check(empty && fs->Size(empty)==0,"empty regular file changed");fs->Close(empty);
        Check(!fs->Open("models/missing.mdl","rb"),"missing file accepted");
        auto shared=fs->Open("models/shared.mdl","rb");Check(shared && fs->Size(shared)==5,"open size differs");fs->Close(shared);
        Check(fs->GetFileTime("models/shared.mdl")==1900000000,"path time behavior changed");
        Check(fs->RemoveSearchPath(first.string().c_str()),"remove search path failed");
        Check(Read(fs,"models/shared.mdl")=="SECOND","changed winner not re-resolved");
        fs->RemoveAllSearchPaths();
        std::ifstream list(path::u8path(argv[3]));Check(bool(list),"actual list missing");
        std::vector<std::string> resources;std::string name;
        while(std::getline(list,name))
        {
            if(!name.empty() && name.back()=='\r')name.pop_back();
            resources.push_back(name);
        }
        Check(!resources.empty(),"actual list empty");
        // The list root is separate so no fixture can win an actual lookup.
        const auto game=path::path(resources.front());
        const auto marker=game.string().find("cstrike\\");
        Check(marker!=std::string::npos,"actual list has no cstrike root");
        const auto base=game.string().substr(0,marker+7);
        fs->AddSearchPath(base.c_str(),"ACTUAL");
        for(unsigned round=0;round<3;++round)
        {
            const auto started=std::chrono::steady_clock::now();
            for(const auto& absolute:resources)
            {
                Check(absolute.starts_with(base+"\\"),"resource escaped actual root");
                struct _stat expected{};Check(!_stat(absolute.c_str(),&expected),"actual stat failed");
                const auto logical=absolute.substr(base.size()+1);
                auto file=fs->Open(logical.c_str(),"rb","ACTUAL");Check(file,"actual VFS open failed");
                Check(fs->Size(file)==unsigned(expected.st_size),"actual size differs");
                const auto count=fs->Size(file)>4?4u:unsigned(fs->Size(file));
                std::array<char,4> actual{},reference{};
                Check(fs->Read(actual.data(),int(count),file)==int(count),"actual VFS read failed");
                auto* direct=std::fopen(absolute.c_str(),"rb");Check(direct,"actual reference open failed");
                Check(std::fread(reference.data(),1,count,direct)==count,"actual reference short read");std::fclose(direct);
                Check(actual==reference,"actual consumer bytes differ");fs->Close(file);
            }
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            std::printf("actual\t%u\t%zu\t%.9f\n",round,resources.size(),seconds);std::fflush(stdout);
        }
        // Measure the consumer alone after all reference validation above.
        // Keep metadata reference opens out of this interval.
        std::vector<std::string> logicalNames;
        for(const auto& absolute:resources)logicalNames.push_back(absolute.substr(base.size()+1));
        for(unsigned round=0;round<3;++round)
        {
            const auto started=std::chrono::steady_clock::now();
            std::uint64_t bytes{};
            for(const auto& logical:logicalNames)
            {
                auto file=fs->Open(logical.c_str(),"rb","ACTUAL");Check(file,"measured VFS open failed");
                bytes+=fs->Size(file);fs->Close(file);
            }
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            std::printf("opens\t%u\t%zu\t%.9f\t%llu\n",round,resources.size(),seconds,
                static_cast<unsigned long long>(bytes));std::fflush(stdout);
        }
        fs->RemoveAllSearchPaths();fs->Unmount();FreeLibrary(module);
        std::printf("complete\t%u\n",checks);return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
