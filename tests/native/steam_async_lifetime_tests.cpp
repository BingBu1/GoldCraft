#include <Windows.h>
#include <cassert>
#include <chrono>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const auto module=LoadLibraryW(argv[1]);assert(module);
    const auto start=reinterpret_cast<int(__cdecl*)(const wchar_t*)>(GetProcAddress(module,"test_start"));
    const auto ready=reinterpret_cast<int(__cdecl*)()>(GetProcAddress(module,"test_ready"));
    const auto stop=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(module,"test_stop"));
    assert(start&&ready&&stop);assert(start(argv[2]));
    const auto deadline=GetTickCount64()+3000;
    while(!ready()){assert(GetTickCount64()<deadline);Sleep(1);}
    Sleep(50);const auto began=std::chrono::steady_clock::now();stop();
    assert(std::chrono::steady_clock::now()-began<std::chrono::milliseconds(100));
    assert(FreeLibrary(module));
    // No call into the unloaded module. The worker's final FreeLibrary and
    // thread exit must atomically release its own pin, even with blocked IPC.
    while(GetModuleHandleW(argv[1])){assert(GetTickCount64()<deadline);Sleep(1);}
    std::cout<<"Steam async lifetime: main stop returned, blocked helper cancelled, DLL unloaded without callback into freed code\n";
}
