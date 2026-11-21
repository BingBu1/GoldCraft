// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the production coordinator against controlled owner/transport contracts.
// Individual owners and WinHTTP/VFS have separate real SDK/network suites.
#include "../../native/client/resource_bridge.cpp"
#include <cstdarg>
#include <iostream>
#include <stdexcept>

namespace fixture {
using namespace goldcraft;
unsigned checks{}, failures{};
std::vector<std::string> calls;
int state=3, uploading=0;
bool ready{}, abort_after_native{}, active_owner{}, provider=true;
bool fail_before{}, fail_after{}, fail_reset{}, fail_native{}, fail_reload{};
bool old_callback_live{}, interrupt_native{}, ordinary{}, reset_during_native{};
bool sprite_readable=true;
bool fail_session{};
std::string progress_action;
unsigned progress_starts{}, progress_steps{}, progress_point{};
std::string progress_label;
bool progress_reentered{};
bool dual_dialog{};
std::vector<float> file_fractions;
std::string file_label;
resource_update::DownloadProgress download;
client_precache::ManifestProgress manifest;
std::string scenario;
resource_lifecycle::Observation observation{7,9,10,1,resource_lifecycle::Completion::returned,true};
resource_update::Status status;
resource_overlay::Snapshot overlay;
std::vector<std::string> changed{"models/csol/buymenu_v10.json"};
std::vector<resource_update::Resource> resources;
struct Abort {};
void Expect(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<text<<'\n';}}
void Call(const char* value){calls.emplace_back(value);}
std::size_t Position(const char* value){auto it=std::find(calls.begin(),calls.end(),value);return it==calls.end()?999:std::size_t(it-calls.begin());}
bool Has(const char* value){return Position(value)!=999;}
bool Available(){return provider;}
int Before(std::uint64_t,char*,std::uint32_t){Call("before");active_owner=true;return !fail_before;}
int After(std::uint64_t,char*,std::uint32_t){Call("after");if(fail_after)return 0;active_owner=false;return 1;}
int Cancel(std::uint64_t,char*,std::uint32_t){Call("cancel-owner");active_owner=false;return 1;}
int Base(std::uint64_t,char*,std::uint32_t){Call("base-owner");active_owner=false;return 1;}
const char* Validate(model_s*const*,std::uint32_t){Call("validate");return nullptr;}
const char* Release(model_s*const*,std::uint32_t){Call("release");return nullptr;}
void Print(const char*,...){ }
int Command(const char* value){Call(std::strstr(value,"retry")?"retry":std::strstr(value,"gc_resource_abort")?"abort-command":"disconnect");return 1;}
void HostError(const char*,...){Call("host-error");throw Abort{};}
int Original(void*){Call("native-original");return 1;}
int Resolution(void*)noexcept{return 640;}
void OriginalFrame(double){Call("original-frame");}
void ProgressEvent(const char* operation, bool native) {
    const std::string action = std::string(native ? "native-" : "") + operation;
    Call(action.c_str());
    if (progress_action == action + "-cancel") {
        progress_action.clear(); manifest = {}; resource_bridge::Cancel();
    } else if (progress_action == "progress-stale" || progress_action == action + "-stale") {
        progress_action.clear(); ++observation.clear_epoch;
    } else if ((progress_action == "progress-reentrant" || progress_action == action + "-reentrant") && !progress_reentered) {
        progress_reentered = true; resource_bridge::Frame(0);
    }
}
void LoadingStart(const char* type, int total) {
    ++progress_starts; progress_steps = unsigned(total);
    Expect(total > 0, "zero loading range");
    ProgressEvent("progress-start", std::strcmp(type, "Connecting") == 0);
}
void LoadingText(const char* text) {
    progress_label = text;
    ProgressEvent("progress-text", std::strstr(text, "Precaching") != nullptr);
}
void LoadingAdvance(const char* type, int point, float fraction) {
    progress_point = unsigned(point);
    Expect(point >= 0 && unsigned(point) <= progress_steps && fraction == 0.f, "progress exceeds actual range");
    ProgressEvent("progress-advance", std::strcmp(type, "ClientConnect") == 0);
}
void LoadingStop() {
    Expect(dual_dialog, "closed the dialog without an active file bar");
    dual_dialog = false;
    file_label.clear();
    ProgressEvent("download-close", false);
}
void LoadingFileProgress(float fraction) {
    Expect((dual_dialog || fraction <= .99f) && fraction >= 0.f && fraction <= 1.f,
        "file bar could not open at the supplied fraction");
    dual_dialog = true;
    file_fractions.push_back(fraction);
    ProgressEvent("download-bar", false);
}
void LoadingFileText(const char* path) {
    Expect(dual_dialog, "file label set before the dual layout was applied");
    file_label = path;
    ProgressEvent("download-file", false);
}
}

