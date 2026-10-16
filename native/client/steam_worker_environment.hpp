#pragma once
#include <Windows.h>
#include <cwchar>
#include <string_view>
#include <vector>

namespace goldcraft::steam_async {
// These identify the launched game. A read-only avatar/voice helper is not
// another instance of that game and must not inherit its Steam registration.
inline constexpr const wchar_t* game_environment_keys[]{
    L"SteamAppId",L"SteamGameId",L"SteamOverlayGameId",L"SteamEnv",L"SteamClientLaunch"
};
inline bool game_environment_entry(std::wstring_view entry){
    const auto equal=entry.find(L'=');
    if(equal==std::wstring_view::npos)return false;
    for(const auto* key:game_environment_keys)
        if(equal==std::wcslen(key)&&_wcsnicmp(entry.data(),key,equal)==0)return true;
    return false;
}
inline std::vector<wchar_t> worker_environment(const wchar_t* source){
    std::vector<wchar_t> result;
    if(source)for(auto p=source;*p;p+=std::wcslen(p)+1){
        const std::wstring_view entry(p);
        if(!game_environment_entry(entry))result.insert(result.end(),p,p+entry.size()+1);
    }
    if(result.empty())result.push_back(L'\0');
    result.push_back(L'\0');return result;
}
inline bool clear_worker_game_environment(){
    for(const auto* key:game_environment_keys){
        if(!SetEnvironmentVariableW(key,nullptr)&&GetLastError()!=ERROR_ENVVAR_NOT_FOUND)return false;
    }
    return true;
}
}
