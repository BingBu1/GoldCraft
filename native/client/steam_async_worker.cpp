// An isolated helper keeps Steam pipe creation/release out of the game's
// process. Those SteamClient APIs explicitly forbid concurrent API access.
#include "steam_async_state.hpp"
#include "steam_async_wire.hpp"
#include "steam_worker_environment.hpp"
#include <Windows.h>
#include <cstdlib>
#include <memory>

using namespace goldcraft::steam_async;
namespace {
template<class R,class... A> R invoke(void* object,unsigned slot,A... args){
    return reinterpret_cast<R(__thiscall*)(void*,A...)>((*static_cast<void***>(object))[slot])(object,args...);
}
bool transfer(HANDLE pipe,void* data,unsigned bytes,bool write){
    auto* p=static_cast<unsigned char*>(data);
    while(bytes){DWORD n{};const auto ok=write?WriteFile(pipe,p,bytes,&n,nullptr):ReadFile(pipe,p,bytes,&n,nullptr);
        if(!ok||!n)return false;p+=n;bytes-=n;}
    return true;
}
struct Steam {
    HMODULE module{};void* client{};int pipe{},user{};void* voice{};void* friends{};void* utils{};
    int error{};unsigned app_id{};int logged_on=-1;
    ~Steam(){
        if(client&&user)invoke<void>(client,4,pipe,user);
        if(client&&pipe)invoke<bool>(client,1,pipe);
        if(module)FreeLibrary(module);
    }
    bool initialize(const wchar_t* path){
        error=2;module=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module){error=2000+static_cast<int>(GetLastError());return false;}
        const auto create=reinterpret_cast<void*(__cdecl*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));
        error=3;if(!create)return false;error=4;client=create("SteamClient021",nullptr);if(!client)return false;
        error=5;pipe=invoke<int>(client,0);if(!pipe)return false;
        error=6;user=invoke<int>(client,2,pipe);if(!user)return false;
        voice=invoke<void*>(client,5,user,pipe,"SteamUser023");
        friends=invoke<void*>(client,8,user,pipe,"SteamFriends017");
        utils=invoke<void*>(client,9,pipe,"SteamUtils010");
        error=voice?(friends?(utils?0:9):8):7;
        if(error)return false;
        app_id=invoke<unsigned>(utils,9);
        // An optional worker must never become a second CS game/auth process.
        // Do not create/cancel tickets or weaken the actual game's validation.
        if(app_id){error=10;return false;}
        logged_on=invoke<bool>(voice,1)?1:0;return true;
    }
};
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    // Also protect manual launches; parent-side filtering prevents inherited
    // game identity from being visible even before this entry point runs.
    if(!clear_worker_game_environment())return 2;
    const auto pipe=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(wcstoull(argv[1],nullptr,10)));
    if(!pipe||pipe==INVALID_HANDLE_VALUE)return 2;
    // No new login, ticket or ownership emulation: attach to the user's real
    // running Steam session. The parent passes no account/token/password.
    Steam steam;wire::Reply reply;
    if(!steam.initialize(argv[2]))reply.result=steam.error;
    reply.app_id=steam.app_id;reply.logged_on=steam.logged_on;
    if(!transfer(pipe,&reply,sizeof(reply),true)||reply.result)return 3;
    auto image=std::make_unique<Avatar>();auto audio=std::make_unique<VoicePacket>();
    bool recording=false;
    for(;;){
        wire::Request request;
        if(!transfer(pipe,&request,sizeof(request),false))break;
        if(request.signature!=wire::magic||request.protocol!=wire::version)break;
        reply={};unsigned bytes1=0,bytes2=0;void* data1=nullptr;void* data2=nullptr;
        switch(request.op){
        case wire::Op::status:
            reply.app_id=steam.app_id;reply.logged_on=invoke<bool>(steam.voice,1)?1:0;break;
        case wire::Op::avatar:
            if(request.size>2){reply.result=1;break;}
            invoke<bool>(steam.friends,37,request.id,false);
            reply.relationship=invoke<int>(steam.friends,5,request.id);
            reply.handle=invoke<int>(steam.friends,34+request.size,request.id);
            if(reply.handle>0){
                if(invoke<bool>(steam.utils,5,reply.handle,&reply.width,&reply.height)&&
                   reply.width&&reply.height&&reply.width<=184&&reply.height<=184){
                    bytes1=reply.width*reply.height*4;data1=image->rgba.data();
                    if(!invoke<bool>(steam.utils,6,reply.handle,data1,static_cast<int>(bytes1)))reply.result=1;
                }else reply.result=1;
                if(reply.result){reply.width=reply.height=bytes1=0;reply.handle=-1;}
            }
            break;
        case wire::Op::start:invoke<void>(steam.voice,7);recording=true;break;
        case wire::Op::stop:invoke<void>(steam.voice,8);recording=false;break;
        case wire::Op::capture:{
            unsigned compressed=0,pcm=0;
            auto status=invoke<VoiceResult>(steam.voice,9,&compressed,&pcm,request.pcm?request.rate:0u);
            if(status==VoiceResult::ok){
                status=invoke<VoiceResult>(steam.voice,10,true,audio->data.data(),static_cast<unsigned>(voice_bytes),
                    &reply.compressed,request.pcm!=0,audio->raw.data(),static_cast<unsigned>(voice_bytes),&reply.pcm,request.rate);
            }
            reply.result=static_cast<int>(status);
            if(status==VoiceResult::ok&&reply.compressed<=voice_bytes&&reply.pcm<=voice_bytes){
                bytes1=reply.compressed;bytes2=reply.pcm;data1=audio->data.data();data2=audio->raw.data();
            }else {reply.compressed=reply.pcm=0;}
            break;}
        case wire::Op::quit:if(recording)invoke<void>(steam.voice,8);CloseHandle(pipe);return 0;
        default:return 4;
        }
        if(!transfer(pipe,&reply,sizeof(reply),true)||(bytes1&&!transfer(pipe,data1,bytes1,true))||
           (bytes2&&!transfer(pipe,data2,bytes2,true)))break;
    }
    if(recording)invoke<void>(steam.voice,8);
    CloseHandle(pipe);return 0;
}
