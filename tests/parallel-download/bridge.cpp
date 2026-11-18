// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production queue/frame/reset bridge with matching SDK types.
#include "../../native/parallel_download/plugin.cpp"
#include "filesystem_adapter.hpp"
#include <chrono>
#include <fstream>
#include <thread>
#include <iostream>
namespace pd=parallel_download;
unsigned checks{},retries{},closes{};
unsigned continuations{},disconnects{},nativeBatches{},nativeProcessed{},nativePrecaches{},keepalives{};
int nativeState=4,nativeUploading{},nativeDemo{},fakeChannel{};
bool cancelNative{};
bool replaceOnCancel{};
struct FS;
FS* activeFS{};
bool cancelDuringPaint{};
enum class UiEvent { none, secondary, text, close, nested, replace, cancelThrow };
UiEvent uiEvent{};
void UiAction(UiEvent event){
    if(uiEvent!=event)return;
    uiEvent=UiEvent::none;
    if(event==UiEvent::nested){pd::Tick();return;}
    pd::Reset();
    if(event==UiEvent::replace)pd::Queue("chunked");
    if(event==UiEvent::cancelThrow)throw std::runtime_error("cancelled UI callback");
}
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
struct FS final:ThrowingFS {
    std::filesystem::path root;
    explicit FS(std::filesystem::path p):root(std::move(p)){std::filesystem::create_directories(root);}
    void CreateDirHierarchy(const char* name,const char*)override{std::filesystem::create_directories(root/name);}
    FileHandle_t Open(const char* name,const char* mode,const char*)override{return std::fopen((root/name).string().c_str(),mode);}
    void Close(FileHandle_t h)override{std::fclose(static_cast<FILE*>(h));}
    bool FileExists(const char* name)override{return std::filesystem::exists(root/name);}
    const char* GetLocalPath(const char* name,char* out,int n)override{const auto s=(root/name).string();if(int(s.size())>=n)return nullptr;std::strcpy(out,s.c_str());return out;}
    void RemoveFile(const char* name,const char*)override{std::filesystem::remove(root/name);}
};
void __cdecl Print(const char*,...){}
void __cdecl Progress(float value){Check(value>=0&&value<=1,"bad progress");UiAction(UiEvent::secondary);UiAction(UiEvent::nested);UiAction(UiEvent::replace);UiAction(UiEvent::cancelThrow);}
void __cdecl Primary(const char*,int,float){if(cancelDuringPaint){cancelDuringPaint=false;pd::Reset();}}
void __cdecl Text(const char*){UiAction(UiEvent::text);}
void __cdecl Close(){++closes;UiAction(UiEvent::close);}
void __cdecl Command(const char* value){
    if(std::string(value)=="gc_download_continue\n"){++continuations;return;}
    Check(std::string(value)=="retry\n","bad command");++retries;
}
void __cdecl Disconnect(){++disconnects;}
int __cdecl NativePrecache(){++nativePrecaches;return 1;}
void __cdecl Transmit(void* channel,int bytes,const void* data){
    Check(channel==&fakeChannel&&!bytes&&!data,"keepalive injected gameplay bytes");++keepalives;
}
void __cdecl ProcessFile(int success,const char* name){
    Check(success==1&&activeFS->FileExists(name),"unpublished HTTP file passed to native success");
    Check(pd::Precache()==0,"native loader ran before all HTTP completions");++nativeProcessed;
    if(cancelNative){cancelNative=false;pd::Reset();pd::Queue("replacement");Check(std::string(name)=="chunked","native callback argument retired during reset");}
}
void __cdecl RequestBatch(){++nativeBatches;pd::Reset();pd::Precache();}
int __cdecl NativeCancel(){pd::Reset();if(replaceOnCancel){replaceOnCancel=false;pd::Queue("replacement");}return 9;}
void Pump(){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);while(!pd::jobs.empty()&&std::chrono::steady_clock::now()<deadline){pd::Tick();std::this_thread::sleep_for(std::chrono::milliseconds(10));}Check(pd::jobs.empty(),"batch hung");}
int main(int argc,char**argv){try{
    Check(argc==3||argc==4,"URL and output required");FS fs(std::filesystem::absolute(argv[2]));mh_interface_t interfaces{};interfaces.FileSystem=&fs;pd::interfaces=&interfaces;
    std::string url=argv[1];char server[]="test";pd::baseUrl=url.data();pd::serverName=server;
    pd::engine.Con_Printf=Print;pd::progress=Progress;pd::primaryProgress=Primary;pd::statusText=Text;pd::closeProgress=Close;pd::command=Command;
    pd::Limits limits;limits.timeout_ms=300;limits.total_ms=argc==4?10000:1000;pd::pool=std::make_unique<pd::Pool>(limits);
    if(argc==4 && std::string(argv[3])=="connected"){
        activeFS=&fs;pd::clientState=&nativeState;pd::uploading=&nativeUploading;pd::demoPlayback=&nativeDemo;
        pd::netchan=&fakeChannel;pd::disconnectOriginal=Disconnect;pd::precacheOriginal=NativePrecache;
        pd::cancelOriginal=NativeCancel;
        pd::transmit=Transmit;pd::processFile=ProcessFile;pd::requestBatch=RequestBatch;
        pd::DownloadDisconnect();Check(disconnects==1&&!pd::holdingConnection,"empty queue suppressed native disconnect");
        pd::Queue("ok");nativeUploading=1;pd::DownloadDisconnect();Check(disconnects==2&&!pd::holdingConnection,"upload connection was intercepted");nativeUploading=0;
        nativeDemo=1;pd::DownloadDisconnect();Check(disconnects==3&&!pd::holdingConnection,"demo disconnect was intercepted");nativeDemo=0;
        nativeState=5;pd::DownloadDisconnect();Check(disconnects==4&&!pd::holdingConnection,"active gameplay disconnect was intercepted");nativeState=4;
        pd::DownloadDisconnect();Check(pd::holdingConnection&&disconnects==4,"HTTP admission dropped connection");
        Check(pd::Precache()==0&&!nativePrecaches,"models loaded before HTTP finished");Pump();
        Check(keepalives&&continuations==1&&!retries&&!closes,"HTTP completion closed or retried the connection");
        Check(!nativeBatches&&pd::holdingConnection,"native loading ran inside Tick C++ owners");
        pd::ContinueDownload();pd::ContinueDownload();Check(nativeBatches==1&&nativeProcessed==1&&nativePrecaches==1,"native completion did not register exactly once");
        Check(nativeState==4&&!pd::holdingConnection&&!retries&&!closes,"completion changed connection or left precache blocked");
        pd::Queue("missing");pd::DownloadDisconnect();Pump();pd::ContinueDownload();Check(nativeProcessed==1&&nativeBatches==2&&!retries,"failed HTTP file became native success or retried");
        pd::Queue("chunked");pd::DownloadDisconnect();Pump();pd::Reset();pd::Queue("replacement");pd::ContinueDownload();Check(pd::Count()==1&&pd::jobs[0]->name=="replacement"&&nativeBatches==2,"cancelled continuation touched replacement queue");pd::Reset();
        pd::Queue("ok");pd::DownloadDisconnect();Pump();nativeState=1;pd::ContinueDownload();Check(pd::jobs.empty()&&!pd::holdingConnection&&nativeBatches==2&&!retries,"disconnected continuation resumed old admission");nativeState=4;
        pd::Queue("chunked");pd::DownloadDisconnect();Pump();cancelNative=true;pd::ContinueDownload();Check(nativeBatches==2&&pd::Count()==1&&pd::jobs[0]->name=="replacement","native callback cancellation resumed retired admission");pd::Reset();
        pd::Queue("hang");pd::DownloadDisconnect();nativeState=3;pd::Tick();Check(pd::jobs.empty()&&!pd::holdingConnection&&!retries,"new serverinfo retained old HTTP admission");
        nativeState=4;pd::Queue("hang");pd::DownloadDisconnect();Check(pd::CancelDownload()==9&&disconnects==5&&pd::jobs.empty()&&!pd::holdingConnection,"explicit user cancellation left retained server slot alive");
        pd::Queue("hang");pd::DownloadDisconnect();replaceOnCancel=true;Check(pd::CancelDownload()==9&&disconnects==5&&pd::Count()==1,"old cancellation disconnected a replacement admission");pd::Reset();
        pd::pool.reset();std::cout<<checks<<" connected download bridge checks passed\n";return 0;
    }
    if(argc==4 && std::string(argv[3])=="ui"){
        uiEvent=UiEvent::secondary;pd::Queue("hang");pd::Tick();Check(pd::jobs.empty(),"secondary progress cancellation ignored");Check(retries==0&&closes==0,"cancelled secondary progress retried or closed another UI");
        uiEvent=UiEvent::replace;pd::Queue("hang");pd::Tick();Check(pd::Count()==1&&pd::jobs[0]->name=="chunked","replacement batch retired by stale tick");Check(retries==0,"replacement batch retried before completion");Pump();Check(retries==1,"replacement batch did not finish once");
        uiEvent=UiEvent::cancelThrow;pd::Queue("hang");pd::Tick();Check(pd::jobs.empty()&&retries==1,"exception after cancellation retried old session");
        {std::ofstream file(fs.root/"done");file<<"existing";}
        uiEvent=UiEvent::nested;pd::Queue("done");pd::Tick();Check(pd::jobs.empty()&&retries==2,"nested tick completed or retried twice");
        const auto beforeClose=closes;uiEvent=UiEvent::close;pd::Queue("done");pd::Tick();Check(pd::jobs.empty()&&retries==2&&closes==beforeClose+1,"close callback cancellation still issued retry");
        pd::Queue("hang");pd::Tick();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(!pd::jobs.front()->active&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(pd::jobs.front()->active,"real transfer did not become active for status cancellation");uiEvent=UiEvent::text;pd::Tick();Check(pd::jobs.empty()&&retries==2,"status text cancellation used retired batch");
        pd::pool.reset();std::cout<<checks<<" UI reentry bridge checks passed\n";return 0;
    }
    if(argc==4){pd::Queue("flaky");Pump();Check(retries==1&&closes==1,"recovery completed without one reconnect");Check(fs.FileExists("flaky")&&std::filesystem::file_size(fs.root/"flaky")==65536,"loading UI ended before failed file recovered");std::cout<<checks<<" recovery bridge checks passed\n";return 0;}
    pd::Queue("ok");pd::Queue("ok");pd::Queue("OK");Check(pd::Count()==1,"duplicate counted");Check(!fs.FileExists("ok"),"early final file");Pump();Check(retries==1&&closes==1,"completion retry missing");Check(std::filesystem::file_size(fs.root/"ok")==65536,"file not committed");
    pd::Queue("../escape");Check(pd::Count()==0,"traversal accepted");
    pd::Queue("missing");Pump();Check(retries==2,"failure fallback missing");Check(!fs.FileExists("missing"),"failure published");
    pd::Queue("hang");pd::Tick();pd::Reset();pd::Queue("chunked");Pump();Check(retries==3,"cancel retried");Check(!fs.FileExists("hang")&&fs.FileExists("chunked"),"cancel session leaked");
    // A preexisting user's file remains unchanged.
    {std::ofstream f(fs.root/"ok");f<<"user bytes";}pd::Queue("ok");Pump();Check(std::filesystem::file_size(fs.root/"ok")==10,"existing file overwritten");
    cancelDuringPaint=true;pd::Queue("hang");pd::Tick();Check(pd::jobs.empty(),"UI cancellation ignored");Check(retries==4,"UI cancellation retried");
    pd::pool.reset();for(const auto& item:std::filesystem::recursive_directory_iterator(fs.root))Check(item.path().extension()!=".part","orphan partial");
    std::cout<<checks<<" bridge checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}}
