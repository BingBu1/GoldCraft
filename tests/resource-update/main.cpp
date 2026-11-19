// SPDX-License-Identifier: GPL-3.0-or-later
// Production updater against the reviewed, original x86 filesystem DLL.
#include <metahook.h>
#include <IFileSystem.h>
#include "resource_update.hpp"
#include "resource_overlay.hpp"
#include "resource_cache.hpp"
#include "http_download_store.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <iomanip>
#include <stdexcept>
#include <thread>

namespace update=goldcraft::resource_update;
namespace overlay=goldcraft::resource_overlay;
namespace cache=goldcraft::resource_cache;
namespace fsys=std::filesystem;
using goldcraft::resource_sync::Entry;
using namespace std::chrono_literals;
namespace {
int checks{};
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Hook {void** slot;void* old;};
hook_t* HookVft(void* object,int,int slot,void* replacement,void** original){
    auto** at=*reinterpret_cast<void***>(object)+slot;DWORD before{};
    if(!VirtualProtect(at,sizeof(void*),PAGE_READWRITE,&before))return nullptr;
    auto* token=new Hook{at,*at};*original=*at;*at=replacement;
    DWORD ignored{};VirtualProtect(at,sizeof(void*),before,&ignored);return reinterpret_cast<hook_t*>(token);
}
BOOL UnHook(hook_t* token){
    auto* hook=reinterpret_cast<Hook*>(token);DWORD before{};
    if(!VirtualProtect(hook->slot,sizeof(void*),PAGE_READWRITE,&before))return FALSE;
    *hook->slot=hook->old;DWORD ignored{};VirtualProtect(hook->slot,sizeof(void*),before,&ignored);delete hook;return TRUE;
}
std::vector<std::uint8_t> Disk(const fsys::path& path){
    std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
Entry Identity(std::uint32_t size,const std::string& text){
    Entry out{true,size};Check(text.size()==64,"SHA argument");
    for(unsigned i=0;i<32;++i)out.sha256[i]=std::uint8_t(std::stoul(text.substr(i*2,2),nullptr,16));return out;
}
bool EqualVfs(IFileSystem* fs,const std::string& name,const std::vector<std::uint8_t>& expected){
    const auto handle=fs->Open(name.c_str(),"rb");if(!handle)return false;
    std::vector<std::uint8_t> got(fs->Size(handle));const int n=fs->Read(got.data(),int(got.size()),handle);fs->Close(handle);
    return n==int(got.size())&&got==expected;
}
void Pump(update::Session& session){
    const auto until=std::chrono::steady_clock::now()+12s;
    do {session.Pump();auto phase=session.GetStatus().phase;
        if(phase==update::Phase::ready||phase==update::Phase::failed)return;
        std::this_thread::sleep_for(2ms);
    } while(std::chrono::steady_clock::now()<until);
    throw std::runtime_error("session timeout");
}
void Ready(update::Session& session){Pump(session);if(session.GetStatus().phase!=update::Phase::ready)
    throw std::runtime_error("not ready: "+session.GetStatus().error);++checks;}

// Exercise one v4 admission across real WinHTTP, the bounded server store,
// and the final VFileSystem009 consumer. A held response owns the whole budget.
void StorePressure(update::Session& session, cache::Cache& storage, IFileSystem* fs,
                   const std::string& name, const Entry& next, const Entry& third,
                   const std::vector<std::uint8_t>& bytes, const std::vector<std::uint8_t>& old,
                   const std::vector<std::uint8_t>& newer) {
    namespace http = goldcraft::http;
    http::Store store(next.size);
    http::Server server;
    Check(server.start("127.0.0.1", 0, [&](const auto& key) { return store.request(key); }), "start real FastDL");
    const auto url = "http://127.0.0.1:" + std::to_string(server.port()) + "/goldcraft/";
    const std::string heldName = "models/held.mdl";
    auto pin = [&](const Entry& entry) {
        store.publish({{heldName, next.size, next}, {name, entry.size, entry}});
        auto result = std::async(std::launch::async, [&] { return store.request(heldName); });
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        auto load = store.take();
        while (!load && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
            load = store.take();
        }
        Check(load && load->name == heldName, "pin the registered response");
        *load->bytes = bytes;
        store.finish(load, true, &next.sha256);
        auto held = result.get();
        Check(held.status == http::LookupStatus::available && held.body && *held.body == bytes, "hold real cache bytes");
        return held.body;
    };
    auto waitBusy = [&](std::uint64_t initial) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (store.stats().capacity_waits < initial + 4 && std::chrono::steady_clock::now() < deadline) {
            session.Pump();
            if (session.GetStatus().phase == update::Phase::failed)
                throw std::runtime_error("v4 abandoned a pending registered resource: " + session.GetStatus().error);
            std::this_thread::sleep_for(2ms);
        }
        Check(store.stats().capacity_waits >= initial + 4, "four real capacity responses");
        Check(session.GetStatus().phase == update::Phase::downloading, "same v4 admission still downloading");
    };
    auto serve = [&](const auto& content, const Entry& serverEntry) {
        const auto deadline = std::chrono::steady_clock::now() + 12s;
        for (;;) {
            if (auto load = store.take()) {
                Check(load->name == name, "only requested resource loaded");
                *load->bytes = content;
                store.finish(load, true, &serverEntry.sha256);
            }
            session.Pump();
            const auto phase = session.GetStatus().phase;
            if (phase == update::Phase::ready || phase == update::Phase::failed) return;
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("v4 admission timeout");
            std::this_thread::sleep_for(2ms);
        }
    };
    auto held = pin(next);
    Check(session.Begin(1, url, {{name, next}}), "begin pressure admission once");
    waitBusy(0);
    Check(!fsys::exists(storage.Path(name, next)) && EqualVfs(fs, name, old), "pending download remains unmounted");
    held.reset();
    serve(bytes, next);
    Check(session.GetStatus().phase == update::Phase::ready && session.GetStatus().downloaded == 1,
          "original admission recovered");
    Check(Disk(storage.Path(name, next)) == bytes && EqualVfs(fs, name, bytes), "verified disk and final VFS bytes");
    Check(session.Reset() && EqualVfs(fs, name, old), "restore previous VFS");

    held = pin(third);
    const auto initial = store.stats().capacity_waits;
    Check(session.Begin(2, url, {{name, third}}), "begin cancellable admission");
    waitBusy(initial);
    Check(session.Reset() && session.GetStatus().phase == update::Phase::idle, "cancel busy admission");
    held.reset();
    Check(!fsys::exists(storage.Path(name, third)) && EqualVfs(fs, name, old), "cancelled version stays unmounted");

    // Keep a valid server resource at a different SHA from the client manifest.
    store.publish({{name, next.size, next}});
    Check(session.Begin(3, url, {{name, third}}), "begin wrong-SHA admission");
    serve(bytes, next);
    Check(session.GetStatus().phase == update::Phase::failed && !fsys::exists(storage.Path(name, third)) &&
          EqualVfs(fs, name, old), "wrong SHA rejected without a mount");
    Check(session.Reset(), "reset integrity failure");
    store.publish({});
    Check(session.Begin(4, url, {{name, third}}), "begin missing-file admission");
    Pump(session);
    Check(session.GetStatus().phase == update::Phase::failed && !fsys::exists(storage.Path(name, third)) &&
          EqualVfs(fs, name, old), "unlisted resource still rejected");
    Check(session.Reset(), "reset missing resource");

    store.publish({{name, third.size, third}});
    Check(session.Begin(5, url, {{name, third}}), "fresh admission");
    serve(newer, third);
    Check(session.GetStatus().phase == update::Phase::ready && session.GetStatus().downloaded == 1 &&
          Disk(storage.Path(name, third)) == newer && EqualVfs(fs, name, newer), "only fresh verified bytes mounted");
    Check(store.stats().failures == 0 && store.stats().reserved == next.size, "bounded server cache throughout");
    std::cout << "{\"fastdlRequests\":" << server.stats().requests
              << ",\"capacityWaits\":" << store.stats().capacity_waits << "}\n";
}
BY_HANDLE_FILE_INFORMATION FileInfo(const fsys::path& path){
    auto h=CreateFileA(path.string().c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    Check(h!=INVALID_HANDLE_VALUE,"file info open");BY_HANDLE_FILE_INFORMATION info{};Check(GetFileInformationByHandle(h,&info),"file info read");CloseHandle(h);return info;
}
bool SameFile(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
}
// Exercise actual captured corruption, including same-size images and WAVs.
// Only the expected identities are supplied; all replacement bodies must come
// through the production HTTP worker, never from a repair-package payload.
int Batch(const fsys::path& binary,const fsys::path& base,const std::string& url){
    std::ifstream manifest(base/"manifest.txt");
    std::vector<update::Resource> resources;
    std::string name,sha;std::uint32_t size{};
    while(manifest>>std::quoted(name)>>size>>sha)resources.push_back({name,Identity(size,sha)});
    Check(manifest.eof()&&!resources.empty(),"complete batch manifest");
    const auto module=LoadLibraryA(binary.string().c_str());Check(module,"batch filesystem module");
    const auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));Check(factory,"batch filesystem factory");
    auto* fs=static_cast<IFileSystem*>(factory("VFileSystem009",nullptr));Check(fs,"batch filesystem interface");
    fs->Mount();fs->AddSearchPath((base/"game").string().c_str(),"GAME");
    mh_interface_t interfaces{};interfaces.FileSystem=fs;metahook_api_t api{};api.VFTHook=HookVft;api.UnHook=UnHook;
    Check(overlay::Install(&api,&interfaces),"batch production overlay install");
    auto verify=[&](const update::Resource& resource,bool expected){
        std::vector<std::uint8_t> bytes;
        Check(overlay::Read(resource.name.c_str(),bytes),"batch actual consumer read");
        cache::Hash hash;hash.Update(bytes);
        Check((bytes.size()==resource.expected.size&&hash.Finish()==resource.expected.sha256)==expected,
              expected?"consumer must read repaired bytes":"corruption must reproduce before update");
    };
    for(const auto& resource:resources)verify(resource,false);
    {
        update::Session session((base/"cache").string());
        auto run=[&]{
            const auto until=std::chrono::steady_clock::now()+180s;
            std::size_t reported=~std::size_t{};
            for(;;){
                session.Pump();const auto& status=session.GetStatus();
                if(status.downloaded!=reported){reported=status.downloaded;std::cout<<"downloaded "<<reported<<"/"<<status.total<<std::endl;}
                if(status.phase==update::Phase::failed)throw std::runtime_error(status.error);
                if(status.phase==update::Phase::ready)break;
                if(std::chrono::steady_clock::now()>=until)throw std::runtime_error("batch automatic update timeout");
                std::this_thread::sleep_for(2ms);
            }
        };
        Check(session.Begin(1,url,resources),"batch begin");run();
        Check(session.GetStatus().downloaded==resources.size(),"all corrupt resources automatically downloaded");
        Check(session.Changed().size()==resources.size(),"all wrong consumers detected");
        for(const auto& resource:resources)verify(resource,true);
        // No game owners in this isolated host; cancelling must restore all
        // original bad samples, then the verified cache must repair without IO.
        Check(session.Reset(),"batch cancel");
        for(const auto& resource:resources)verify(resource,false);
        Check(session.Begin(2,url,resources),"batch cached retry");run();
        Check(session.GetStatus().downloaded==0&&session.GetStatus().cached==resources.size(),"cached retry needs no redownload");
        for(const auto& resource:resources)verify(resource,true);
        Check(session.Reset(),"batch final reset");
    }
    overlay::Shutdown();fs->RemoveAllSearchPaths();fs->Unmount();FreeLibrary(module);
    std::cout<<"PASS captured-corruption "<<resources.size()<<" files, "<<checks<<" checks\n";
    return 0;
}
int Scale(const fsys::path& binary,const fsys::path& base,const std::string& url){
    fsys::create_directories(base/"game/models");
    const auto module=LoadLibraryA(binary.string().c_str());Check(module,"scale filesystem module");
    const auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));Check(factory,"scale filesystem factory");
    auto* fs=static_cast<IFileSystem*>(factory("VFileSystem009",nullptr));Check(fs,"scale filesystem interface");
    fs->Mount();fs->AddSearchPath((base/"game").string().c_str(),"GAME");
    mh_interface_t interfaces{};interfaces.FileSystem=fs;metahook_api_t api{};api.VFTHook=HookVft;api.UnHook=UnHook;
    Check(overlay::Install(&api,&interfaces),"scale production overlay install");
    std::vector<std::uint8_t> body(4096,0x5a), changed(4096,0xa5);
    auto identity=[](const auto& bytes){cache::Hash hash;hash.Update(bytes);return Entry{true,std::uint32_t(bytes.size()),hash.Finish()};};
    const auto expected=identity(body), replacement=identity(changed);
    auto write=[&](const std::string& name,const auto& bytes){
        std::ofstream file(base/"game"/name,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());Check(bool(file),"scale source write");
    };
    std::vector<update::Resource> resources;
    for(const unsigned count:{1000u,3000u,6000u}){
        while(resources.size()<count){
            auto name="models/scale"+std::to_string(resources.size())+".mdl";
            write(name,body);resources.push_back({std::move(name),expected});
        }
        update::Session session((base/"cache").string());
        auto run=[&](const char* label,std::uint64_t bytes){
            const auto until=std::chrono::steady_clock::now()+120s;
            do{
                session.Pump();const auto& status=session.GetStatus();
                if(status.phase==update::Phase::failed)throw std::runtime_error(status.error);
                if(status.phase==update::Phase::ready)break;
                Check(std::chrono::steady_clock::now()<until,"scale session deadline");
                std::this_thread::sleep_for(2ms);
            }while(true);
            const auto& s=session.GetStatus();
            Check(s.hashedBytes==bytes,"scale only changed source bodies are hashed");
            Check(s.downloaded==0&&s.checked==count,"scale complete without HTTP");
            Check(s.profile.openReader.calls==2*count,"scale resolves every VFS name again in the final pass");
            Check(s.leased==count&&s.profile.leaseOpen.calls==count,"scale reuses verified leases");
            std::cout<<"{\"files\":"<<count<<",\"run\":\""<<label<<"\",\"elapsedMs\":"<<s.elapsedMs
                <<",\"pumpWorkMs\":"<<s.pumpWorkMs<<",\"hashedBytes\":"<<s.hashedBytes
                <<",\"openCalls\":"<<s.profile.openReader.calls<<",\"openMs\":"<<s.profile.openReader.totalMs
                <<",\"hashMs\":"<<s.profile.hashUpdate.totalMs<<",\"reused\":"<<s.reused<<"}"<<std::endl;
            Check(session.Commit(),"scale commit");
        };
        Check(session.Begin(1,url,resources),"scale cold begin");run("cold",std::uint64_t(count)*body.size());
        Check(session.Begin(2,url,resources),"scale warm begin");run("warm",0);
        write(resources.back().name,changed);resources.back().expected=replacement;
        Check(session.Begin(3,url,resources),"scale single change begin");run("one-change",changed.size());
        Check(session.Reset(),"scale reset");
        write(resources.back().name,body);resources.back().expected=expected;
    }
    overlay::Shutdown();fs->RemoveAllSearchPaths();fs->Unmount();FreeLibrary(module);
    return 0;
}
// Admission-shaped measurement: the live manifest size, the live count of wrong
// game-root files already repaired in the content cache, several GAME search
// paths ahead of the winner, and one Pump per 31 Hz host frame. It measures the
// updater only; it is not a client, engine or gameplay timing.
int Join(const fsys::path& binary,const fsys::path& base,const std::string& url){
    constexpr unsigned count=3345,wrong=445;constexpr std::size_t bytes=16384;
    fsys::create_directories(base/"game/models");
    const auto module=LoadLibraryA(binary.string().c_str());Check(module,"join filesystem module");
    const auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));Check(factory,"join filesystem factory");
    auto* fs=static_cast<IFileSystem*>(factory("VFileSystem009",nullptr));Check(fs,"join filesystem interface");
    fs->Mount();
    for(unsigned i=0;i<5;++i){
        const auto empty=base/("empty"+std::to_string(i));fsys::create_directories(empty);
        fs->AddSearchPath(empty.string().c_str(),"GAME");
    }
    fs->AddSearchPath((base/"game").string().c_str(),"GAME");
    mh_interface_t interfaces{};interfaces.FileSystem=fs;metahook_api_t api{};api.VFTHook=HookVft;api.UnHook=UnHook;
    Check(overlay::Install(&api,&interfaces),"join production overlay install");
    std::vector<update::Resource> resources;
    {
        cache::Cache storage((base/"cache").string());
        for(unsigned i=0;i<count;++i){
            std::vector<std::uint8_t> body(bytes,std::uint8_t(i*131u+7u));
            std::memcpy(body.data(),&i,sizeof(i));
            cache::Hash hash;hash.Update(body);
            const Entry expected{true,std::uint32_t(body.size()),hash.Finish()};
            auto name="models/join"+std::to_string(i)+".mdl";
            if(i<wrong){
                Check(storage.Store(name,expected,body),"join repaired cache object");
                for(auto& value:body)value=std::uint8_t(~value);
            }
            std::ofstream file(base/"game"/name,std::ios::binary|std::ios::trunc);
            file.write(reinterpret_cast<const char*>(body.data()),body.size());Check(bool(file),"join source write");
            resources.push_back({std::move(name),expected});
        }
    }
    update::Session session((base/"cache").string());
    auto run=[&](const char* label){
        using Clock=std::chrono::steady_clock;
        const auto frame=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/31.0));
        const auto until=Clock::now()+180s;
        for(;;){
            const auto begin=Clock::now();
            session.Pump();const auto phase=session.GetStatus().phase;
            if(phase==update::Phase::failed)throw std::runtime_error(session.GetStatus().error);
            if(phase==update::Phase::ready)break;
            Check(Clock::now()<until,"join session deadline");
            while(Clock::now()<begin+frame)std::this_thread::yield();
        }
        const auto& s=session.GetStatus();
        Check(s.downloaded==0&&s.checked==count,"join complete without HTTP");
        std::vector<std::uint8_t> got;
        Check(overlay::Read(resources.front().name.c_str(),got)&&got.size()==bytes&&got[4]==std::uint8_t(7),"join consumer reads repaired body");
        std::cout<<"{\"run\":\""<<label<<"\",\"elapsedMs\":"<<s.elapsedMs<<",\"pumps\":"<<s.pumps
            <<",\"pumpWorkMs\":"<<s.pumpWorkMs<<",\"pumpMaxMs\":"<<s.pumpMaxMs
            <<",\"openCalls\":"<<s.profile.openReader.calls<<",\"openMs\":"<<s.profile.openReader.totalMs
            <<",\"sourceMs\":"<<s.profile.backgroundSource.totalMs
            <<",\"cacheOpenCalls\":"<<s.profile.cacheOpen.calls<<",\"cacheOpenMs\":"<<s.profile.cacheOpen.totalMs
            <<",\"mountMs\":"<<s.profile.mount.totalMs<<",\"hashedBytes\":"<<s.hashedBytes
            <<",\"cached\":"<<s.cached<<",\"reused\":"<<s.reused<<",\"leased\":"<<s.leased
            <<",\"leaseOpenCalls\":"<<s.profile.leaseOpen.calls<<",\"leaseOpenMs\":"<<s.profile.leaseOpen.totalMs
            <<",\"leaseCheckCalls\":"<<s.profile.leaseCheck.calls<<",\"leaseCheckMs\":"<<s.profile.leaseCheck.totalMs<<"}"<<std::endl;
        Check(session.Commit(),"join commit");
    };
    // A replaced binding still gets a new independent read lease.
    auto passes=[&](unsigned remapped,unsigned mounts){
        const auto& s=session.GetStatus();
        Check(s.profile.openReader.calls==2*count&&s.profile.mount.calls==mounts,"join final pass shape");
        Check(s.leased==count-remapped&&s.profile.leaseOpen.calls==count+remapped,"join only reopens changed bindings");
    };
    Check(session.Begin(1,url,resources),"join first begin");run("first-join");
    Check(session.GetStatus().cached==wrong,"join repaired every wrong root file from cache");passes(wrong,1);
    // The host restores the base view when a connection ends.
    overlay::Clear();
    Check(session.Begin(2,url,resources),"join reconnect begin");run("rejoin");
    Check(session.GetStatus().hashedBytes==0,"join reconnect reads no bodies");passes(wrong,1);
    // A map change on the same server keeps the committed overlay mounted.
    Check(session.Begin(3,url,resources),"join map change begin");run("map-change");
    Check(session.GetStatus().hashedBytes==0,"join map change reads no bodies");passes(0,0);
    Check(session.Reset(),"join reset");
    overlay::Shutdown();fs->RemoveAllSearchPaths();fs->Unmount();FreeLibrary(module);
    std::cout<<"PASS join "<<checks<<" checks\n";
    return 0;
}
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4);
    if(argc==5&&std::string(argv[4])=="join"){
        try{return Join(fsys::absolute(argv[1]),fsys::absolute(argv[2]),argv[3]);}
        catch(const std::exception& e){std::cerr<<"FAIL join: "<<e.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[4])=="scale"){
        try{return Scale(fsys::absolute(argv[1]),fsys::absolute(argv[2]),argv[3]);}
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[4])=="captured-corruption"){
        try{return Batch(fsys::absolute(argv[1]),fsys::absolute(argv[2]),argv[3]);}
        catch(const std::exception& error){std::cerr<<"FAIL captured-corruption: "<<error.what()<<'\n';return 1;}
    }
    if(argc!=9)return 2;
    const fsys::path binary=fsys::absolute(argv[1]),base=fsys::absolute(argv[2]);
    const std::string url=argv[3],mode=argv[4],name=argv[5];
    try {
        const auto next=Identity(std::uint32_t(std::stoul(argv[6])),argv[7]);
        const auto third=Identity(next.size,argv[8]);const auto bytes=Disk(base/"new.bin"),newer=Disk(base/"third.bin"),old=Disk(base/"old.bin");
        Check(bytes.size()==next.size,"body fixture size");
        auto module=LoadLibraryA(binary.string().c_str());Check(module,"load actual filesystem module");
        auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));Check(factory,"filesystem factory");
        auto* fs=static_cast<IFileSystem*>(factory("VFileSystem009",nullptr));Check(fs,"filesystem interface");
        fs->Mount();fs->AddSearchPath((base/"game").string().c_str(),"GAME");fs->AddSearchPath((base/"game").string().c_str(),"GAMECONFIG");
        if(fsys::exists(base/"fallback"))fs->AddSearchPath((base/"fallback").string().c_str(),"GAME");
        mh_interface_t interfaces{};interfaces.FileSystem=fs;metahook_api_t api{};api.VFTHook=HookVft;api.UnHook=UnHook;
        Check(overlay::Install(&api,&interfaces),"production identity checked overlay install");
        {
        update::Session session((base/"cache").string());cache::Cache storage((base/"cache").string());
        if(mode=="store-pressure"){
            StorePressure(session,storage,fs,name,next,third,bytes,old,newer);
        }else if(mode=="same"||mode=="update"||mode=="pak"||mode=="reuse"||mode=="same-generation"||mode=="old-hardlink"||mode=="final-change"){
            Check(EqualVfs(fs,name,mode=="same"?bytes:old),"baseline source winner");
            Check(session.Begin(3,url,{{name,next}}),"manifest accepted");
            if(mode=="final-change"){
                const auto until=std::chrono::steady_clock::now()+5s;
                while(session.GetStatus().phase!=update::Phase::verifying&&std::chrono::steady_clock::now()<until){session.Pump();std::this_thread::sleep_for(2ms);}
                Check(session.GetStatus().phase==update::Phase::verifying,"final verification reached");
                std::ofstream out(storage.Path(name,next),std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(old.data()),old.size());out.close();
                Pump(session);Check(session.GetStatus().phase==update::Phase::failed,"final VFS mutation rejected");
                Check(EqualVfs(fs,name,old),"failed final verification removes mount");
            }else{
                Ready(session);Check(EqualVfs(fs,name,bytes),"final consumer reads manifest body");
                Check(session.GetStatus().downloaded==(mode=="same"?0:1),"download accounting");
                if(mode=="same") {
                    Check(!fsys::exists(base/"cache"),"matching source makes no cache");
                    Check(session.GetStatus().hashedBytes==next.size,"final VFS reuses unchanged source digest");
                    Check(session.GetStatus().reused==1&&session.GetStatus().leased==1,"final VFS pass reuses the unchanged identity digest");
                    Check(session.Reset()&&session.Begin(4,url,{{name,next}}),"warm reconnect setup");Ready(session);
                    Check(session.GetStatus().hashedBytes==0&&session.GetStatus().reused==2&&session.GetStatus().leased==1,"warm reconnect avoids all repeated body reads");
                    auto reader=overlay::OpenReader(name.c_str());Check(bool(reader),"open actual source for lease");
                    auto deferred=reader->DeferredSource();Check(bool(deferred),"engine winner borrowed");
                    auto lease=deferred->Lease();Check(bool(lease),"independent OS lease acquired");
                    Check(deferred->Matches(*lease),"same exact file and range match");
                    if(name=="sprites/packed.spr") {
                        auto alternate=overlay::OpenReader("sprites/alternate.spr");
                        Check(bool(alternate)&&alternate->Size()==reader->Size(),"second equal-length PAK entry");
                        auto other=alternate->DeferredSource();
                        Check(bool(other)&&!other->Matches(*lease),"different span of same PAK cannot use the lease");
                        Check(EqualVfs(fs,"sprites/alternate.spr",newer),"second PAK entry has different bytes");
                    }
                    std::vector<std::uint8_t> tail(113),head(71);
                    bool readGood{};
                    std::thread worker([&]{readGood=lease->ReadAt(next.size-113,tail);});
                    Check(reader->ReadAt(0,head)==71,"engine read alongside independent worker");worker.join();
                    Check(readGood&&std::equal(tail.begin(),tail.end(),bytes.end()-113)&&
                        std::equal(head.begin(),head.end(),bytes.begin()),"PAK or loose offsets stay independent");
                }
                else Check(Disk(storage.Path(name,next))==bytes,"cache committed content");
                if(mode=="reuse"){
                    session.Reset();Check(EqualVfs(fs,name,old),"reset clears overlay");
                    Check(session.Begin(4,url,{{name,next}}),"reuse manifest");Ready(session);
                    Check(session.GetStatus().cached==1&&session.GetStatus().downloaded==0,"verified cache reused without download");
                    Check(EqualVfs(fs,name,bytes),"reused cache final VFS");
                }
                if(mode=="same-generation"){
                    Check(session.Begin(3,url+"third/",{{name,third}}),"same generation changed SHA accepted");Ready(session);
                    Check(session.GetStatus().downloaded==1&&EqualVfs(fs,name,newer),"generation number does not mask changed content");
                    Check(fsys::exists(storage.Path(name,next)),"previous content addressed cache remains independent");
                }
                if(mode=="old-hardlink"){
                    Check(Disk(base/"old-linked-source.bin")==old,"old linked source preserved");
                    Check(SameFile(FileInfo(base/"game"/name),FileInfo(base/"old-linked-source.bin")),"original hardlink identity preserved");
                    Check(!SameFile(FileInfo(storage.Path(name,next)),FileInfo(base/"old-linked-source.bin")),"new cache independent file identity");
                }
            }
        }else if(mode=="shadow"){
            const auto game=base/"game", shadow=base/"higher-priority";
            fsys::create_directories((shadow/name).parent_path());
            std::ofstream replacement(shadow/name,std::ios::binary);
            replacement.write(reinterpret_cast<const char*>(newer.data()),newer.size());replacement.close();
            Check(fs->RemoveSearchPath(game.string().c_str()),"switch to alternate path for memoization");
            fs->AddSearchPath(shadow.string().c_str(),"GAME");
            Check(session.Begin(1,url,{{name,third}}),"prime digest cache for alternate VFS winner");
            Ready(session);
            Check(session.Reset()&&fs->RemoveSearchPath(shadow.string().c_str()),"restore original mount");
            fs->AddSearchPath(game.string().c_str(),"GAME");
            Check(session.Begin(2,url,{{name,next}}),"shadow begin");
            const auto until=std::chrono::steady_clock::now()+5s;
            while(session.GetStatus().phase!=update::Phase::verifying&&std::chrono::steady_clock::now()<until){session.Pump();std::this_thread::sleep_for(2ms);}
            Check(session.GetStatus().phase==update::Phase::verifying,"shadow final pass reached");
            Check(fs->RemoveSearchPath(game.string().c_str()),"remove original lookup path while preserving its read lease");
            fs->AddSearchPath(shadow.string().c_str(),"GAME");
            fs->AddSearchPath(game.string().c_str(),"GAME");
            Check(EqualVfs(fs,name,newer),"actual VFS winner changes despite pinned original bytes");
            Pump(session);
            Check(session.GetStatus().phase==update::Phase::failed,"final verification rejects higher-priority stale winner");
            const auto& s=session.GetStatus();
            Check(s.leased==0&&s.profile.leaseCheck.calls==1&&s.profile.leaseOpen.calls==2,
                  "different VFS winner falls back to independent reopen");
            Check(s.hashedBytes==2ull*next.size&&s.reused==0,"rejected lease requires fresh SHA despite a cached digest");
        }else if(mode=="download-releases-lease"){
            const std::string companion="models/unchanged.mdl";
            const auto path=base/"game"/companion;
            Check(EqualVfs(fs,companion,bytes),"companion matches before transfer");
            Check(session.Begin(1,url,{{companion,next},{name,next}}),"mixed manifest accepted");
            const auto until=std::chrono::steady_clock::now()+5s;
            while(session.GetStatus().phase!=update::Phase::verifying&&std::chrono::steady_clock::now()<until) {
                session.Pump();
                std::this_thread::sleep_for(2ms);
            }
            Check(session.GetStatus().phase==update::Phase::verifying&&session.GetStatus().downloaded==1,
                  "transfer completed after releasing first-pass leases");
            {
                std::ofstream replacement(path,std::ios::binary|std::ios::trunc);
                replacement.write(reinterpret_cast<const char*>(old.data()),old.size());
                Check(bool(replacement),"can write previous winner after download begins");
            }
            Pump(session);
            Check(session.GetStatus().phase==update::Phase::failed&&session.GetStatus().leased==0,
                  "post-transfer verification rejects changed companion.");
            Check(EqualVfs(fs,name,old),"failed verification rolls back the downloaded mount");
            std::ofstream restore(path,std::ios::binary|std::ios::trunc);
            restore.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            Check(bool(restore),"restore verified fixture bytes");
        }else if(mode=="lease"){
            // Pin proved bytes while independently resolving the final winner.
            const auto path=(base/"game"/name).string();
            const auto write=[&]{return CreateFileA(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);};
            Check(session.Begin(1,url,{{name,next}}),"lease begin");
            const auto until=std::chrono::steady_clock::now()+5s;
            while(session.GetStatus().phase!=update::Phase::verifying&&std::chrono::steady_clock::now()<until){session.Pump();std::this_thread::sleep_for(2ms);}
            Check(session.GetStatus().phase==update::Phase::verifying,"lease final pass reached");
            Check(write()==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION,"proved source cannot be opened for writing");
            Check(!DeleteFileA(path.c_str())&&GetLastError()==ERROR_SHARING_VIOLATION,"proved source cannot be deleted");
            Check(!MoveFileExA(path.c_str(),(path+".moved").c_str(),0),"proved source cannot be renamed");
            Ready(session);
            const auto& s=session.GetStatus();
            Check(s.profile.openReader.calls==2&&s.profile.leaseCheck.calls==1,"final pass searches the VFS again under the read lease");
            Check(s.leased==1&&s.profile.leaseOpen.calls==1,"final pass avoids the duplicate OS reopen");
            Check(EqualVfs(fs,name,bytes),"leased source is the consumer body");
            const auto writer=write();Check(writer!=INVALID_HANDLE_VALUE,"lease released at ready");CloseHandle(writer);
        }else if(mode=="mtime-change"||mode=="replace-source"){
            const auto path=base/"game"/name;
            Check(session.Begin(1,url,{{name,next}}),"stamp baseline begin");Ready(session);
            Check(session.Reset(),"stamp baseline reset");
            const auto time=fsys::last_write_time(path);
            if(mode=="replace-source"){
                auto replacement=path;replacement+=".replacement";
                std::ofstream out(replacement,std::ios::binary);out.write(reinterpret_cast<const char*>(old.data()),old.size());out.close();
                Check(MoveFileExA(replacement.string().c_str(),path.string().c_str(),MOVEFILE_REPLACE_EXISTING),"atomic source replacement");
            }else{
                std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(old.data()),old.size());out.close();
            }
            fsys::last_write_time(path,time);
            Check(session.Begin(1,url,{{name,next}}),"same size and timestamp changed source begin");Ready(session);
            Check(session.GetStatus().downloaded==1&&session.GetStatus().hashedBytes>=next.size,"changed source cannot reuse old digest");
            Check(EqualVfs(fs,name,bytes),"changed source repaired through actual overlay");
            session.Reset();
            std::ofstream restore(path,std::ios::binary|std::ios::trunc);restore.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());restore.close();
        }else if(mode=="transaction"){
            Check(!session.Commit(),"cannot commit before final verification");
            Check(session.Begin(1,url,{{name,next}}),"first version accepted");Ready(session);
            Check(session.Changed()==std::vector<std::string>{name},"actual stale VFS recorded");
            Check(session.Commit(),"commit after consumer refresh");
            Check(!session.Commit(),"double commit rejected");
            Check(session.Reset()&&EqualVfs(fs,name,bytes),"reset preserves committed version");
            Check(session.Begin(1,url,{{name,next}}),"same committed version accepted");Ready(session);
            Check(session.GetStatus().downloaded==0&&session.Changed().empty(),"matching overlay avoids transfer and cache retirement");
            Check(EqualVfs(fs,name,bytes),"matching previous mount retained");
            Check(session.Reset()&&EqualVfs(fs,name,bytes),"cancel matching restores prior version");
            Check(session.Begin(1,url+"third/",{{name,third}}),"next same generation SHA accepted");Ready(session);
            Check(EqualVfs(fs,name,newer),"new version visible before consumer refresh");
            Check(session.Reset()&&EqualVfs(fs,name,bytes),"consumer failure rolls back committed prior version");
            Check(!session.Begin(1,url,{{"models/x.dll",next}}),"bad manifest rejected with prior mount");
            Check(EqualVfs(fs,name,bytes),"bad manifest keeps prior mount");
            Check(session.Begin(2,url,{}),"removed resource accepted");Ready(session);
            Check(session.Changed()==std::vector<std::string>{name},"removed overlay retires prior consumers");
            Check(EqualVfs(fs,name,old),"removed resource reads original source");
            Check(session.Reset()&&EqualVfs(fs,name,bytes),"cancel removal restores prior overlay");
        }else if(mode=="rollback-failure"){
            Check(storage.Store(name,next,bytes),"prepare previous cache");
            Check(overlay::Begin(storage.Root(),{{name,storage.Path(name,next)}}),"previous cache mounted");
            Check(session.Begin(1,url,{}),"empty next manifest");Ready(session);
            Check(fsys::remove(storage.Path(name,next)),"external removal of rollback source");
            Check(!session.Reset()&&session.GetStatus().phase==update::Phase::failed,"missing rollback source cannot report success");
            Check(!session.GetStatus().error.empty(),"rollback error reported");
            Check(storage.Store(name,next,bytes),"restore external rollback source");
            Check(session.Reset()&&EqualVfs(fs,name,bytes),"retry restores valid previous generation");
        }else if(mode=="bad-sha"){
            Check(session.Begin(2,url,{{name,next}}),"bad server body setup");Pump(session);
            Check(session.GetStatus().phase==update::Phase::failed&&!session.GetStatus().error.empty(),"bad SHA fails session");
            Check(!fsys::exists(storage.Path(name,next)),"bad SHA never commits cache");Check(EqualVfs(fs,name,old),"bad SHA keeps old VFS source");
        }else if(mode=="cancel"){
            Check(session.Begin(2,url,{{name,next}}),"cancel setup");
            const auto until=std::chrono::steady_clock::now()+5s;
            while(!fsys::exists(base/"http-started")&&std::chrono::steady_clock::now()<until){session.Pump();std::this_thread::sleep_for(2ms);}
            Check(fsys::exists(base/"http-started"),"actual transfer started");
            auto inFlight=session.GetDownloadProgress();
            while(!inFlight.received&&std::chrono::steady_clock::now()<until){
                session.Pump();std::this_thread::sleep_for(2ms);inFlight=session.GetDownloadProgress();
            }
            Check(inFlight.resourceIndex==0&&inFlight.size==next.size&&inFlight.received>0&&
                inFlight.received<inFlight.size&&session.GetStatus().downloaded==0,
                "slow HTTP exposes received bytes before the completed file count advances");
            session.Reset();Check(session.GetStatus().phase==update::Phase::idle&&session.Resources().empty(),"reset discards old state");
            Check(!session.GetDownloadProgress().size,"cancelled HTTP file no longer reports progress");
            Check(session.Begin(2,url+"fresh/",{{name,third}}),"new same-generation connection accepted after cancel");Ready(session);
            Check(!session.GetDownloadProgress().size,"final VFS success exposes no in-flight file");
            Check(EqualVfs(fs,name,newer),"only new connection body mounted");
            Check(!fsys::exists(storage.Path(name,next)),"old cancelled result never commits");
            Check(fsys::exists(storage.Path(name,third)),"new connection committed");
        }else if(mode=="cache-hardlink"){
            const auto target=fsys::path(storage.Path(name,next));fsys::create_directories(target.parent_path());
            fsys::create_hard_link(base/"old-linked-source.bin",target);const auto before=FileInfo(base/"old-linked-source.bin");
            Check(SameFile(before,FileInfo(target)),"cache hardlink setup");
            Check(storage.Store(name,next,bytes),"atomic store succeeds over hardlink destination");
            Check(Disk(base/"old-linked-source.bin")==old,"hardlink source bytes preserved");
            Check(SameFile(before,FileInfo(base/"old-linked-source.bin")),"source file ID preserved");
            Check(!SameFile(before,FileInfo(target))&&Disk(target)==bytes,"destination replaced independently");
            Check(!storage.Store(name,next,old),"incorrect digest cannot replace valid cached target");
            Check(Disk(target)==bytes,"failed store leaves existing content intact");
        }else if(mode=="invalid"){
            Check(overlay::SafeName("resource/csol/wall/v1/WallUpgradeScheme.res"),"published wall scheme accepted");
            Check(overlay::SafeName("resource/csol/hud/v1/CsolHudScheme.res"),"published HUD scheme accepted");
            Check(overlay::SafeName("resource/csol/zecounter/v1/ZeCounterScheme.res"),"published counter scheme accepted");
            Check(!overlay::SafeName("resource/csol/zecounter/v2/ZeCounterScheme.res"),"unpublished counter revision rejected");
            Check(!overlay::SafeName("resource/csol/zecounter/v1/Other.res"),"arbitrary counter schemes rejected");
            Check(!overlay::SafeName("resource/csol/wall/v1/OtherScheme.res"),"arbitrary schemes remain rejected");
            for(const std::string bad:std::vector<std::string>{"../x.mdl","models/CON.mdl","models/x.mdl:ads","models/x.mdl.","models//x.mdl","models/.x.mdl","models/x.dll","resource/x.cfg","C:/x.mdl",std::string("models/")+std::string(260,'x')+".mdl"}){
                Check(!session.Begin(1,url,{{bad,next}}),"invalid manifest path rejected");
                Check(session.GetStatus().phase==update::Phase::failed,"invalid path fails before transfer");
            }
            Check(!session.Begin(1,url,{{name,next},{name,third}}),"conflicting manifest entries rejected");
            Check(!session.Begin(1,url,{{name,next},{"MODELS/UPDATE.MDL",third}}),"case collision rejected");
            Check(!session.Begin(0,url,{{name,next}}),"zero generation rejected");
            Check(!session.Begin(1,"file:///C:/tmp",{{name,next}}),"non-http URL rejected");
            const auto longName=std::string("models/")+std::string(110,'x')+".mdl";
            Check(overlay::SafeName(longName),"logical name within logical policy");
            Check(!storage.Store(longName,next,bytes),"cache path beyond host MAX_PATH rejected");
            Check(!fsys::exists(base/"cache"),"invalid identities created no files");
        }else throw std::runtime_error("unknown mode");
        session.Reset();
        }
        overlay::Shutdown();fs->RemoveAllSearchPaths();fs->Unmount();FreeLibrary(module);
        std::cout<<"PASS "<<mode<<" "<<checks<<" checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<mode<<" "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