namespace goldcraft::resource_lifecycle {
const char* install(metahook_api_s*,resource_native_hud::Host::ScreenInfo,Handler,bool){return nullptr;}
resource_native_hud::Host::Release source_release()noexcept{return nullptr;}
const char* failure()noexcept{return nullptr;}
Observation observe()noexcept{return fixture::observation;}
bool current(const Entry&)noexcept{return true;}
resource_native_hud::Binding binding()noexcept{resource_native_hud::Binding value;value.vid_init=fixture::Original;value.current_resolution=fixture::Resolution;return value;}
bool block_untracked_calls(bool)noexcept{return true;}
}
namespace goldcraft::hud_sprites {
bool bind_source_release(SourceRelease){return true;}
resource_native_hud::DirectoryOwner resource_owner(){return {};}
}
namespace goldcraft::resource_native_hud {
Result Refresh::Prepare(const Admission& a,const Binding&,DirectoryOwner){fixture::Call("hud-prepare");fixture::Expect(a.consumers_paused&&a.state==3,"HUD prepared outside admission");phase_=Phase::prepared;return {};}
Result Refresh::Rebuild(const Admission&){
    fixture::Call("hud-rebuild");phase_=Phase::rebuilding;
    if(fixture::interrupt_native){fixture::observation.completion=resource_lifecycle::Completion::none;resource_bridge::Cancel();throw fixture::Abort{};}
    if(fixture::reset_during_native){++fixture::observation.clear_epoch;resource_bridge::Cancel();}
    phase_=fixture::fail_native?Phase::restore_required:Phase::rebuilt;
    return {fixture::fail_native?"native failed":nullptr};
}
Result Refresh::Commit(const Admission&){fixture::Call("hud-commit");phase_=Phase::idle;return {};}
Result Refresh::Initialize(const NativeInitialization& event,const Binding&,DirectoryOwner){
    fixture::Call("hud-natural");fixture::Expect(event.engine_vidinit&&event.previous_callback_unwound,"synthetic natural recovery");phase_=Phase::idle;return {};
}
}
namespace goldcraft::resource_sprite_handles {
const char* Capture(const Binding&,std::vector<Handle>& out){fixture::Call("capture");out={{1,"sprites/test.spr"}};return nullptr;}
const char* Reload(const Binding&,const std::vector<Handle>&){fixture::Call("reload");return fixture::fail_reload?"sprite failed":nullptr;}
}
namespace goldcraft::resource_overlay {
bool Install(metahook_api_s*,mh_interface_s*){return true;}
Snapshot Capture(){return fixture::overlay;}
bool Restore(const Snapshot& value){fixture::Call("restore-overlay");fixture::overlay=value;return true;}
void Clear(){fixture::Call("clear-overlay");fixture::overlay={};}
void Shutdown(){}
const char* Error(){return "overlay failed";}
Reader::~Reader()=default;
std::unique_ptr<Reader> OpenReader(const char*){return fixture::sprite_readable?std::unique_ptr<Reader>(new Reader(nullptr,nullptr,10)):nullptr;}
}
namespace goldcraft::client_precache {
bool waiting{};
ManifestProgress manifest_progress() noexcept { return fixture::manifest; }
void set_resource_sync(ResourceSyncCallbacks){}
void refresh_resource_capability(){}
void set_native_download_gate(bool(*)()){}
bool resume_resource_sync(){fixture::Call("resume-v4");fixture::Expect(!resource_bridge::DeferNativeDownload(),"v4 resume mistaken for ordinary server");return true;}
bool native_download_pending(){return waiting;}
bool resume_native_download(){fixture::Call("resume-native");waiting=false;return true;}
resource_model_cache::Result retire_resource_caches(bool,bool,const resource_model_cache::Engine&,const resource_model_cache::Owners& owners){
    fixture::Call("retire");fixture::Expect(fixture::active_owner,"native caches retired without CSOL pause");
    const std::array<resource_model_cache::Model,1> models{{{reinterpret_cast<model_s*>(1)}}};
    if(const auto* reason=owners.validate(nullptr,models))return {reason};
    if(!owners.release(nullptr,models))return {"release"};
    return {nullptr,1,1,1,true};
}
}
namespace goldcraft::resource_update {
struct Session::Impl {resource_overlay::Snapshot previous;bool transaction{};};
Session::Session(std::string, std::string):impl_(std::make_unique<Impl>()){}
Session::~Session()=default;
bool Session::Begin(std::uint64_t,std::string,std::vector<Resource> value){fixture::Call("begin-session");impl_->previous=fixture::overlay;impl_->transaction=true;fixture::resources=std::move(value);fixture::status={Phase::checking};fixture::status.total=fixture::resources.size();return true;}
void Session::Pump(){fixture::Call("pump");if(fixture::fail_session){fixture::status.phase=Phase::failed;fixture::status.error="models/test.mdl: unavailable HTTP 503";}else if(fixture::ready){fixture::overlay={"verified",{{"models/csol/buymenu_v10.json","verified-object"}}};fixture::status={Phase::ready,1,0,1,1};}}
bool Session::Reset(){fixture::Call("reset-session");if(fixture::fail_reset)return false;if(impl_->transaction){fixture::overlay=impl_->previous;impl_->transaction=false;}fixture::status={};return true;}
bool Session::Commit(){fixture::Call("commit-session");impl_->transaction=false;return true;}
const Status& Session::GetStatus()const{return fixture::status;}
DownloadProgress Session::GetDownloadProgress()const noexcept{return fixture::download;}
const std::vector<Resource>& Session::Resources()const{return fixture::resources;}
const std::vector<std::string>& Session::Changed()const{return fixture::changed;}
}

