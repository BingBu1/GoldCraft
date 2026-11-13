// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/server/custom/settings_store.hpp"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winioctl.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace csol::scenario::custom;
namespace fs=std::filesystem;
int checks{},failures{};
void Check(bool value,const char* text)
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
void Require(bool value,const char* text){Check(value,text);if(!value)throw std::runtime_error(text);}
std::string Read(const fs::path& path)
{std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
void Write(const fs::path& path,std::string_view text)
{std::ofstream stream(path,std::ios::binary);stream<<text;Require(bool(stream),"test-only independent file written");}
fs::path Target(const fs::path& root){return root/L"addons/goldcraft/config/gameplay.json";}
StoreResult Wait(SettingsStore& store)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline)
    {if(auto result=store.Poll())return *result;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error("bounded worker did not finish within test deadline");
}
Draft Edit(const SettingsStore& store,int seconds)
{Draft draft(*store.Snapshot());Error error;Require(draft.Set(Setting::MiddaySeconds,seconds,error),"valid typed draft edit");return draft;}
void Junction(const fs::path& from,const fs::path& to)
{
    fs::create_directory(from);
    HANDLE h=CreateFileW(from.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    Require(h!=INVALID_HANDLE_VALUE,"test junction directory opened");
    const auto substitute=L"\\??\\"+to.wstring(),print=to.wstring();
    struct Buffer{DWORD tag;WORD length,reserved;WORD subOffset,subLength,printOffset,printLength;wchar_t path[2048];} buffer{};
    buffer.tag=IO_REPARSE_TAG_MOUNT_POINT;buffer.subLength=WORD(substitute.size()*2);
    buffer.printOffset=buffer.subLength+2;buffer.printLength=WORD(print.size()*2);
    std::copy(substitute.begin(),substitute.end(),buffer.path);
    std::copy(print.begin(),print.end(),buffer.path+substitute.size()+1);
    buffer.length=WORD(8+buffer.printOffset+buffer.printLength+2);
    DWORD output{};const bool made=DeviceIoControl(h,FSCTL_SET_REPARSE_POINT,&buffer,DWORD(buffer.length)+8,nullptr,0,&output,nullptr)!=FALSE;
    CloseHandle(h);Require(made,"actual junction created for path rejection test");
}
int main(int argc,char** argv)
{
    try
    {
        Require(argc==2,"explicit fresh build test root required");const auto base=fs::absolute(argv[1]);
        Require(!fs::exists(base),"test root must not exist");fs::create_directories(base);
        StoreError error;
        const auto root=base/L"normal";fs::create_directories(Target(root).parent_path());
        const auto collision=Target(root).parent_path()/(L".gameplay-"+std::to_wstring(GetCurrentProcessId())+L"-1.tmp");
        Write(collision,"preexisting independent file");
        auto store=SettingsStore::Open(root,error);Require(bool(store),error.message.c_str());
        Require(store->Revision()==1&&store->Snapshot()&&fs::exists(Target(root)),"first missing file creates validated defaults");
        Check(Read(collision)=="preexisting independent file","CREATE_NEW temporary collision never overwrites a preexisting file");
        Require(fs::remove(collision),"remove only test-owned collision sentinel after exact content check");
        Check(!SettingsStore::Open(root,error),"second store cannot share writer lock");
        const auto original=Read(store->Path());const auto first=store->Snapshot();
        Draft draft(*first);Error typed;Require(draft.Set(Setting::MiddaySeconds,181,typed)&&
            draft.SetComment(Setting::MiddaySeconds,"local explanation 中文",typed),"typed edit preserves comments");
        const auto transaction=store->PrepareEdit(1,draft,error);Require(transaction!=0,error.message.c_str());
        Check(store->Snapshot()==first&&store->Revision()==1,"candidate is not published before Poll");
        Check(!store->PrepareEdit(1,draft,error),"one outstanding transaction remains bounded until final Poll");
        const auto result=Wait(*store);if(!result.success)std::cerr<<"STORE_ERROR "<<result.error.path<<" code="<<result.error.systemCode<<" message="<<result.error.message<<'\n';Require(result.success,result.error.message.c_str());
        Check(result.transaction==transaction&&result.revision==2&&result.snapshot==store->Snapshot()&&
            result.snapshot->Integer(Setting::MiddaySeconds)==181,"persisted candidate publishes exact revision once");
        Check(result.snapshot->Comment(Setting::MiddaySeconds)=="local explanation 中文"&&Read(store->Path()).find("local explanation")!=std::string::npos,
            "real serialized explanatory comments survive disk replacement");
        Check(Read(store->Path().parent_path()/L"gameplay.json.bak1")==original,"first backup retains prior complete contents");
        Check(!store->Submit(1,first,error)&&error.message=="stale settings revision","stale expected revision is rejected");
        Check(!store->Poll(),"completion consumed exactly once");
        for(int n=0;n<4;++n)
        {Require(store->PrepareEdit(store->Revision(),Edit(*store,182+n),error)!=0,error.message.c_str());Require(Wait(*store).success,"serial atomic replacement succeeds");}
        std::size_t backups{},temps{};
        for(const auto& entry:fs::directory_iterator(store->Path().parent_path()))
        {const auto name=entry.path().filename().wstring();backups+=name.starts_with(L"gameplay.json.bak");temps+=name.ends_with(L".tmp");}
        Check(backups==2&&temps==0,"only two bounded backups and no leaked owned temporary files");
        const auto committed=store->Snapshot();const auto revision=store->Revision();const auto bytes=Read(store->Path());
        HANDLE block=CreateFileW(store->Path().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Require(block!=INVALID_HANDLE_VALUE,"replacement failure uses real deny-delete file handle");
        Require(store->PrepareEdit(revision,Edit(*store,190),error)!=0,error.message.c_str());
        const auto failed=Wait(*store);CloseHandle(block);
        Check(!failed.success&&store->Revision()==revision&&store->Snapshot()==committed&&Read(store->Path())==bytes,
            "failed atomic replacement retains exact old file and snapshot");
        Write(store->Path(),"{invalid external edit}");
        Require(store->PrepareEdit(revision,Edit(*store,191),error)!=0,error.message.c_str());
        const auto external=Wait(*store);
        Check(!external.success&&Read(store->Path())=="{invalid external edit}"&&store->Snapshot()==committed,
            "external file mutation is rejected without overwrite");
        store.reset();
        Check(!SettingsStore::Open(root,error)&&!error.validation.message.empty()&&Read(Target(root))=="{invalid external edit}",
            "invalid existing startup file retains exact bytes and parser error");
        Write(Target(root),bytes);store=SettingsStore::Open(root,error);Require(bool(store),error.message.c_str());
        Check(store->Snapshot()->Integer(Setting::MiddaySeconds)==185,"restart reads real persisted configuration");
        std::uint64_t crossThread{};std::thread other([&]{StoreError local;crossThread=store->Submit(store->Revision(),first,local);});other.join();
        Check(!crossThread&&!store->Busy(),"worker/nonowner cannot submit trusted edit");
        const auto latePeer=base/L"late-hardlink.json";
        Require(CreateHardLinkW(latePeer.c_str(),store->Path().c_str(),nullptr)!=FALSE,"actual hardlink added after store startup");
        Require(store->PrepareEdit(store->Revision(),Edit(*store,198),error)!=0,error.message.c_str());
        Check(!Wait(*store).success&&Read(store->Path())==bytes&&Read(latePeer)==bytes,"worker revalidates late hardlink before any replacement");
        store.reset();
        const auto hard=base/L"hard";fs::create_directories(Target(hard).parent_path());
        Write(Target(hard),original);const auto peer=base/L"immutable-peer.json";
        Require(CreateHardLinkW(peer.c_str(),Target(hard).c_str(),nullptr)!=FALSE,"actual shared config hardlink created");
        Check(!SettingsStore::Open(hard,error)&&Read(peer)==original,"shared config is rejected without modifying linked peer");
        const auto junction=base/L"junction",outside=base/L"outside";fs::create_directory(junction);fs::create_directory(outside);
        Junction(junction/L"addons",outside);
        Check(!SettingsStore::Open(junction,error)&&!fs::exists(outside/L"goldcraft"),"directory junction rejected before child creation outside fixed root");
        Check(!SettingsStore::Open(root/L".."/L"outside",error),"explicit traversal path rejected");
        Check(!SettingsStore::Open(L"relative-game",error),"relative root rejected");
        const auto protectedRoot=base/L"never-created/steamapps/common/Half-Life";
        Check(!SettingsStore::Open(protectedRoot,error)&&!fs::exists(base/L"never-created")&&
            error.message=="protected production installation is read-only","protected root on any drive rejected before opening or creating files");
        // Backup linked to an unrelated file must reject before replacing main.
        const auto backupRoot=base/L"backup-link";fs::create_directory(backupRoot);
        auto backupStore=SettingsStore::Open(backupRoot,error);Require(bool(backupStore),error.message.c_str());
        const auto backup=backupStore->Path().parent_path()/L"gameplay.json.bak1";
        Require(CreateHardLinkW(backup.c_str(),peer.c_str(),nullptr)!=FALSE,"actual linked backup created");
        const auto old=Read(backupStore->Path());
        Require(backupStore->PrepareEdit(1,Edit(*backupStore,199),error)!=0,error.message.c_str());
        Check(!Wait(*backupStore).success&&Read(backupStore->Path())==old&&Read(peer)==original,
            "unsafe backup causes rollback before current config replacement");
        backupStore.reset();
    }
    catch(const std::exception& exception){++failures;std::cerr<<"EXCEPTION "<<exception.what()<<'\n';}
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
