// A DLL fixture for the production helper. Never loads Steam or a microphone.
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <cstdint>

namespace {
struct Object {void** table;};
std::array<void*,40> client_table{},user_table{},friends_table{},utils_table{};
Object client{client_table.data()},user{user_table.data()},friends{friends_table.data()},utils{utils_table.data()};
bool recording{};unsigned pipes{},users{},health_queries{};
void trace(const char* text){
    wchar_t path[32768]{};assert(GetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_TRACE",path,32768));
    FILE* file=nullptr;assert(!_wfopen_s(&file,path,L"ab"));std::fputs(text,file);std::fputc('\n',file);std::fclose(file);
}
unsigned env_number(const wchar_t* key){wchar_t s[32]{};GetEnvironmentVariableW(key,s,32);return static_cast<unsigned>(std::wcstoul(s,nullptr,10));}
void __fastcall forbidden(void*,void*){trace("FORBIDDEN");ExitProcess(97);}
int __fastcall create_pipe(void*,void*){assert(!pipes);++pipes;trace("create_pipe");return 71;}
bool __fastcall release_pipe(void*,void*,int pipe){assert(pipe==71&&pipes==1&&!users);--pipes;trace("release_pipe");return true;}
int __fastcall global_user(void*,void*,int pipe){assert(pipe==71&&!users);++users;trace("connect_existing_user");return 73;}
void __fastcall release_user(void*,void*,int pipe,int handle){assert(pipe==71&&handle==73&&users==1);--users;trace("release_own_user");}
void* __fastcall get_user(void*,void*,int handle,int pipe,const char* version){assert(handle==73&&pipe==71&&!std::strcmp(version,"SteamUser023"));return &user;}
void* __fastcall get_friends(void*,void*,int handle,int pipe,const char* version){assert(handle==73&&pipe==71&&!std::strcmp(version,"SteamFriends017"));return &friends;}
void* __fastcall get_utils(void*,void*,int pipe,const char* version){assert(pipe==71&&!std::strcmp(version,"SteamUtils010"));return &utils;}
bool __fastcall logged_on(void*,void*){
    trace("logged_on");const bool offline=env_number(L"GOLDCRAFT_STEAM_TEST_OFFLINE")!=0;
    return health_queries++?offline:!offline;
}
unsigned __fastcall app_id(void*,void*){trace("app_id");return env_number(L"GOLDCRAFT_STEAM_TEST_APPID");}
void __fastcall start(void*,void*){recording=true;trace("start");}
void __fastcall stop(void*,void*){recording=false;trace("stop");}
int __fastcall available(void*,void*,unsigned* n,unsigned* raw,unsigned){*n=recording?3:0;*raw=0;return recording?0:2;}
int __fastcall capture(void*,void*,bool compressed,void* data,unsigned cap,unsigned* n,bool,void*,unsigned,unsigned* raw,unsigned){
    assert(compressed&&cap>=3);std::memcpy(data,"abc",3);*n=3;*raw=0;trace("capture");return 0;
}
bool __fastcall request_info(void*,void*,std::uint64_t,bool){return true;}
int __fastcall relationship(void*,void*,std::uint64_t){return 3;}
int __fastcall avatar(void*,void*,std::uint64_t){return 7;}
bool __fastcall image_size(void*,void*,int handle,unsigned* x,unsigned* y){assert(handle==7);*x=*y=1;return true;}
bool __fastcall image_rgba(void*,void*,int handle,void* rgba,int size){assert(handle==7&&size==4);std::memcpy(rgba,"RGBA",4);return true;}
}
extern "C" void* __cdecl CreateInterface(const char* name,int*){
    assert(!std::strcmp(name,"SteamClient021"));
    for(const auto* key:{L"SteamAppId",L"SteamGameId",L"SteamOverlayGameId",L"SteamEnv",L"SteamClientLaunch"})
        assert(GetEnvironmentVariableW(key,nullptr,0)==0&&GetLastError()==ERROR_ENVVAR_NOT_FOUND);
    assert(env_number(L"GOLDCRAFT_STEAM_TEST_KEEP")==42);trace("isolated_environment");
    for(auto* table:{&client_table,&user_table,&friends_table,&utils_table})table->fill(reinterpret_cast<void*>(forbidden));
    client_table[0]=reinterpret_cast<void*>(create_pipe);client_table[1]=reinterpret_cast<void*>(release_pipe);
    client_table[2]=reinterpret_cast<void*>(global_user);client_table[4]=reinterpret_cast<void*>(release_user);
    client_table[5]=reinterpret_cast<void*>(get_user);client_table[8]=reinterpret_cast<void*>(get_friends);client_table[9]=reinterpret_cast<void*>(get_utils);
    user_table[1]=reinterpret_cast<void*>(logged_on);user_table[7]=reinterpret_cast<void*>(start);user_table[8]=reinterpret_cast<void*>(stop);
    user_table[9]=reinterpret_cast<void*>(available);user_table[10]=reinterpret_cast<void*>(capture);
    friends_table[5]=reinterpret_cast<void*>(relationship);friends_table[34]=friends_table[35]=friends_table[36]=reinterpret_cast<void*>(avatar);
    friends_table[37]=reinterpret_cast<void*>(request_info);
    utils_table[5]=reinterpret_cast<void*>(image_size);utils_table[6]=reinterpret_cast<void*>(image_rgba);utils_table[9]=reinterpret_cast<void*>(app_id);
    return &client;
}
