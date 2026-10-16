#include "steam_async_transport.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace goldcraft::steam_async;
using namespace std::chrono_literals;
template<class F> void until(F test){const auto deadline=Clock::now()+2s;while(!test()){assert(Clock::now()<deadline);Sleep(1);}}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    SetEnvironmentVariableW(L"SteamAppId",L"10");SetEnvironmentVariableW(L"SteamGameId",L"10");
    SetEnvironmentVariableW(L"SteamOverlayGameId",L"10");SetEnvironmentVariableW(L"SteamEnv",L"1");
    SetEnvironmentVariableW(L"SteamClientLaunch",L"1");SetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_CHECK_ENV",L"1");
    for(bool startup:{false,true}){
        State s;auto cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::thread worker([&]{ProcessBackend backend(argv[1],startup?L"startup-block":L"avatar-block",cancel);s.run_avatars(backend);});
        if(!startup){until([&]{return s.counters.avatar_ready.load();});s.avatar(123,1);Sleep(30);}
        else Sleep(30);
        const auto began=Clock::now();
        for(unsigned i=0;i<1000;++i)assert(s.avatar(123,1)==0);
        assert(Clock::now()-began<100ms);
        s.stopping=true;SetEvent(cancel);worker.join();
        assert(Clock::now()-began<1500ms);CloseHandle(cancel);
    }
    wchar_t app_id[16]{};assert(GetEnvironmentVariableW(L"SteamAppId",app_id,16)&&!std::wcscmp(app_id,L"10"));
    {
        State s;auto cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::thread worker([&]{ProcessBackend backend(argv[1],L"voice",cancel);s.run_voice(backend);});
        s.start_voice();until([&]{return s.counters.voice_packets.load();});
        unsigned n{},r{};unsigned char data[8]{};
        assert(s.voice(true,data,8,&n,false,nullptr,0,&r,0)==VoiceResult::ok);
        assert(n==3&&data[0]==1&&data[2]==3);
        s.stop_voice();
        until([&]{return s.available(&n,&r,0)==VoiceResult::ok;});
        assert(s.voice(true,data,8,&n,false,nullptr,0,&r,0)==VoiceResult::ok);
        assert(n==3&&data[0]==99&&data[2]==3);
        until([&]{return s.available(&n,&r,0)==VoiceResult::not_recording;});
        s.stopping=true;SetEvent(cancel);worker.join();CloseHandle(cancel);
    }
    std::cout<<"Steam process transport: blocked startup/request cancellation, nonblocking UI, voice bytes, delayed post-stop tail and teardown passed\n";
}
