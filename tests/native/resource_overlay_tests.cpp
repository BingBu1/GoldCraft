// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <IFileSystem.h>
#include "../../native/client/resource_overlay.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/utime.h>
#include <sys/stat.h>
#include <vector>

namespace overlay = goldcraft::resource_overlay;
namespace path = std::filesystem;
int checks{}, failures{}, hookCalls{}, failHookAt{-1};
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::cerr << __LINE__ << ": " #value "\n"; } } while (false)
struct Hook { void** slot; void* old; };
hook_t* HookVft(void* object,int,int slot,void* replacement,void** original) {
    if (hookCalls++ == failHookAt) return nullptr;
    auto** at = *reinterpret_cast<void***>(object) + slot;
    DWORD before{};
    if (!VirtualProtect(at,sizeof(void*),PAGE_READWRITE,&before)) return nullptr;
    auto* token = new Hook{at,*at}; *original=*at; *at=replacement;
    DWORD ignored{}; VirtualProtect(at,sizeof(void*),before,&ignored);
    return reinterpret_cast<hook_t*>(token);
}
BOOL UnHook(hook_t* token) {
    auto* hook=reinterpret_cast<Hook*>(token);DWORD before{};
    if(!VirtualProtect(hook->slot,sizeof(void*),PAGE_READWRITE,&before)) return FALSE;
    *hook->slot=hook->old; DWORD ignored{};VirtualProtect(hook->slot,sizeof(void*),before,&ignored);
    delete hook;return TRUE;
}
void Write(const path::path& at,const std::string& bytes) {
    path::create_directories(at.parent_path());std::ofstream out(at,std::ios::binary);out.write(bytes.data(),bytes.size());
}
std::string Read(IFileSystem* fs,const char* name,const char* id=nullptr) {
    auto file=fs->Open(name,"rb",id);if(!file)return {};
    std::string bytes(fs->Size(file),'\0');const int n=fs->Read(bytes.data(),int(bytes.size()),file);fs->Close(file);
    return n==int(bytes.size())?bytes:std::string{};
}
void Pak(const path::path& at) {
    const std::string bytes="PAK-OLD";struct Entry {char name[56];int offset,size;};
    Entry entry{};std::strcpy(entry.name,"sprites/packed.spr");entry.offset=12;entry.size=int(bytes.size());
    std::ofstream out(at,std::ios::binary);out.write("PACK",4);int offset=12+int(bytes.size()),size=sizeof(entry);
    out.write(reinterpret_cast<char*>(&offset),4);out.write(reinterpret_cast<char*>(&size),4);
    out.write(bytes.data(),bytes.size());out.write(reinterpret_cast<char*>(&entry),sizeof(entry));
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const path::path binary=path::absolute(argv[1]),base=path::absolute(argv[2]);
    if(!path::exists(binary)||path::exists(base))return 3;
    const auto game=base/"game",cacheRoot=base/"cache",other=base/"other",secondRoot=base/"second";
    const auto cache=cacheRoot/"objects"/std::string(64,'a'),second=secondRoot/"objects"/std::string(64,'b');
    Write(game/"models/update.mdl","old!");Write(game/"models/unlisted.mdl","game-unlisted");
    Write(cache/"models/update.mdl","new!");Write(cache/"models/unlisted.mdl","cache-unlisted");
    Write(cache/"sprites/packed.spr","PAK-NEW");Write(cache/"sound/sound.wav","new sound");
    Write(other/"models/update.mdl","OTHER");Write(second/"models/update.mdl","next");Pak(game/"pak0.pak");
    __utimbuf64 stamp{1700000000,1700000000};_utime64((game/"models/update.mdl").string().c_str(),&stamp);
    stamp.actime=stamp.modtime=1800000000;_utime64((cache/"models/update.mdl").string().c_str(),&stamp);
    auto module=LoadLibraryA(binary.string().c_str());CHECK(module);if(!module)return 4;
    auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));CHECK(factory);
    auto* fs=static_cast<IFileSystem*>(factory("VFileSystem009",nullptr));CHECK(fs);if(!fs)return 5;
    fs->Mount();fs->AddSearchPath(game.string().c_str(),"GAME");fs->AddSearchPath(game.string().c_str(),"GAMECONFIG");
    fs->AddSearchPath(other.string().c_str(),"OTHER");
    CHECK(Read(fs,"models/update.mdl")=="old!");CHECK(Read(fs,"sprites/packed.spr")=="PAK-OLD");
    struct _stat64 oldStamp{};_stat64((game/"models/update.mdl").string().c_str(),&oldStamp);
    CHECK(fs->Size("models/update.mdl")==4);CHECK(fs->GetFileTime("models/update.mdl")==long(std::max(oldStamp.st_mtime,oldStamp.st_ctime)));
    mh_interface_t interfaces{};interfaces.FileSystem=fs;
    metahook_api_t api{};api.VFTHook=HookVft;api.UnHook=UnHook;
    failHookAt=2;CHECK(!overlay::Install(&api,&interfaces));CHECK(Read(fs,"models/update.mdl")=="old!");
    failHookAt=-1;CHECK(overlay::Install(&api,&interfaces));
    overlay::Paths entries{{"models/update.mdl",(cache/"models/update.mdl").string()},
        {"sprites/packed.spr",(cache/"sprites/packed.spr").string()}, {"sound/sound.wav",(cache/"sound/sound.wav").string()}};
    CHECK(overlay::Begin(cacheRoot.string(),entries));
    auto reader=overlay::OpenReader("models/update.mdl");CHECK(reader && reader->Size()==4);
    std::array<std::uint8_t,2> fragment{};CHECK(reader->ReadAt(2,fragment)==2);CHECK(fragment[0]=='w'&&fragment[1]=='!');
    CHECK(reader->ReadAt(0,fragment)==2);CHECK(fragment[0]=='n'&&fragment[1]=='e');CHECK(reader->ReadAt(3,fragment)==-1);reader.reset();
    CHECK(Read(fs,"models/update.mdl")=="new!");CHECK(Read(fs,"MODELS\\UPDATE.MDL")=="new!");
    CHECK(Read(fs,"models/update.mdl","GAME")=="new!");CHECK(Read(fs,"models/update.mdl","GAMEDOWNLOAD")=="new!");
    CHECK(Read(fs,"models/update.mdl","GAMECONFIG")=="old!");CHECK(Read(fs,"models/update.mdl","OTHER")=="OTHER");
    CHECK(Read(fs,"sprites/packed.spr")=="PAK-NEW");CHECK(Read(fs,"models/unlisted.mdl")=="game-unlisted");
    CHECK(fs->FileExists("sound/sound.wav"));CHECK(fs->Size("sound/sound.wav")==9);
    CHECK(fs->GetFileTime("models/update.mdl")==1800000000);
    char resolved[MAX_PATH]{};CHECK(fs->GetLocalPath("models/update.mdl",resolved,sizeof(resolved))==resolved);
    CHECK(path::path(resolved)==(cache/"models/update.mdl"));
    char tooShort[2]{'x','x'};CHECK(!fs->GetLocalPath("models/update.mdl",tooShort,2));CHECK(tooShort[0]==0);
    std::vector<std::uint8_t> bytes;CHECK(overlay::Read("models/update.mdl",bytes));CHECK(std::string(bytes.begin(),bytes.end())=="new!");
    auto write=fs->Open("models/update.mdl","wb","GAME");CHECK(write);if(write){CHECK(fs->Write("edit",4,write)==4);fs->Close(write);}
    CHECK(Read(fs,"models/update.mdl","GAMECONFIG")=="edit");CHECK(Read(fs,"models/update.mdl")=="new!");
    auto update=fs->Open("models/update.mdl","r+b","GAME");CHECK(update);if(update){CHECK(fs->Write("EDIT",4,update)==4);fs->Close(update);}
    CHECK(Read(fs,"models/update.mdl","GAMECONFIG")=="EDIT");CHECK(Read(fs,"models/update.mdl")=="new!");
    for(const char* name:{"models/plugin.dll","models/../secret.mdl","models/CON.mdl","models/foo.exe",
        "../model.mdl","models/.private.mdl","models/m.mdl:stream","models/m.mdl.","models//m.mdl","models/m.mdl ",
        "c:/models/m.mdl","models/m.mdl%20","models/m.mdl?x","cfg/autoexec.cfg","events/code.dll","shader/code.glsl"})
        CHECK(!overlay::SafeName(name));
    for(const char* name:{"models/v_test.mdl","sprites/HUD.spr","resource/cso_chn.txt","resource/font/font.ttf",
        "sound/a.wav","bgm/a.mp3","models/a.seq","materials/a.json","events/ak47.sc"})CHECK(overlay::SafeName(name));
    CHECK(!overlay::Begin(cacheRoot.string(),{{"models/plugin.dll",(cache/"models/update.mdl").string()}}));
    CHECK(!overlay::Begin(cacheRoot.string(),{{"models/update.mdl",(game/"models/update.mdl").string()}}));
    CHECK(Read(fs,"models/update.mdl")=="new!");
    CHECK(!overlay::Begin(cacheRoot.string(),{{"models/update.mdl",(cache/"models/update.mdl").string()},
        {"MODELS/UPDATE.MDL",(cache/"models/update.mdl").string()}}));
    overlay::Clear();CHECK(Read(fs,"models/update.mdl")=="EDIT");CHECK(Read(fs,"sprites/packed.spr")=="PAK-OLD");
    auto pakReader=overlay::OpenReader("sprites/packed.spr"),pakReader2=overlay::OpenReader("sprites/packed.spr");
    CHECK(pakReader && pakReader2);CHECK(pakReader->ReadAt(4,fragment)==2);CHECK(fragment[0]=='O'&&fragment[1]=='L');
    CHECK(pakReader2->ReadAt(0,fragment)==2);CHECK(fragment[0]=='P'&&fragment[1]=='A');
    CHECK(pakReader->ReadAt(5,fragment)==2);CHECK(fragment[0]=='L'&&fragment[1]=='D');pakReader.reset();pakReader2.reset();
    CHECK(!fs->FileExists("sound/sound.wav"));CHECK(!fs->Open("models/update.mdl","rb","GOLDCRAFT_RESOURCE_1"));
    CHECK(overlay::Begin(secondRoot.string(),{{"models/update.mdl",(second/"models/update.mdl").string()}}));
    CHECK(Read(fs,"models/update.mdl")=="next");
    path::remove(second/"models/update.mdl");CHECK(Read(fs,"models/update.mdl").empty());CHECK(!fs->FileExists("models/update.mdl"));
    CHECK(fs->Size("models/update.mdl")==unsigned(-1));CHECK(!fs->GetLocalPath("models/update.mdl",resolved,sizeof(resolved)));
    overlay::Shutdown();CHECK(Read(fs,"models/update.mdl")=="EDIT");CHECK(fs->GetFileTime("models/update.mdl")!=1800000000);
    interfaces.FileSystem_HL25=reinterpret_cast<IFileSystem_HL25*>(fs);CHECK(!overlay::Install(&api,&interfaces));
    fs->RemoveAllSearchPaths();fs->Unmount();FreeLibrary(module);
    std::cout<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<"}\n";return failures?1:0;
}
