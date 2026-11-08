// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <IPanel.h>
#include <ISurface.h>
#include <IVGUI2Extension.h>
#include "transfer.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <unordered_set>
#include <vector>
#include <stdexcept>

namespace parallel_download {
namespace {
metahook_api_t* api{};
mh_interface_t* interfaces{};
cl_enginefunc_t engine{};
std::unique_ptr<Pool> pool;
std::vector<std::shared_ptr<Job>> jobs;
std::unordered_set<std::string> names;
std::vector<hook_t*> hooks;
char* baseUrl{};
char* serverName{};
void (__cdecl* progress)(float){};
void (__cdecl* primaryProgress)(const char*,int,float){};
void (__cdecl* statusText)(const char*){};
void (__cdecl* closeProgress)(){};
void (__cdecl* command)(const char*){};
int (__thiscall* resetOriginal)(void*){};
void* queueOriginal{};
void* countOriginal{};
void* tickOriginal{};
unsigned completed{},failed{},settled{};
std::uint64_t serial{};
bool running{};
IVGUI2Extension* extension{};
vgui::ISurface* surface{};
vgui::IPanel* panel{};
vgui::HFont font{};
unsigned uiPaints{}, counterDraws{};
bool uiReported{};

template<class F> auto FS(F f){
    if(interfaces->FileSystem_HL25)return f(interfaces->FileSystem_HL25);
    return f(interfaces->FileSystem);
}
void Reset(){
    if(pool)pool->Cancel();
    jobs.clear();names.clear();completed=failed=settled=0;running=false;
}
int __fastcall ResetHook(void* self,void*){Reset();return resetOriginal(self);}
int __cdecl Queue(const char* value){
    try{
        if(!value||!std::strcmp(serverName,"local"))return 0;
        std::string name=value;
        std::replace(name.begin(),name.end(),'\\','/');
        if(!ValidPath(name)||jobs.size()>=32768){engine.Con_Printf("ParallelDownload: rejected path or full queue\n");return 0;}
        auto key=name;for(auto& c:key)if(c>='A'&&c<='Z')c+='a'-'A';
        if(!names.insert(key).second)return 0;
        auto job=std::make_shared<Job>();job->name=name;
        const auto length=strnlen(baseUrl,128);
        if(!length||length==128)throw std::runtime_error("invalid download URL");
        job->url.assign(baseUrl,length);if(job->url.back()!='/')job->url+='/' ;
        // Native paths are plain relative names. Escape spaces before HTTP.
        for(char c:name){if(c==' ')job->url+="%20";else job->url+=c;}
        jobs.push_back(std::move(job));
        return 0;
    }catch(const std::exception& e){engine.Con_Printf("ParallelDownload: cannot queue: %s\n",e.what());return 0;}
}
int __cdecl Count(){return static_cast<int>(jobs.size()-settled);}
bool Prepare(Job& job){
    const auto slash=job.name.find_last_of('/');
    if(slash!=std::string::npos){const auto parent=job.name.substr(0,slash);FS([&](auto* fs){fs->CreateDirHierarchy(parent.c_str(),"GAMEDOWNLOAD");});}
    const auto temp=job.name+".gc-download-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial)+".part";
    auto handle=FS([&](auto* fs){return fs->Open(temp.c_str(),"wb","GAMEDOWNLOAD");});
    if(!handle)return false;
    FS([&](auto* fs){fs->Close(handle);});
    std::array<char,4096> local{};
    if(!FS([&](auto* fs){return fs->GetLocalPath(temp.c_str(),local.data(),int(local.size()));})){FS([&](auto* fs){fs->RemoveFile(temp.c_str(),"GAMEDOWNLOAD");});return false;}
    job.temporary=std::filesystem::path(local.data());
    job.destination=job.temporary.parent_path()/std::filesystem::path(job.name).filename();
    return true;
}
void TickImpl(){
    if(jobs.empty())return;
    if(!running){
        if(!pool)pool=std::make_unique<Pool>();
        running=true;
        engine.Con_Printf("ParallelDownload: start %u files, 8 workers\n",unsigned(jobs.size()));
        uiReported=false;counterDraws=0;
        engine.Con_Printf("ParallelDownload: UI surface=%p panel=%p paints=%u draws=%u\n",surface,panel,uiPaints,counterDraws);
    }
    unsigned outstanding=0;
    for(const auto& j:jobs)if(j->submitted&&!j->done.load(std::memory_order_acquire))++outstanding;
    unsigned prepared=0;
    for(auto& j:jobs){
        if(outstanding>=8||prepared>=8)break;
        if(j->submitted)continue;
        j->submitted=true;++prepared;
        if(FS([&](auto* fs){return fs->FileExists(j->name.c_str());})){j->success=true;j->done=true;}
        else if(!Prepare(*j)||!pool->Submit(j)){j->error=ERROR_WRITE_FAULT;j->done=true;}
        else ++outstanding;
    }
    unsigned newCompleted=0,newFailed=0;
    const Job* shown=nullptr;
    for(auto& j:jobs){
        if(!j->done.load(std::memory_order_acquire)){if(!shown&&j->active)shown=j.get();continue;}
        if(j->success&&!j->temporary.empty()){
            // No replace: downloads must never overwrite a user file that
            // appeared during this batch. Atomic rename hides partial bytes.
            const bool exists=FS([&](auto* fs){return fs->FileExists(j->name.c_str());});
            if(exists||MoveFileExW(j->temporary.c_str(),j->destination.c_str(),MOVEFILE_WRITE_THROUGH)){
                if(!exists)j->temporary.clear();
            }else{j->success=false;j->error=GetLastError();}
        }
        if(j->success)++newCompleted;else ++newFailed;
    }
    if(completed!=newCompleted||failed!=newFailed){
        completed=newCompleted;failed=newFailed;
        engine.Con_Printf("ParallelDownload: %u/%u complete, %u failed\n",completed,unsigned(jobs.size()),failed);
    }
    settled=completed+failed;
    if(shown)statusText(shown->name.c_str());
    const auto bytes=shown?shown->received.load():0;
    const auto length=shown?shown->length.load():0;
    progress(length?std::min(1.0f,float(bytes)/float(length)):float(settled)/float(jobs.size()));
    // Native ContinueProgressBar may pump the loading UI. Do not access a
    // batch after that call; a cancel can retire it during UI dispatch.
    if(settled==jobs.size()){
        for(const auto& j:jobs)if(!j->success)engine.Con_Printf("ParallelDownload: %s HTTP=%u error=%u attempts=%u; native fallback\n",j->name.c_str(),j->status,j->error,j->attempts);
        Reset();closeProgress();command("retry\n");
    }else{
        primaryProgress("Http",int(settled),0.0f);
    }
}
void __cdecl Tick(){
    try{TickImpl();}catch(const std::exception& e){engine.Con_Printf("ParallelDownload: %s; native fallback\n",e.what());Reset();closeProgress();command("retry\n");}
}
vgui::VPANEL FindLoading(vgui::VPANEL root,unsigned depth=0){
    if(!root||depth>8||!panel->IsVisible(root))return 0;
    const char* name=panel->GetName(root);
    const char* cls=panel->GetClassName(root);
    if((name&&std::strstr(name,"LoadingDialog"))||(cls&&std::strstr(cls,"LoadingDialog")))return root;
    const auto count=std::min(panel->GetChildCount(root),128);
    for(int i=0;i<count;++i)if(auto found=FindLoading(panel->GetChild(root,i),depth+1))return found;
    return 0;
}
class UI final:public IVGUI2Extension_BaseUICallbacks {
public:
    int GetAltitude()const override{return 0;}
    void Initialize(CreateInterfaceFn* factories,int count)override{
        for(int i=0;i<count;++i){
            if(!surface)surface=static_cast<vgui::ISurface*>(factories[i](VGUI_SURFACE_INTERFACE_VERSION,nullptr));
            if(!panel)panel=static_cast<vgui::IPanel*>(factories[i](VGUI_PANEL_INTERFACE_VERSION,nullptr));
        }
        // BaseUI's factory array exposes the engine surface, but does not
        // necessarily contain vgui2's factory. IPanel lives in vgui2.dll,
        // just like the scheme interface acquired by VGUI2Extension/BaseUI.
        if(!panel){
            const auto module=GetModuleHandleA("vgui2.dll");
            auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
            if(factory)panel=static_cast<vgui::IPanel*>(factory(VGUI_PANEL_INTERFACE_VERSION,nullptr));
        }
    }
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{surface=nullptr;panel=nullptr;font=0;Reset();}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* c)override{
        ++uiPaints;
        if(!c->IsPost||!running||!surface||!panel)return;
        vgui::VPANEL target{};
        for(int i=surface->GetPopupCount()-1;i>=0&&!target;--i)target=FindLoading(surface->GetPopup(i));
        if(!uiReported){
            uiReported=true;
            engine.Con_Printf("ParallelDownload: loading panel=%u popups=%d\n",unsigned(target),surface->GetPopupCount());
        }
        if(!target)return;
        if(!font){font=surface->CreateFont();surface->AddGlyphSetToFont(font,"Microsoft YaHei",13,400,0,0,vgui::FONTFLAG_ANTIALIAS,0,0xffff);}
        wchar_t text[96]{};
        std::swprintf(text,std::size(text),L"%u/%u  剩余 %u",completed,unsigned(jobs.size()),unsigned(jobs.size())-settled);
        int width{},height{},tw{},th{};panel->GetSize(target,width,height);surface->GetTextSize(font,text,tw,th);
        surface->PushMakeCurrent(target,false);
        surface->DrawSetTextFont(font);surface->DrawSetTextColor(220,225,215,255);
        surface->DrawSetTextPos(std::max(12,width-tw-24),24);
        surface->DrawPrintText(text,int(std::wcslen(text)));surface->DrawFlushText();surface->PopMakeCurrent(target);
        if(!counterDraws)engine.Con_Printf("ParallelDownload: count drawn in loading panel %dx%d\n",width,height);
        ++counterDraws;
    }
    void HideGameUI(VGUI2Extension_CallbackContext*)override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext*)override{}
    void HideConsole(VGUI2Extension_CallbackContext*)override{}
    void ShowConsole(VGUI2Extension_CallbackContext*)override{}
} ui;
void* Resolve(const char* name,mh_gamesymbol_kind_t kind=MH_GAMESYMBOL_KIND_FUNCTION){
    void* address{};
    if(api->ResolveGameSymbol(api->GetEngineBase(),name,kind,&address)!=MH_GAMESYMBOL_OK||!address)throw std::runtime_error(name);
    return address;
}
void Install(){
    // Resolve the complete exact-module contract before installing any hook.
    auto queue=Resolve("ParallelDownload_Queue"),count=Resolve("ParallelDownload_Count"),reset=Resolve("ParallelDownload_Reset"),tick=Resolve("ParallelDownload_Tick");
    baseUrl=static_cast<char*>(Resolve("ParallelDownload_BaseURL",MH_GAMESYMBOL_KIND_GLOBAL));
    serverName=static_cast<char*>(Resolve("ParallelDownload_ServerName",MH_GAMESYMBOL_KIND_GLOBAL));
    progress=reinterpret_cast<decltype(progress)>(Resolve("ParallelDownload_Progress"));
    primaryProgress=reinterpret_cast<decltype(primaryProgress)>(Resolve("ParallelDownload_PrimaryProgress"));
    statusText=reinterpret_cast<decltype(statusText)>(Resolve("ParallelDownload_Text"));
    closeProgress=reinterpret_cast<decltype(closeProgress)>(Resolve("ParallelDownload_Close"));
    command=reinterpret_cast<decltype(command)>(Resolve("ParallelDownload_Command"));
    const auto module=GetModuleHandleA("VGUI2Extension.dll");
    auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
    extension=factory?static_cast<IVGUI2Extension*>(factory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
    if(!extension)throw std::runtime_error("VGUI2Extension is required before ParallelDownload");
    auto hook=[&](void* from,void* to,void** original){auto* h=api->InlineHook(from,to,original);if(!h)throw std::runtime_error("hook failed");hooks.push_back(h);};
    hook(queue,reinterpret_cast<void*>(Queue),&queueOriginal);
    hook(count,reinterpret_cast<void*>(Count),&countOriginal);
    hook(reset,reinterpret_cast<void*>(ResetHook),reinterpret_cast<void**>(&resetOriginal));
    hook(tick,reinterpret_cast<void*>(Tick),&tickOriginal);
    extension->RegisterBaseUICallbacks(&ui);
    engine.Con_Printf("ParallelDownload: verified HL10210 hooks installed\n");
}
void Shutdown(){
    Reset();pool.reset();
    if(extension){extension->UnregisterBaseUICallbacks(&ui);extension=nullptr;}
    for(auto it=hooks.rbegin();it!=hooks.rend();++it)api->UnHook(*it);hooks.clear();
}
}
}
void IPluginsV4::Init(metahook_api_t* a,mh_interface_t* i,mh_enginesave_t*){parallel_download::api=a;parallel_download::interfaces=i;}
void IPluginsV4::LoadEngine(cl_enginefunc_t* e){parallel_download::engine=*e;try{parallel_download::Install();}catch(const std::exception& error){parallel_download::Shutdown();e->Con_Printf("ParallelDownload disabled (original downloader retained): %s\n",error.what());}}
void IPluginsV4::LoadClient(cl_exportfuncs_t*){}
void IPluginsV4::Shutdown(){parallel_download::Shutdown();}
void IPluginsV4::ExitGame(int){parallel_download::Shutdown();}
const char* IPluginsV4::GetVersion(){return "ParallelDownload 1.0";}
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);
