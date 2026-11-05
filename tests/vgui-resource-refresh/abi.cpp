// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <cstdio>
#include <VGUI/IResourceRefresh.h>
#include <VGUI/IScheme2.h>
#include <VGUI/ISurface2.h>

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    HMODULE module=LoadLibraryA(argv[1]);if(!module){std::printf("LoadLibrary error=%lu\n",GetLastError());return 3;}
    auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));
    unsigned checks{},failures{};
    auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}};
    check(factory!=nullptr,"factory");if(!factory)return 4;
    auto* api=static_cast<vgui::IResourceRefresh*>(factory(VGUI_RESOURCE_REFRESH_INTERFACE_VERSION,nullptr));
    check(api!=nullptr,"separate versioned interface");if(!api)return 5;
    vgui::ResourceRefreshRequest request{sizeof(request),nullptr,0,nullptr,0,nullptr};
    unsigned long long token=123;
    check(api->BeginUpdate(request,token)==vgui::ResourceRefreshResult::InvalidRequest && !token,"uninitialized host fails without mutation");
    check(api->LastError() && *api->LastError(),"LastError virtual slot");
    check(api->CommitUpdate(123)==vgui::ResourceRefreshResult::StaleToken,"Commit virtual slot");
    check(api->AbortUpdate(123)==vgui::ResourceRefreshResult::StaleToken,"Abort virtual slot");
    vgui::ResourceFontDefinition font{sizeof(font),"Arial",22,400,0,0,0,0,0xffff};
    check(api->StageFont(123,1,font)==vgui::ResourceRefreshResult::StaleToken,"Stage virtual slot");
    check(api->CommitRemoval(123)==vgui::ResourceRefreshResult::StaleToken,"Removal virtual slot");
    check(factory(VGUI_SCHEME2_INTERFACE_VERSION,nullptr)!=nullptr,"existing Scheme2 interface retained");
    check(factory(VGUI_SURFACE2_INTERFACE_VERSION,nullptr)!=nullptr,"existing Surface2 interface retained");
    FreeLibrary(module);
    std::printf("abi checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
