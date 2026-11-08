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
bool cancelDuringPaint{};
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
void __cdecl Progress(float value){Check(value>=0&&value<=1,"bad progress");}
void __cdecl Primary(const char*,int,float){if(cancelDuringPaint){cancelDuringPaint=false;pd::Reset();}}
void __cdecl Text(const char*){}
void __cdecl Close(){++closes;}
void __cdecl Command(const char* value){Check(std::string(value)=="retry\n","bad command");++retries;}
void Pump(){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);while(!pd::jobs.empty()&&std::chrono::steady_clock::now()<deadline){pd::Tick();std::this_thread::sleep_for(std::chrono::milliseconds(10));}Check(pd::jobs.empty(),"batch hung");}
int main(int argc,char**argv){try{
    Check(argc==3,"URL and output required");FS fs(std::filesystem::absolute(argv[2]));mh_interface_t interfaces{};interfaces.FileSystem=&fs;pd::interfaces=&interfaces;
    std::string url=argv[1];char server[]="test";pd::baseUrl=url.data();pd::serverName=server;
    pd::engine.Con_Printf=Print;pd::progress=Progress;pd::primaryProgress=Primary;pd::statusText=Text;pd::closeProgress=Close;pd::command=Command;
    pd::Limits limits;limits.timeout_ms=300;limits.total_ms=1000;pd::pool=std::make_unique<pd::Pool>(limits);
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
