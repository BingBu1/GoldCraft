// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_store.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cwctype>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace csol::scenario::custom
{
namespace
{
struct Handle
{
    HANDLE value{INVALID_HANDLE_VALUE};
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(Handle&& other) noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
    Handle& operator=(Handle&& other) noexcept
    {if(this!=&other){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;}
    Handle(const Handle&)=delete;
    explicit operator bool() const{return value!=INVALID_HANDLE_VALUE;}
};
bool Fail(StoreError& error,const std::filesystem::path& path,const char* message,DWORD code=0)
{error={message,path,code,{}};return false;}
bool EqualPath(std::wstring a,std::wstring b)
{
    if(a.starts_with(L"\\\\?\\"))a.erase(0,4);
    if(b.starts_with(L"\\\\?\\"))b.erase(0,4);
    while(a.size()>3&&a.back()==L'\\')a.pop_back();
    while(b.size()>3&&b.back()==L'\\')b.pop_back();
    return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL;
}
bool ProtectedInstallation(const std::filesystem::path& path)
{
    const std::wstring_view components[]{L"steamapps",L"common",L"Half-Life"};
    unsigned matched{};
    for(const auto& component:path.relative_path())
    {
        const auto name=component.wstring();
        if(EqualPath(name,std::wstring(components[matched])))++matched;
        else matched=EqualPath(name,L"steamapps")?1u:0u;
        if(matched==3)return true;
    }
    return false;
}
bool Identity(HANDLE handle,const std::filesystem::path& path,bool directory,StoreError& error)
{
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(handle,&info))return Fail(error,path,"cannot inspect file identity",GetLastError());
    if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
        bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory)
        return Fail(error,path,"reparse point or unexpected file type rejected");
    if(!directory&&info.nNumberOfLinks!=1)return Fail(error,path,"shared hardlink file rejected");
    std::wstring actual(32768,L'\0');
    const DWORD size=GetFinalPathNameByHandleW(handle,actual.data(),DWORD(actual.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!size||size>=actual.size())return Fail(error,path,"cannot resolve file destination",GetLastError());
    actual.resize(size);
    if(!EqualPath(actual,path.wstring()))return Fail(error,path,"resolved destination differs from checked local path");
    return true;
}
struct DirectoryPin{std::filesystem::path path;Handle handle;};
bool Directory(const std::filesystem::path& path,bool create,std::vector<DirectoryPin>& pins,StoreError& error)
{
    if(create&&!CreateDirectoryW(path.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)
        return Fail(error,path,"cannot create fixed settings directory",GetLastError());
    Handle handle(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
        OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!handle)return Fail(error,path,"cannot pin settings directory",GetLastError());
    if(!Identity(handle.value,path,true,error))return false;
    pins.push_back({path,std::move(handle)});return true;
}
bool Root(const std::filesystem::path& supplied,std::filesystem::path& output,
    std::vector<DirectoryPin>& pins,StoreError& error)
{
    const auto raw=supplied.wstring();
    if(raw.size()<3||raw[1]!=L':'||(raw[2]!=L'\\'&&raw[2]!=L'/')||!std::iswalpha(raw[0])||raw.find(L'\0')!=raw.npos)
        return Fail(error,supplied,"game directory must be an absolute local drive path");
    for(const auto& part:supplied.relative_path())
        if(part==L".."||part==L"."||part.wstring().find(L':')!=std::wstring::npos)
            return Fail(error,supplied,"relative traversal or alternate stream rejected");
    output=supplied.lexically_normal();output.make_preferred();
    if(ProtectedInstallation(output))
        return Fail(error,output,"protected production installation is read-only");
    if(GetDriveTypeW(output.root_path().c_str())==DRIVE_REMOTE)
        return Fail(error,output,"network settings roots are unsupported");
    auto current=output.root_path();
    if(!Directory(current,false,pins,error))return false;
    for(const auto& part:output.relative_path())
    {if(part.empty())continue;current/=part;if(!Directory(current,false,pins,error))return false;}
    return true;
}
struct DiskRead
{
    Handle handle;
    bool exists{};
    std::string bytes;
};
bool Read(const std::filesystem::path& path,DiskRead& output,StoreError& error)
{
    DiskRead next;
    next.handle=Handle(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(!next.handle)
    {
        const auto code=GetLastError();
        if(code==ERROR_FILE_NOT_FOUND){output=std::move(next);return true;}
        return Fail(error,path,"cannot open settings file",code);
    }
    if(!Identity(next.handle.value,path,false,error))return false;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(next.handle.value,&size)||size.QuadPart<0||size.QuadPart>MaximumBytes)
        return Fail(error,path,"settings file exceeds bounded size or has no readable size",GetLastError());
    next.bytes.resize(std::size_t(size.QuadPart));DWORD read{};
    if(!next.bytes.empty()&&(!ReadFile(next.handle.value,next.bytes.data(),DWORD(next.bytes.size()),&read,nullptr)||read!=next.bytes.size()))
        return Fail(error,path,"settings read is incomplete",GetLastError());
    next.exists=true;output=std::move(next);return true;
}
struct Temp
{
    Handle handle;
    std::filesystem::path path;
    bool moved{};
    ~Temp()
    {
        if(handle&&!moved)
        {FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(handle.value,FileDispositionInfo,&remove,sizeof(remove));}
    }
};
bool Temporary(const std::filesystem::path& directory,std::string_view bytes,Temp& output,StoreError& error)
{
    static std::atomic<std::uint64_t> sequence{};
    if(bytes.size()>MaximumBytes)return Fail(error,directory,"serialized settings exceed bounded size");
    for(unsigned attempt=0;attempt<16;++attempt)
    {
        output.path=directory/(L".gameplay-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence)+L".tmp");
        output.handle=Handle(CreateFileW(output.path.c_str(),GENERIC_WRITE|DELETE|FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(output.handle)break;
        const auto code=GetLastError();
        if(code!=ERROR_FILE_EXISTS&&code!=ERROR_ALREADY_EXISTS)return Fail(error,output.path,"cannot create independent temporary settings",code);
    }
    if(!output.handle)return Fail(error,directory,"temporary filename collision limit reached");
    if(!Identity(output.handle.value,output.path,false,error))return false;
    DWORD written{};
    if(!WriteFile(output.handle.value,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size())
        return Fail(error,output.path,"temporary settings write is incomplete",GetLastError());
    if(!FlushFileBuffers(output.handle.value))return Fail(error,output.path,"cannot flush temporary settings",GetLastError());
    return true;
}
bool Rename(Temp& temporary,const std::filesystem::path& destination,bool replace,StoreError& error)
{
    const auto name=destination.wstring();
    std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t));
    auto* info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->Flags=replace?(FILE_RENAME_FLAG_REPLACE_IF_EXISTS|FILE_RENAME_FLAG_POSIX_SEMANTICS):0;info->RootDirectory=nullptr;info->FileNameLength=DWORD(name.size()*sizeof(wchar_t));
    std::copy(name.begin(),name.end(),info->FileName);
    if(!SetFileInformationByHandle(temporary.handle.value,FileRenameInfoEx,info,DWORD(storage.size())))
        return Fail(error,destination,"atomic settings replacement failed",GetLastError());
    temporary.moved=true;return true;
}
bool Persist(const std::vector<DirectoryPin>& directories,const std::filesystem::path& path,const std::string& expected,bool existed,
    std::string_view candidate,StoreError& error)
{
    for(const auto& directory:directories)
        if(!Identity(directory.handle.value,directory.path,true,error))return false;
    DiskRead current;
    if(!Read(path,current,error))return false;
    if(current.exists!=existed||(existed&&current.bytes!=expected))
        return Fail(error,path,"settings file changed outside the committed revision");
    Temp next;
    if(!Temporary(path.parent_path(),candidate,next,error))return false;
    if(existed)
    {
        const auto first=path.parent_path()/L"gameplay.json.bak1",second=path.parent_path()/L"gameplay.json.bak2";
        DiskRead oldFirst,oldSecond;
        if(!Read(first,oldFirst,error)||!Read(second,oldSecond,error))return false;
        if(oldFirst.exists)
        {
            Temp backup;
            if(!Temporary(path.parent_path(),oldFirst.bytes,backup,error)||!Rename(backup,second,true,error))return false;
        }
        Temp backup;
        if(!Temporary(path.parent_path(),current.bytes,backup,error)||!Rename(backup,first,true,error))return false;
        // Recheck the named target immediately before commit. Directory pins
        // deny rename/reparse substitution; the held file denies in-place write.
        DiskRead checked;
        if(!Read(path,checked,error)||!checked.exists||checked.bytes!=expected)
            return error.message.empty()?Fail(error,path,"settings target changed before replacement"):false;
    }
    return Rename(next,path,existed,error);
}
}

struct SettingsStore::State
{
    std::filesystem::path path;
    std::vector<DirectoryPin> directories;
    Handle lock;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::thread::id owner=std::this_thread::get_id();
    SnapshotPtr snapshot;
    std::string bytes;
    std::uint64_t revision{1},nextTransaction{};
    struct Job{std::uint64_t transaction{};SnapshotPtr snapshot;std::string bytes;};
    std::optional<Job> job;
    std::optional<StoreResult> result;
    bool outstanding{},stop{};
    void Work()
    {
        for(;;)
        {
            Job work;
            {std::unique_lock guard(mutex);wake.wait(guard,[&]{return stop||job.has_value();});
             if(!job){if(stop)return;continue;}work=std::move(*job);job.reset();}
            StoreResult done;done.transaction=work.transaction;
            try{done.success=Persist(directories,path,bytes,true,work.bytes,done.error);}
            catch(const std::exception& exception){done.error={exception.what(),path,0,{}};}
            catch(...){done.error={"settings worker failed",path,0,{}};}
            std::lock_guard guard(mutex);
            if(done.success){bytes=std::move(work.bytes);done.snapshot=std::move(work.snapshot);done.revision=revision+1;}
            else done.revision=revision;
            result=std::move(done);
        }
    }
};
SettingsStore::SettingsStore(std::unique_ptr<State> state):state_(std::move(state)){}
SettingsStore::~SettingsStore()
{
    {std::lock_guard guard(state_->mutex);state_->stop=true;}state_->wake.notify_one();
    if(state_->worker.joinable())state_->worker.join();
}
std::unique_ptr<SettingsStore> SettingsStore::Open(const std::filesystem::path& gameDirectory,StoreError& error)
{
    error={};
    try
    {
        auto state=std::make_unique<State>();std::filesystem::path root;
        if(!Root(gameDirectory,root,state->directories,error))return {};
        auto directory=root;
        for(const auto* name:{L"addons",L"goldcraft",L"config"})
        {directory/=name;if(!Directory(directory,true,state->directories,error))return {};}
        state->path=directory/L"gameplay.json";
        const auto lockPath=directory/L"gameplay.lock";
        state->lock=Handle(CreateFileW(lockPath.c_str(),GENERIC_READ,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!state->lock){Fail(error,lockPath,"another writer owns settings or lock cannot be opened",GetLastError());return {};}
        if(!Identity(state->lock.value,lockPath,false,error))return {};
        DiskRead read;
        if(!Read(state->path,read,error))return {};
        Error validation;
        if(read.exists)
        {
            if(!Parse(read.bytes,state->snapshot,validation))
            {error={"existing settings file is invalid; retained without replacement",state->path,0,std::move(validation)};return {};}
            state->bytes=std::move(read.bytes);
        }
        else
        {
            if(!Draft{}.Finish(state->snapshot,validation))
            {error={"default settings are invalid",state->path,0,std::move(validation)};return {};}
            state->bytes=Serialize(*state->snapshot);
            if(!Persist(state->directories,state->path,{},false,state->bytes,error))return {};
        }
        auto store=std::unique_ptr<SettingsStore>(new SettingsStore(std::move(state)));
        store->state_->worker=std::thread([value=store->state_.get()]{value->Work();});
        return store;
    }
    catch(const std::exception& exception){Fail(error,gameDirectory,exception.what());return {};}
}
SnapshotPtr SettingsStore::Snapshot() const
{std::lock_guard guard(state_->mutex);return state_->snapshot;}
std::uint64_t SettingsStore::Revision() const
{std::lock_guard guard(state_->mutex);return state_->revision;}
bool SettingsStore::Busy() const
{std::lock_guard guard(state_->mutex);return state_->outstanding;}
const std::filesystem::path& SettingsStore::Path() const{return state_->path;}
std::uint64_t SettingsStore::PrepareEdit(std::uint64_t expectedRevision,const Draft& draft,StoreError& error)
{
    SnapshotPtr candidate;Error validation;
    if(!draft.Finish(candidate,validation))
    {error={"typed settings draft is invalid",state_->path,0,std::move(validation)};return 0;}
    return Submit(expectedRevision,std::move(candidate),error);
}
std::uint64_t SettingsStore::Submit(std::uint64_t expectedRevision,SnapshotPtr candidate,StoreError& error)
{
    error={};
    if(std::this_thread::get_id()!=state_->owner){Fail(error,state_->path,"settings edits require the owner thread");return 0;}
    if(!candidate){Fail(error,state_->path,"settings candidate is missing");return 0;}
    SnapshotPtr checked;Error validation;
    if(!Draft(*candidate).Finish(checked,validation))
    {error={"settings candidate is invalid",state_->path,0,std::move(validation)};return 0;}
    auto bytes=Serialize(*checked);
    if(bytes.size()>MaximumBytes){Fail(error,state_->path,"serialized settings exceed bounded size");return 0;}
    std::lock_guard guard(state_->mutex);
    if(state_->outstanding||state_->stop){Fail(error,state_->path,"settings transaction is busy or closing");return 0;}
    if(expectedRevision!=state_->revision){Fail(error,state_->path,"stale settings revision");return 0;}
    if(state_->revision==UINT64_MAX||state_->nextTransaction==UINT64_MAX)
    {Fail(error,state_->path,"settings transaction identity exhausted");return 0;}
    const auto transaction=++state_->nextTransaction;
    state_->job=State::Job{transaction,std::move(checked),std::move(bytes)};
    state_->outstanding=true;state_->wake.notify_one();return transaction;
}
std::optional<StoreResult> SettingsStore::Poll()
{
    if(std::this_thread::get_id()!=state_->owner)
        return StoreResult{0,0,false,{},StoreError{"settings polling requires the owner thread",state_->path,0,{}}};
    std::lock_guard guard(state_->mutex);
    if(!state_->result)return {};
    auto result=std::move(state_->result);state_->result.reset();
    if(result->success){state_->snapshot=result->snapshot;state_->revision=result->revision;}
    state_->outstanding=false;return result;
}
}
