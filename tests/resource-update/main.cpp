// SPDX-License-Identifier: GPL-3.0-or-later
// Production updater against the reviewed, original x86 filesystem DLL.
#include <metahook.h>
#include <IFileSystem.h>
#include "resource_update.hpp"
#include "resource_overlay.hpp"
#include "resource_cache.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
BY_HANDLE_FILE_INFORMATION FileInfo(const fsys::path& path){
    auto h=CreateFileA(path.string().c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    Check(h!=INVALID_HANDLE_VALUE,"file info open");BY_HANDLE_FILE_INFORMATION info{};Check(GetFileInformationByHandle(h,&info),"file info read");CloseHandle(h);return info;
}
bool SameFile(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
}
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4);
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
        mh_interface_t interfaces{};interfaces.FileSystem=fs;metahook_api_t api{};api.VFTHook=HookVft;api.UnHook=UnHook;
        Check(overlay::Install(&api,&interfaces),"production identity checked overlay install");
        {
        update::Session session((base/"cache").string());cache::Cache storage((base/"cache").string());
        if(mode=="same"||mode=="update"||mode=="pak"||mode=="reuse"||mode=="same-generation"||mode=="old-hardlink"||mode=="final-change"){
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
                if(mode=="same")Check(!fsys::exists(base/"cache"),"matching source makes no cache");
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
            session.Reset();Check(session.GetStatus().phase==update::Phase::idle&&session.Resources().empty(),"reset discards old state");
            Check(session.Begin(2,url+"fresh/",{{name,third}}),"new same-generation connection accepted after cancel");Ready(session);
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
