#pragma once
#include "goldcraft/endpoint.hpp"
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace goldcraft::local_discovery {
// Discovery grants access only to the existing authenticated loopback transport.
// No GCBind credential is stored here. All filesystem work stays off HUD_Frame.
class Publisher {
    std::jthread worker_;
    std::atomic<std::uint64_t> world_{0};
    std::atomic<bool> busy_{false};
    static std::wstring user_sid() {
        HANDLE token=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw ProtocolError("discovery token unavailable");
        DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
        std::vector<std::uint8_t> storage(size);
        const bool ok=GetTokenInformation(token,TokenUser,storage.data(),size,&size)!=FALSE;CloseHandle(token);
        if(!ok)throw ProtocolError("discovery user unavailable");
        LPWSTR value=nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,&value))throw ProtocolError("discovery SID unavailable");
        std::wstring result(value);LocalFree(value);return result;
    }
    static std::uint64_t creation(HANDLE process) {
        FILETIME start{},exit{},kernel{},user{};
        if(!GetProcessTimes(process,&start,&exit,&kernel,&user))return 0;
        return (std::uint64_t(start.dwHighDateTime)<<32)|start.dwLowDateTime;
    }
    static bool no_links(const std::filesystem::path& path) {
        for(auto current=path;!current.empty();){
            const auto attributes=GetFileAttributesW(current.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
            const auto parent=current.parent_path();if(parent==current)break;current=parent;
        }
        return true;
    }
    static bool private_path(const std::filesystem::path& path,const std::wstring& sid) {
        if(!no_links(path))return false;
        PSID owner=nullptr;PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
        if(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
            &owner,nullptr,&acl,nullptr,&descriptor)!=ERROR_SUCCESS)return false;
        PSID expected=nullptr;bool ok=ConvertStringSidToSidW(sid.c_str(),&expected)!=FALSE;
        SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;
        ok=ok&&owner&&EqualSid(owner,expected)&&acl&&acl->AceCount==1&&
            GetSecurityDescriptorControl(descriptor,&control,&revision)&&(control&SE_DACL_PROTECTED);
        void* raw=nullptr;
        if(ok)ok=GetAce(acl,0,&raw)&&static_cast<ACE_HEADER*>(raw)->AceType==ACCESS_ALLOWED_ACE_TYPE;
        if(ok){auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);ok=EqualSid(&ace->SidStart,expected)&&
            !(ace->Header.AceFlags&INHERIT_ONLY_ACE)&&(ace->Mask&FILE_ALL_ACCESS)==FILE_ALL_ACCESS;}
        if(expected)LocalFree(expected);if(descriptor)LocalFree(descriptor);return ok;
    }
    static std::filesystem::path directory(const std::wstring& sid) {
        std::filesystem::path result;
        if(const auto* override_path=_wgetenv(L"GOLDCRAFT_DISCOVERY_DIR"))result=override_path;
        else if(const auto* local=_wgetenv(L"LOCALAPPDATA"))result=std::filesystem::path(local)/L"GoldCraft-client-links-v1";
        if(result.empty()||!result.is_absolute()||result.native().starts_with(L"\\\\"))throw ProtocolError("discovery directory must be local and absolute");
        result=result.lexically_normal();
        const auto drive=GetDriveTypeW(result.root_path().c_str());
        if(drive!=DRIVE_FIXED&&drive!=DRIVE_REMOVABLE&&drive!=DRIVE_RAMDISK)throw ProtocolError("discovery directory must be on a local drive");
        if(!no_links(result.parent_path()))throw ProtocolError("discovery parent unavailable or redirected");
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        const auto sddl=L"O:"+sid+L"D:P(A;OICI;FA;;;"+sid+L")";
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))throw ProtocolError("discovery security unavailable");
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};
        const bool made=CreateDirectoryW(result.c_str(),&attributes)!=FALSE;const auto error=GetLastError();LocalFree(descriptor);
        if((!made&&error!=ERROR_ALREADY_EXISTS)||!private_path(result,sid))throw ProtocolError("discovery directory is not private");
        return result;
    }
    static std::string hex(const Key& key){
        constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(32);
        for(const auto value:key){result+=digits[value>>4];result+=digits[value&15];}return result;
    }
    static bool publish(const std::filesystem::path& path,const std::wstring& sid,const std::string& text){
        if(!private_path(path.parent_path(),sid))return false;
        auto temporary=path;temporary+=L".tmp";
        if(GetFileAttributesW(temporary.c_str())!=INVALID_FILE_ATTRIBUTES){
            if(!private_path(temporary,sid)||!DeleteFileW(temporary.c_str()))return false;
        }
        PSECURITY_DESCRIPTOR descriptor=nullptr;const auto sddl=L"O:"+sid+L"D:P(A;;FA;;;"+sid+L")";
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,&attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);LocalFree(descriptor);
        if(file==INVALID_HANDLE_VALUE)return false;
        DWORD written=0;const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();CloseHandle(file);
        const bool moved=ok&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!moved)DeleteFileW(temporary.c_str());return moved;
    }
    static void prune(const std::filesystem::path& directory,const std::wstring& sid){
        unsigned count=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            if(++count>256)break;
            const auto& path=entry.path();if(path.extension()!=L".gclink"||!private_path(path,sid))continue;
            try{
                const auto stem=path.stem().wstring();const auto separator=stem.find(L'-');if(separator==std::wstring::npos)continue;
                const auto pid=std::stoull(stem.substr(0,separator)),created=std::stoull(stem.substr(separator+1));
                if(!pid||pid>0xffffffffull||stem!=std::to_wstring(pid)+L"-"+std::to_wstring(created))continue;
                HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,static_cast<DWORD>(pid));
                bool stale=false;
                if(process){const auto actual=creation(process);stale=WaitForSingleObject(process,0)==WAIT_OBJECT_0||(actual&&actual!=created);CloseHandle(process);}
                else stale=GetLastError()==ERROR_INVALID_PARAMETER;
                if(stale)DeleteFileW(path.c_str());
            }catch(...){/* Unknown files and inaccessible processes are preserved. */}
        }
    }
public:
    ~Publisher(){stop();}
    // OS-selected port and system RNG also work with ordinary MetaHook shortcuts.
    void start(const EndpointConfig& config,std::uint16_t port) {
        stop();world_=0;busy_=false;
        worker_=std::jthread([this,config,port](std::stop_token stop){
            std::filesystem::path path;std::wstring sid;
            try{
                sid=user_sid();const auto created=creation(GetCurrentProcess());if(!created)return;
                const auto root=directory(sid);prune(root,sid);
                path=root/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(created)+L".gclink");
                const auto prefix="GOLDCRAFT_LOCAL_V1\n"+std::to_string(protocol_version)+"\n"+std::to_string(GetCurrentProcessId())+"\n"+
                    std::to_string(created)+"\n"+std::to_string(port)+"\n"+hex(config.session)+"\n"+hex(config.token)+"\n";
                std::string previous;
                while(!stop.stop_requested()){
                    const auto record=prefix+std::to_string(world_.load())+"\n"+(busy_.load()?"1\n":"0\n");
                    if(record!=previous&&publish(path,sid,record))previous=record;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }catch(...){OutputDebugStringA("[GoldCraft] local discovery unavailable; explicit launcher configuration remains usable\n");}
            if(!path.empty()&&private_path(path,sid))DeleteFileW(path.c_str());
        });
    }
    void state(std::uint64_t world,bool busy) noexcept {world_.store(world);busy_.store(busy);}
    void stop(){if(worker_.joinable()){worker_.request_stop();worker_.join();}}
};
}