int main(int argc,char** argv){
    using namespace goldcraft;
    using namespace goldcraft::resource_bridge;
    using fixture::Expect;using fixture::Has;using fixture::Position;
    if(argc!=2)return 2;
    fixture::scenario=argv[1];
    static metahook_api_t fake_api{};
    static cl_enginefunc_t fake_engine{};
    static const csol_resources::Api fake_csol{sizeof(csol_resources::Api),csol_resources::api_version,fixture::Available,fixture::Before,fixture::After,fixture::Cancel,fixture::Base};
    static const renderer_resources::Api fake_renderer{sizeof(renderer_resources::Api),1,fixture::Available,fixture::Validate,fixture::Release};
    fake_engine.Con_Printf=fixture::Print;fake_engine.pfnClientCmd=fixture::Command;
    api=&fake_api;engine=&fake_engine;csol=&fake_csol;renderer=&fake_renderer;
    client_state=&fixture::state;uploading=&fixture::uploading;static char url[]="http://127.0.0.1/resources/";resource_url=url;
    original_frame=fixture::OriginalFrame;host_error=fixture::HostError;
    send_connection=[](){fixture::Call("keepalive");};
    loading_start=fixture::LoadingStart;loading_text=fixture::LoadingText;loading_advance=fixture::LoadingAdvance;
    loading_stop=fixture::LoadingStop;loading_file_progress=fixture::LoadingFileProgress;loading_file_text=fixture::LoadingFileText;
    thread=GetCurrentThreadId();installed=true;session=std::make_unique<resource_update::Session>("unused");
    const resource_lifecycle::Entry natural{20,2,fixture::observation,true,true};
    const std::vector<resource_update::Resource> request{{"models/csol/buymenu_v10.json",{}}};
    const auto& name=fixture::scenario;
    if(name=="not-ready"||name=="active"||name=="upload"||name=="no-init"||name=="callback-live"){
        if(name=="not-ready")fixture::provider=false;
        if(name=="active")fixture::state=5;
        if(name=="upload")fixture::uploading=1;
        if(name=="no-init")fixture::observation.initial_vidinit=0;
        if(name=="callback-live")fixture::observation.completion=Completion::none;
        Expect(Begin(1,request)!=nullptr,"unsafe admission accepted");Expect(!Has("before"),"rejected admission changed owners");
    }else if(name=="base"||name=="base-failure"||name=="disconnect-base"){
        fixture::overlay={"old",{{"sprites/test.spr","old-object"}}};
        if(name=="disconnect-base"){fixture::state=0;Cancel();}
        else{Expect(DeferNativeDownload(),"ordinary download did not wait");client_precache::waiting=true;}
        fixture::fail_reload=name=="base-failure";
        Frame(0);
        if(name=="base-failure"){
            Expect(!Has("resume-native"),"failed base refresh resumed");fixture::fail_reload=false;Frame(0);
            Expect(fixture::overlay.root=="old","base refresh did not restore prior overlay");
        }else{
            Expect(Has("base-owner"),"base owner reset missing");Expect(fixture::overlay.paths.empty(),"ordinary server inherited overlay");
            Expect(Has("resume-native")==(name=="base"),"wrong ordinary download resume");
            if(name=="disconnect-base"){Expect(paused(),"old disconnected HUD exposed");Expect(NativeInitialize(nullptr,natural)==1,"natural base recovery failed");Expect(!paused(),"natural base recovery stayed paused");}
        }
    }else if(name.starts_with("manifest-progress")){
        fixture::manifest = {1, 128, 512};
        if (name.ends_with("-start-cancel")) fixture::progress_action = "progress-start-cancel";
        if (name.ends_with("-text-cancel")) fixture::progress_action = "progress-text-cancel";
        Frame(0);
        Expect(fixture::progress_starts == 1 && fixture::progress_steps == 512,
            "partial manifest did not initialize the native progress range");
        Expect(!Has("before") && !Has("pump"), "partial manifest started resource verification");
        if (name.ends_with("-cancel")) {
            Expect(!Has("progress-advance") && !displayed_manifest.connection,
                "cancelled manifest advanced native progress");
        } else {
            Expect(fixture::progress_point == 128 && fixture::progress_label == "#GameUI_ParseServerInfo",
                "native dialog did not show the committed manifest ordinal");
            fixture::manifest.received = 256;
            next_loading_update = std::chrono::steady_clock::time_point::max();
            Frame(0);
            Expect(fixture::progress_point == 128, "manifest progress exceeded update rate");
            next_loading_update = {};
            Frame(0);
            Expect(fixture::progress_point == 256 && fixture::progress_starts == 1,
                "additional chunks did not advance the existing native dialog");
            Frame(0);
            Expect(std::count(fixture::calls.begin(), fixture::calls.end(), "progress-advance") == 2,
                "unchanged manifest progress dispatched a redundant callback");
            fixture::manifest = {};
            Expect(Begin(1, request) == nullptr, "completed manifest could not begin verification");
            Frame(0);
            Expect(fixture::progress_steps == 1 && fixture::progress_label == "#GameUI_VerifyingResources" && Has("pump"),
                "completed manifest did not hand off to native verification progress");
        }
    }else if(name.starts_with("download-ui")){
        const std::vector<resource_update::Resource> files{{"sound/first.wav",{}},{"models/second.mdl",{}}};
        Expect(Begin(1,files)==nullptr,"loading setup rejected");
        Frame(0);
        Expect(!fixture::dual_dialog && fixture::progress_label=="#GameUI_VerifyingResources",
            "local VFS check displayed a download");
        fixture::calls.clear();
        fixture::status.queued=2;
        fixture::download={0,200,200};
        if(name=="download-ui-start-cancel")fixture::progress_action="progress-start-cancel";
        if(name=="download-ui-bar-cancel")fixture::progress_action="download-bar-cancel";
        if(name=="download-ui-file-cancel")fixture::progress_action="download-file-cancel";
        if(name=="download-ui-file-stale")fixture::progress_action="download-file-stale";
        if(name=="download-ui-file-reentrant")fixture::progress_action="download-file-reentrant";
        Frame(0);
        if(name=="download-ui-start-cancel"){
            Expect(!fixture::dual_dialog && !Has("download-bar"),
                "cancelled primary start opened a secondary layout");
            fixture::download={};
            ++fixture::observation.clear_epoch;
            Frame(0);
            Frame(0);
            Expect(Begin(2,files)==nullptr,"replacement admission could not start");
            Frame(0);
            Expect(!Has("download-close"),"replacement admission closed a layout that never opened");
        }else if(name=="download-ui-bar-cancel"||name=="download-ui-file-cancel"||name=="download-ui-file-stale"){
            Expect(!Has("pump")&&!Has("resume-v4"),"obsolete download UI advanced its resource session");
            Expect(fixture::file_fractions==std::vector{0.f},"obsolete download UI published later bytes");
            if(name=="download-ui-bar-cancel")Expect(!Has("download-file"),"cancelled file bar sent file text");
            Frame(0);
            Frame(0);
            Expect(Has("reset-session")&&Has("cancel-owner"),"obsolete download UI did not unwind");
            AbortAdmission();
            Expect(!Has("host-error")&&!Has("retry"),"obsolete download UI started an engine command");
        }else{
            Expect(fixture::status.phase==resource_update::Phase::checking && fixture::dual_dialog &&
                fixture::progress_label=="#GameUI_VerifyingAndDownloading",
                "HTTP during the first VFS check did not switch layouts");
            Expect(fixture::file_label==files[0].name && fixture::file_fractions==std::vector{0.f,1.f},
                "quick first transfer did not open the native file bar from zero");
            resource_progress::Snapshot progress{};
            Expect(ReadProgress(&progress) && progress.phase==resource_progress::Phase::downloading &&
                progress.completed==0 && progress.total==2,"overlay counter did not follow early HTTP");
            if(name=="download-ui-file-reentrant"){
                Expect(fixture::progress_reentered,"file text did not exercise recursive rendering");
                Expect(std::count(fixture::calls.begin(),fixture::calls.end(),"pump")==1,
                    "file text recursively pumped the resource session");
            }
            fixture::status.downloaded=1;
            fixture::download={1,50,200};
            next_loading_update={};
            Frame(0);
            Expect(fixture::file_label==files[1].name && fixture::file_fractions==std::vector{0.f,1.f,0.f,.25f} &&
                fixture::progress_point==1,"the next file did not start with fresh progress and ETA");
            fixture::download.received=150;
            next_loading_update=std::chrono::steady_clock::time_point::max();
            Frame(0);
            Expect(fixture::file_fractions.back()==.25f,"file bytes bypassed the UI rate limit");
            fixture::calls.clear();
            next_loading_update={};
            Frame(0);
            Expect(fixture::file_fractions.back()==.75f && !Has("download-file") && !Has("progress-advance"),
                "changing file bytes did not update only the secondary bar");
            const auto updates=fixture::file_fractions.size();
            next_loading_update={};
            Frame(0);
            Expect(fixture::file_fractions.size()==updates,"unchanged file bytes sent a duplicate UI update");
            if(name=="download-ui-reconnect"){
                fixture::calls.clear();
                Cancel(); ++fixture::observation.clear_epoch;
                fixture::download={};
                Frame(0);
                Expect(!Has("download-close") && !Has("resume-v4"),"abandoned session touched the native dialog");
                Expect(Begin(2,files)==nullptr,"replacement admission could not start");
                Frame(0);
                Expect(!fixture::dual_dialog && fixture::file_label.empty() &&
                    Position("download-close")<Position("progress-start"),
                    "replacement admission did not clear the old file layout");
                std::cout << fixture::checks << " checks, " << fixture::failures << " failures\n";
                return fixture::failures?1:0;
            }
            fixture::status.phase=resource_update::Phase::verifying;
            fixture::status.checked=2;
            fixture::download={};
            fixture::calls.clear();
            if(name=="download-ui-close-cancel")fixture::progress_action="download-close-cancel";
            Frame(0);
            Expect(!fixture::dual_dialog && fixture::file_label.empty() && Has("download-close"),
                "completed HTTP left a stale secondary layout");
            if(name=="download-ui-close-cancel"){
                Expect(!Has("progress-start")&&!Has("pump"),"cancelled close opened a replacement dialog");
                Frame(0);
                Expect(Has("cancel-owner"),"cancelled close did not unwind admission");
            }else{
                Expect(Position("download-close")<Position("progress-start") &&
                    fixture::progress_label=="#GameUI_VerifyingResources" && fixture::progress_point==2,
                    "single-bar final VFS check did not follow HTTP");
                fixture::ready=true;
                Frame(0);
                Expect(Has("resume-v4") && fixture::progress_point==6 && !Has("disconnect") && !Has("retry"),
                    "native loading did not resume on the original connection");
            }
        }
    }else if(name.starts_with("progress-")||name.starts_with("native-progress-")){
        Expect(Begin(1,request)==nullptr,"loading setup rejected");
        fixture::progress_action=name;
        fixture::ready=name.starts_with("native-")||name=="progress-reentrant";
        Frame(0);
        if(name.ends_with("-cancel")){
            Expect(!Has("resume-v4")&&!Has("retire"),"UI cancellation continued native resource work");
            if(name.starts_with("progress-"))Expect(!Has("pump"),"UI cancellation continued verification");
            Frame(0);Expect(Has("reset-session")&&Has("cancel-owner"),"UI cancel did not unwind owners");
            Expect(!Has("host-error")&&!Has("abort-command"),"user cancel manufactured a transfer error");
        }else if(name=="progress-stale"){
            Expect(!Has("pump"),"replaced connection continued verification");Frame(0);Frame(0);
            Expect(!Has("resume-v4")&&Has("cancel-owner"),"stale progress resumed a connection");
        }else if(name=="progress-reentrant"){
            Expect(fixture::progress_reentered,"loading UI did not exercise recursive frame");
            Expect(std::count(fixture::calls.begin(),fixture::calls.end(),"pump")==1,"loading UI recursively pumped resource session");
            Expect(Has("resume-v4"),"valid reentrant draw blocked native resume");
        }else{
            Expect(Has("progress-start")&&Position("before")<Position("progress-start"),"UI started before resource owners paused");
            fixture::status.phase=resource_update::Phase::checking;fixture::status.total=100;fixture::status.checked=17;
            next_loading_update={};Frame(0);Expect(fixture::progress_steps==100&&fixture::progress_point==17,"checking count not taken from actual session");
            const auto starts=fixture::progress_starts;Frame(0);Expect(fixture::progress_starts==starts,"same phase reset loading dialog");
            fixture::status.phase=resource_update::Phase::downloading;fixture::status.queued=8;fixture::status.downloaded=3;
            next_loading_update={};Frame(0);Expect(fixture::progress_steps==8&&fixture::progress_point==3,"download count not taken from actual session");
            fixture::status.phase=resource_update::Phase::verifying;fixture::status.checked=41;
            next_loading_update={};Frame(0);Expect(fixture::progress_steps==100&&fixture::progress_point==41,"final VFS count not taken from actual session");
            fixture::ready=true;Frame(0);
            Expect(Has("resume-v4")&&fixture::progress_steps==12&&fixture::progress_point==6,"native loading range was not restored");
            Expect(Position("native-progress-advance")<Position("retire"),"loading status followed blocking resource refresh");
        }
    }else if(name=="download-failure"||name=="failure-cancel"||name=="failure-reconnect"){
        Expect(Begin(1,request)==nullptr,"failed-download setup rejected");
        fixture::fail_session=true;Frame(0);
        Expect(!Has("disconnect")&&!Has("abort-command"),"failure disconnected before owner cleanup");
        Expect(!Has("resume-v4"),"failed body resumed admission");
        AbortAdmission();Expect(!Has("host-error"),"error escaped before cleanup");
        fixture::calls.clear();Frame(0);
        Expect(Has("reset-session")&&Has("cancel-owner"),"failed transaction not restored");
        Expect(Has("abort-command"),"failure did not schedule visible engine error");
        Expect(Position("cancel-owner")<Position("abort-command"),"error command preceded cleanup");
        if(name=="failure-cancel")Cancel();
        if(name!="download-failure")++fixture::observation.clear_epoch;
        try{AbortAdmission();}catch(const fixture::Abort&){}
        Expect(Has("host-error")== (name=="download-failure"),"old failure affected a cancelled or replaced connection");
        if(name!="download-failure"){
            fixture::fail_session=false;fixture::ready=true;
            Expect(Begin(2,request)==nullptr,"fresh manifest blocked after old error retired");Frame(0);
            Expect(Has("resume-v4"),"fresh verified session did not resume");
        }
    }else{
        Expect(Begin(1,request)==nullptr,"valid manifest rejected");Expect(paused(),"manifest did not suspend native readers");
        Expect(!Has("before")&&!Has("begin-session"),"manifest parser performed GL/HTTP setup");
        if(name=="before-failure")fixture::fail_before=true;
        if(name=="unchanged")fixture::changed.clear();
        if(name=="after-failure")fixture::fail_after=true;
        if(name=="native-failure")fixture::fail_native=true;
        if(name=="native-unwind")fixture::interrupt_native=true;
        if(name=="native-reset")fixture::reset_during_native=true;
        fixture::ready=name!="cancel"&&name!="stale";
        try{Frame(0);}catch(const fixture::Abort&){}
        if(name=="cancel"||name=="stale"){
            Expect(Has("before")&&Has("begin-session"),"frame did not begin transaction");Expect(!Has("resume-v4"),"unfinished HTTP resumed");
            Expect(Has("keepalive"),"paused resource update never maintained actual netchan");
            fixture::calls.clear();
            Frame(0);
            Expect(!Has("keepalive"),"keepalive flooded repeated same-second frames");
            fixture::calls.clear();
            if(name=="cancel"){Cancel();Expect(fixture::calls.empty(),"reset callback freed resources");}
            else ++fixture::observation.clear_epoch;
            Frame(0);if(name=="stale")Frame(0);
            Expect(Has("reset-session")&&Has("cancel-owner"),"cancel did not restore owners");Expect(!Has("resume-v4"),"stale transaction resumed");
        }else if(name=="native-unwind"){
            fixture::calls.clear();Frame(0);Expect(!Has("reset-session"),"freed live native callback");
            fixture::observation.completion=Completion::frame_returned;fixture::interrupt_native=false;Frame(0);
            Expect(Has("reset-session")&&hud.pending(),"unwound callback lost retained native arrays");
            Expect(NativeInitialize(nullptr,natural)==1,"pending native recovery failed");Expect(Has("hud-natural"),"pending native recovery bypassed natural entry");
        }else if(name=="before-failure"||name=="after-failure"||name=="native-failure"||name=="native-reset"){
            Expect(!Has("resume-v4"),"failed update resumed downloads");
            if(name=="native-reset")Expect(!Has("hud-commit"),"committed after connection reset");
            fixture::calls.clear();Frame(0);Expect(Has("reset-session")&&Has("cancel-owner"),"failed update not cancelled");
        }else{
            Expect(Has("resume-v4"),"verified transaction did not resume");Expect(!paused(),"success remained paused");
            Expect(Position("before")<Position("begin-session"),"view switched before pause");
            Expect(Position("after")<Position("commit-session")&&Position("commit-session")<Position("resume-v4"),"resume preceded owner/session commit");
            Expect(Has("retire")==(name!="unchanged"),"unchanged content retired caches");
            if(name!="unchanged")Expect(Position("release")<Position("reload")&&Position("reload")<Position("hud-rebuild"),"invalid owner/reload/HUD ordering");
        }
    }
    std::cout<<"resource bridge "<<name<<": "<<fixture::checks<<" checks, "<<fixture::failures<<" failures\n";
    session.reset();return fixture::failures?1:0;
}
