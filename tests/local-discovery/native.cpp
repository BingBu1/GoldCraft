#include "local_discovery.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>

namespace fs=std::filesystem;
unsigned checks=0;
void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
std::string read(const fs::path& path){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    char bytes[513]{};DWORD count=0;const bool ok=ReadFile(file,bytes,sizeof(bytes),&count,nullptr)!=FALSE;CloseHandle(file);
    return ok?std::string(bytes,count):std::string{};
}
fs::path record(const fs::path& root){
    if(!fs::exists(root))return {};
    for(const auto& entry:fs::directory_iterator(root))if(entry.path().extension()==L".gclink"&&entry.path().stem().wstring().starts_with(std::to_wstring(GetCurrentProcessId())+L"-"))return entry.path();
    return {};
}
template<class F>bool wait(F condition){for(int i=0;i<80;++i){if(condition())return true;Sleep(25);}return false;}
bool private_acl(const fs::path& path){
    PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr;PACL acl=nullptr;
    if(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&sd))return false;
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;void* raw=nullptr;
    bool ok=owner&&acl&&acl->AceCount==1&&GetSecurityDescriptorControl(sd,&control,&revision)&&(control&SE_DACL_PROTECTED)&&GetAce(acl,0,&raw);
    if(ok){auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);ok=ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE&&EqualSid(owner,&ace->SidStart)&&(ace->Mask&FILE_ALL_ACCESS)==FILE_ALL_ACCESS;}
    LocalFree(sd);return ok;
}
bool complete(const std::string& text){
    if(text.empty()||text.size()>512||text.back()!='\n')return false;
    return std::count(text.begin(),text.end(),'\n')==9&&text.starts_with("GOLDCRAFT_LOCAL_V1\n30\n");
}
int wmain(int argc,wchar_t** argv){
    try{
        check(argc==3,"arguments");fs::path root=argv[1],junction=argv[2];fs::create_directory(root);
        const auto secure=root/L"private";_wputenv_s(L"GOLDCRAFT_DISCOVERY_DIR",secure.c_str());
        goldcraft::EndpointConfig config;config.session.fill(0xaa);config.token.fill(0xbb);
        goldcraft::local_discovery::Publisher publisher;publisher.start(config,41000);
        check(wait([&]{return !record(secure).empty();}),"initial publish");const auto file=record(secure);
        check(private_acl(secure),"directory protected ACL");check(private_acl(file),"file protected ACL");
        auto text=read(file);check(complete(text),"complete schema");check(text.ends_with("\n0\n0\n"),"disconnected initial state");
        check(text.find("\n41000\n")!=std::string::npos,"actual endpoint port");
        publisher.state((std::numeric_limits<std::uint64_t>::max)(),true);
        check(wait([&]{return read(file).ends_with("\n18446744073709551615\n1\n");}),"unsigned world and busy update");
        bool atomic=true;unsigned samples=0;
        for(unsigned i=1;i<=120;++i){publisher.state(i,(i&1)!=0);const auto value=read(file);if(!value.empty()){++samples;atomic=atomic&&complete(value);}Sleep(5);}
        check(samples==120&&atomic,"atomic replace never exposes partial record");
        publisher.state(99,false);check(wait([&]{return read(file).ends_with("\n99\n0\n");}),"released availability");
        const auto stable=fs::last_write_time(file);Sleep(250);check(stable==fs::last_write_time(file),"unchanged state avoids rewrite");
        const auto stopped=std::chrono::steady_clock::now();publisher.stop();
        check(std::chrono::steady_clock::now()-stopped<std::chrono::seconds(1),"bounded shutdown");check(!fs::exists(file),"shutdown removes own record");
        publisher.start(config,41001);check(wait([&]{return read(file).find("\n41001\n")!=std::string::npos;}),"restart republishes port");publisher.stop();
        check(!fs::exists(file),"restart cleanup");
        // Test stale cleanup with private known-format and unknown records.
        const auto stale=secure/L"4294967295-1.gclink",unknown=secure/L"unknown.gclink";
        {std::ofstream(stale)<<"stale";std::ofstream(unknown)<<"unknown";}
        // Inherited test files are protected explicitly before passing the private gate.
        for(const auto& path:{stale,unknown}){
            PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr;PACL acl=nullptr;
            check(GetNamedSecurityInfoW(const_cast<LPWSTR>(secure.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&sd)==ERROR_SUCCESS,"read fixture ACL");
            check(SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,owner,nullptr,acl,nullptr)==ERROR_SUCCESS,"protect fixture ACL");LocalFree(sd);
        }
        publisher.start(config,41000);check(wait([&]{return !record(secure).empty();}),"prune startup publish");
        check(!fs::exists(stale),"dead PID record removed");check(fs::exists(unknown),"unknown record preserved");publisher.stop();
        const auto public_dir=root/L"public";fs::create_directory(public_dir);_wputenv_s(L"GOLDCRAFT_DISCOVERY_DIR",public_dir.c_str());
        publisher.start(config,41000);Sleep(200);publisher.stop();check(record(public_dir).empty(),"insecure directory rejected");
        _wputenv_s(L"GOLDCRAFT_DISCOVERY_DIR",junction.c_str());publisher.start(config,41000);Sleep(200);publisher.stop();
        check(record(junction).empty(),"junction rejected");
        std::cout<<"PASS "<<checks<<" native discovery checks; no endpoint transport or game execution.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
