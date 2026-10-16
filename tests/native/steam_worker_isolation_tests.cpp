#include "steam_async_transport.hpp"
#include "steam_worker_environment.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace goldcraft::steam_async;
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    const std::filesystem::path trace=argv[3];
    SetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_TRACE",trace.c_str());
    SetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_KEEP",L"42");
    for(const auto* key:game_environment_keys)SetEnvironmentVariableW(key,L"10");
    const wchar_t fixture[]=L"=H:=H:\\MineCraft_Cs\0Path=keep\0sTeAmApPiD=10\0SteamGameId=10\0OTHER=42\0\0";
    const auto filtered=worker_environment(fixture);
    const std::wstring_view first(filtered.data());assert(first==L"=H:=H:\\MineCraft_Cs");
    for(auto p=filtered.data();*p;p+=std::wcslen(p)+1)assert(!game_environment_entry(p));
    assert(filtered[filtered.size()-1]==0&&filtered[filtered.size()-2]==0);
    const auto cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(cancel);
    for(int scenario=0;scenario<3;++scenario){
        std::filesystem::remove(trace);
        SetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_APPID",scenario==2?L"10":L"0");
        SetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_OFFLINE",scenario==1?L"1":L"0");
        HelperStatus status;
        {
            ProcessBackend backend(argv[1],argv[2],cancel,&status);
            assert(backend.initialize()==(scenario!=2));
            assert(status.initialization==(scenario==2?10:0));
            assert(status.app_id==(scenario==2?10u:0u));
            if(scenario!=2){
                assert(status.logged_on==(scenario==1?0:1));
                Avatar a;a.id=123;a.size=1;backend.avatar(a);
                assert(a.width==1&&a.height==1&&!std::memcmp(a.rgba.data(),"RGBA",4));
                VoicePacket audio;backend.start_voice();assert(backend.capture(audio,false,0)==VoiceResult::ok);
                assert(audio.compressed==3&&!std::memcmp(audio.data.data(),"abc",3));backend.stop_voice();
                // Exercise actual worker-only polling and diagnostic publication.
                Sleep(1050);backend.poll();assert(status.samples>=2&&status.failures==0);
                assert(status.logged_on==(scenario==1?1:0));
                assert(status.sample_tick_ms>0&&GetTickCount64()-status.sample_tick_ms<1000);
            }
        }
        std::ifstream input(trace);std::string events((std::istreambuf_iterator<char>(input)),{});
        assert(events.find("isolated_environment")!=std::string::npos);
        assert(events.find("release_own_user\nrelease_pipe")!=std::string::npos);
        assert(events.find("FORBIDDEN")==std::string::npos);
        if(scenario==2)assert(events.find("start\n")==std::string::npos);
        for(const auto* key:game_environment_keys){wchar_t value[16]{};assert(GetEnvironmentVariableW(key,value,16)&&!std::wcscmp(value,L"10"));}
    }
    CloseHandle(cancel);
    std::cout<<"Steam worker isolation: parent identity preserved, child identity removed, app-context rejection, offline diagnostics, actual production helper APIs and owned cleanup passed\n";
}
