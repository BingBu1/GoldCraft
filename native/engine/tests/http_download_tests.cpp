#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "precompiled.h"
#include "precache.h"
#include "http_download.h"
#include "http_download_store.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <future>
#include <iostream>
#include <map>
#include <thread>
#include <unordered_map>
using namespace goldcraft::http;
using namespace std::chrono_literals;
static std::atomic<unsigned> checks{0};
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << ": " #x "\n"; std::abort(); } } while (0)
template<class F> void until(F predicate, std::chrono::seconds timeout=3s) {
    const auto end=std::chrono::steady_clock::now()+timeout;
    while(!predicate()) { CHECK(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(1ms); }
}
static const auto main_thread=std::this_thread::get_id();
static void main_only(){CHECK(std::this_thread::get_id()==main_thread);}
GoldCraftPrecache gc_precache;
cvar_t sv_downloadurl{"sv_downloadurl","",0,0,nullptr}, sv_allow_download{"sv_allowdownload","1",0,1,nullptr},
    sv_send_resources{"sv_send_resources","1",0,1,nullptr}, sv_allow_dlfile{"sv_allow_dlfile","1",0,1,nullptr};
double realtime=0;
static std::map<std::string,cvar_t*> cvars;
static std::map<std::string,std::string> values;
static std::map<std::string,void(*)()> commands;
void Cvar_RegisterVariable(cvar_t* v){main_only();cvars[v->name]=v;v->value=std::strtof(v->string,nullptr);}
void Cvar_Set(const char* name,const char* value){main_only();CHECK(cvars.count(name));values[name]=value;auto& v=*cvars[name];v.string=values[name].c_str();v.value=std::strtof(v.string,nullptr);}
void Cmd_AddCommand(const char* name,void(*fn)()){main_only();commands[name]=fn;}
void Con_Printf(const char* fmt,...){main_only();va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);}
struct File { Bytes bytes; int time=1; bool broken=false; };
static std::map<std::string,File> files;
// Deliberately share a cursor, as pack members do in the engine stdio FS.
static std::size_t shared_cursor=0, read_frame=0, largest_read_frame=0;
static unsigned opens=0, closes=0, seeks=0;
FileHandle_t FS_Open(const char* name,const char*){main_only();++opens;auto it=files.find(name);return it==files.end()?nullptr:&it->second;}
void FS_Close(FileHandle_t){main_only();++closes;}
unsigned FS_Size(FileHandle_t f){main_only();return static_cast<unsigned>(static_cast<File*>(f)->bytes.size());}
unsigned FS_FileSize(const char* name){main_only();return static_cast<unsigned>(files.at(name).bytes.size());}
int FS_GetFileTime(const char* name){main_only();return files.at(name).time;}
int FS_IsOk(FileHandle_t f){main_only();return !static_cast<File*>(f)->broken;}
void FS_Seek(FileHandle_t,int offset,FileSystemSeek_t){main_only();shared_cursor=offset;++seeks;}
int FS_Read(void* out,int size,int count,FileHandle_t f){main_only();auto& src=*static_cast<File*>(f);auto n=std::min<std::size_t>(size*count,src.bytes.size()-std::min(shared_cursor,src.bytes.size()));std::memcpy(out,src.bytes.data()+shared_cursor,n);shared_cursor+=n;read_frame+=n;return src.broken?0:static_cast<int>(n);}
static Bytes pattern(std::size_t size,unsigned salt=0){Bytes b(size);for(std::size_t i=0;i<size;++i)b[i]=static_cast<std::uint8_t>(i*73+salt);return b;}
static SOCKET connect_to(unsigned short port){
    auto s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(s!=INVALID_SOCKET);
    DWORD timeout=16000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
    CHECK(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);return s;
}
struct Response {int status=0;std::string header;Bytes body;};
static Response receive(SOCKET s){
    std::string wire;char b[32768];for(;;){auto n=recv(s,b,sizeof(b),0);if(n<=0)break;wire.append(b,n);}closesocket(s);
    if(wire.empty())return {}; // shutdown/cancel may close before a response
    auto end=wire.find("\r\n\r\n");CHECK(end!=std::string::npos);
    Response r;r.status=std::stoi(wire.substr(9,3));r.header=wire.substr(0,end);r.body.assign(wire.begin()+end+4,wire.end());return r;
}
static Response request(unsigned short port,const std::string& target,const std::string& headers={},const std::string& method="GET",const std::string& version="HTTP/1.1"){
    auto s=connect_to(port);auto q=method+" "+target+" "+version+"\r\nHost: localhost\r\n"+headers+"\r\n";
    CHECK(send(s,q.data(),static_cast<int>(q.size()),0)==static_cast<int>(q.size()));return receive(s);
}
static std::future<Response> async_request(unsigned short port,const std::string& name){return std::async(std::launch::async,[=]{return request(port,"/goldcraft/"+name);});}
static void frame(){realtime+=.02;shared_cursor=17;read_frame=0;GC_HttpFrame();largest_read_frame=std::max(largest_read_frame,read_frame);CHECK(read_frame<=256*1024);}
static Response pump(std::future<Response>& f){until([&]{frame();return f.wait_for(0ms)==std::future_status::ready;});return f.get();}
static void add(const char* name,resourcetype_t type,std::size_t size,int flags=0){resource_t r{};CHECK(std::strlen(name)<sizeof(r.szFileName));std::memcpy(r.szFileName,name,std::strlen(name));r.type=type;r.nDownloadSize=static_cast<int>(size);r.ucFlags=static_cast<unsigned char>(flags);gc_precache.resources.push_back(r);}
static void server_tests(){
    const auto data=std::make_shared<const Bytes>(pattern(1024*1024+17));
    Server s;std::atomic<unsigned> providers{0};CHECK(!s.start("hostname",0,{}));
    CHECK(s.start("127.0.0.1",0,[&](const std::string& name)->Body{++providers;return name=="models/test space.mdl"?data:Body{};}));auto port=s.port();CHECK(port);
    auto r=request(port,"/goldcraft/models/test%20space.mdl","Accept-Encoding: gzip\r\n");CHECK(r.status==200&&r.body==*data);CHECK(r.header.find("Content-Encoding")==std::string::npos);
    r=request(port,"/goldcraft/models/test%20space.mdl",{},"HEAD","HTTP/1.0");CHECK(r.status==200&&r.body.empty());CHECK(r.header.find("Content-Length: "+std::to_string(data->size()))!=std::string::npos);
    for(const auto& range:std::vector<std::pair<std::string,Bytes>>{{"bytes=5-10",Bytes(data->begin()+5,data->begin()+11)},{"bytes=-8",Bytes(data->end()-8,data->end())},{"bytes=1048590-",Bytes(data->end()-3,data->end())},{"bytes=0-999999999",*data}}){r=request(port,"/goldcraft/models/test%20space.mdl","Range: "+range.first+"\r\n");CHECK(r.status==206&&r.body==range.second);}
    for(const auto& range:{"bytes=8-7","bytes=9999999999-","bytes=-0","bytes=1-3,5-7","garbage","bytes=0-999999999999999999"})CHECK(request(port,"/goldcraft/models/test%20space.mdl",std::string("Range: ")+range+"\r\n").status==416);
    r=request(port,"/goldcraft/models/test%20space.mdl","Range: bytes=5-10\r\nIf-Range: \"old\"\r\n");CHECK(r.status==200&&r.body==*data);
    CHECK(request(port,"/goldcraft/models/test%20space.mdl","Range: \r\n").status==416);
    r=request(port,"/goldcraft/models/test%20space.mdl","Range: bytes=5-10\r\nIf-Range: \r\n");CHECK(r.status==200&&r.body==*data);
    auto calls=providers.load();
    for(const auto& target:{"/goldcraft/../server.cfg","/goldcraft/models/../test.mdl","/goldcraft/models%2Ftest.mdl","/goldcraft/models/%252e.mdl","/goldcraft/models/CON.mdl","/goldcraft/models/test.mdl?x=1","/goldcraft/models/x.dll","/goldcraft/","/other/models/test.mdl","/goldcraft/models/x%00.mdl"})CHECK(request(port,target).status==404);
    CHECK(providers==calls);CHECK(request(port,"/goldcraft/models/missing.mdl").status==404);
    CHECK(request(port,"/goldcraft/models/test.mdl",{},"POST").status==405);
    CHECK(request(port,"/goldcraft/models/test.mdl","Transfer-Encoding: chunked\r\n").status==400);
    CHECK(request(port,"/goldcraft/models/test.mdl","Bad Header: value\r\n").status==400);
    CHECK(request(port,"/goldcraft/models/test.mdl","X-Test: value\x01\r\n").status==400);
    CHECK(request(port,"/goldcraft/models/test.mdl","Range: bytes=0-2\r\nRange: bytes=3-4\r\n").status==400);
    CHECK(request(port,"/goldcraft/models/test.mdl",std::string(8300,'a')+"\r\n").status==400);
    std::vector<std::future<Response>> concurrent;for(int i=0;i<5;++i)concurrent.push_back(std::async(std::launch::async,[=]{return request(port,"/goldcraft/models/test%20space.mdl");}));for(auto& f:concurrent)CHECK(f.get().body==*data);
    // Occupy all eight slots without completed headers, then observe overload.
    std::vector<SOCKET> idle;for(int i=0;i<8;++i){idle.push_back(connect_to(port));std::this_thread::sleep_for(5ms);}
    CHECK(request(port,"/goldcraft/models/test.mdl").status==503);
    auto began=std::chrono::steady_clock::now();s.stop();CHECK(std::chrono::steady_clock::now()-began<1500ms);for(auto fd:idle)closesocket(fd);
    CHECK(s.start("127.0.0.1",port,[&](const std::string&)->Body{return data;}));
    auto partial=connect_to(port);const char incomplete[]="GET /goldcraft/models/test.mdl HTTP/1.1\r\n";send(partial,incomplete,sizeof(incomplete)-1,0);
    auto timed=std::async(std::launch::async,[=]{return receive(partial);});CHECK(timed.wait_for(7s)==std::future_status::ready);CHECK(timed.get().status==400);
    auto cancelled=connect_to(port);send(cancelled,incomplete,sizeof(incomplete)-1,0);closesocket(cancelled);
    CHECK(request(port,"/goldcraft/models/test.mdl").body==*data);CHECK(s.stats().rejected>=10);s.stop();
}
static void store_tests(){
    Store store(16);store.publish({{"models/a.mdl",8},{"models/b.mdl",8},{"models/c.mdl",8},{"server.cfg",4},{"models/zero.mdl",0}});CHECK(store.stats().files==3);CHECK(!store.request("server.cfg"));
    auto a=std::async(std::launch::async,[&]{return store.request("models/a.mdl");});auto a2=std::async(std::launch::async,[&]{return store.request("models/a.mdl");});std::shared_ptr<Load> load;until([&]{load=store.take();return !!load;});load->bytes->assign(8,7);store.finish(load,true);load.reset();auto body=a.get();CHECK(body&&body==a2.get());CHECK(store.stats().loads==1);
    auto fill=[&](const char* name){auto f=std::async(std::launch::async,[&]{return store.request(name);});until([&]{load=store.take();return !!load;});load->bytes->assign(8,9);store.finish(load,true);load.reset();return f.get();};
    auto b=fill("models/b.mdl");CHECK(store.stats().reserved==16);CHECK(!store.request("models/c.mdl"));b.reset();auto c=fill("models/c.mdl");CHECK(c&&body&&store.stats().reserved==16);
    // A new map cannot revoke a body already returned to a sender; its memory
    // remains counted until that sender releases it.
    store.publish({{"models/a.mdl",8}});CHECK(store.stats().reserved==16);CHECK(!store.request("models/a.mdl"));body.reset();c.reset();CHECK(store.stats().reserved==0);
    a=std::async(std::launch::async,[&]{return store.request("models/a.mdl");});until([&]{load=store.take();return !!load;});store.publish({{"models/b.mdl",8}});CHECK(!a.get());load->bytes->assign(8,1);store.finish(load,true);load.reset();CHECK(store.stats().reserved==0);CHECK(!store.request("models/a.mdl"));
    a=std::async(std::launch::async,[&]{return store.request("models/b.mdl");});until([&]{load=store.take();return !!load;});load->bytes->assign(7,1);store.finish(load,true);load.reset();CHECK(!a.get());
}
static void adapter_tests(){
    for(auto v:{&sv_downloadurl,&sv_allow_download,&sv_send_resources,&sv_allow_dlfile})Cvar_RegisterVariable(v);
    // Reserve a free port without relying on a fixed machine-wide test port.
    Server probe;CHECK(probe.start("127.0.0.1",0,{}));auto port=probe.port();probe.stop();
    for(auto key:{"GOLDCRAFT_HTTP_ENABLED","GOLDCRAFT_HTTP_BIND","GOLDCRAFT_HTTP_PORT","GOLDCRAFT_HTTP_PUBLIC_URL"})_putenv_s(key,"");
    GC_HttpInit();CHECK(cvars.at("mc_http_download")->value==1);Cvar_Set("mc_http_port",std::to_string(port).c_str());Cvar_Set("mc_http_bind","127.0.0.1");
    files["models/a.mdl"].bytes=pattern(1024*1024+27);files["sound/a.wav"].bytes=pattern(37,1);files["sprites/test.spr"].bytes=pattern(13,2);
    add("models/a.mdl",t_model,files["models/a.mdl"].bytes.size());add("a.wav",t_sound,37);add("sprites/test.spr",t_generic,13);add("server.cfg",t_generic,20);add("*1",t_model,20);add("!sentence",t_sound,20);add("spray.wad",t_generic,20,RES_CUSTOM);add("decal",t_decal,20);
    const char* csol_resources[]{"models/v_weapon.json", "models/$0a_sphere.tga", "models/texture/hand.bmp",
        "shader/@004.txt", "shader/env.txt", "materials/common.json", "materials/weapon/base.tga", "gfx/tattoo/tattoo.cso"};
    for(const auto* name:csol_resources){files[name].bytes=pattern(31,3);add(name,t_generic,31);}
    files["models/texture/#512512hand.bmp"].bytes=pattern(41,4);add("models/texture/#512512hand.bmp",t_generic,41);
    GC_HttpResources();frame();CHECK(std::string(sv_downloadurl.string).empty());
    auto f=async_request(port,"models/a.mdl");CHECK(pump(f).body==files["models/a.mdl"].bytes);CHECK(largest_read_frame==256*1024&&seeks>=5&&opens==closes);
    f=async_request(port,"sound/a.wav");CHECK(pump(f).body==files["sound/a.wav"].bytes);
    f=async_request(port,"sprites/test.spr");CHECK(pump(f).body==files["sprites/test.spr"].bytes);
    for(const auto* name:csol_resources){f=async_request(port,name);auto response=pump(f);CHECK(response.status==200 && response.body==files[name].bytes);}
    f=async_request(port,"models/texture/%23512512hand.bmp");CHECK(pump(f).body==files["models/texture/#512512hand.bmp"].bytes);
    f=async_request(port,"models/texture/#512512hand.bmp");CHECK(pump(f).body==files["models/texture/#512512hand.bmp"].bytes);
    f=async_request(port,"models/texture/#512512hand.bmp#extra");CHECK(pump(f).status==404);
    f=async_request(port,"models/not_precached.json");CHECK(pump(f).status==404);
    f=async_request(port,"spray.wad");CHECK(pump(f).status==404);
    Cvar_Set("sv_allow_dlfile","0");Cvar_Set("mc_http_public_url","http://example.invalid:12345/goldcraft/");realtime+=2;frame();CHECK(std::string(sv_downloadurl.string)=="http://example.invalid:12345/goldcraft/");CHECK(sv_allow_dlfile.value==0);
    Cvar_Set("sv_downloadurl","http://admin.invalid/");Cvar_Set("mc_http_public_url","");realtime+=2;frame();CHECK(std::string(sv_downloadurl.string)=="http://admin.invalid/");
    Cvar_Set("sv_allowdownload","0");frame();CHECK(opens==closes);CHECK(probe.start("127.0.0.1",port,{}));probe.stop();Cvar_Set("sv_allowdownload","1");frame();
    Cvar_Set("sv_send_resources","0");frame();CHECK(probe.start("127.0.0.1",port,{}));probe.stop();Cvar_Set("sv_send_resources","1");frame();
    // Refresh clears immutable cached bytes; a source mutation during a read
    // is rejected instead of returning a mixture of map generations.
    commands.at("mc_http_refresh")();f=async_request(port,"models/a.mdl");until([&]{frame();return read_frame>0;});++files["models/a.mdl"].time;CHECK(pump(f).status==404);
    commands.at("mc_http_refresh")();f=async_request(port,"models/a.mdl");CHECK(pump(f).body==files["models/a.mdl"].bytes);
    commands.at("mc_http_refresh")();files["models/a.mdl"].broken=true;f=async_request(port,"models/a.mdl");CHECK(pump(f).status==404);CHECK(opens==closes);files["models/a.mdl"].broken=false;
    GC_HttpReset();f=async_request(port,"models/a.mdl");CHECK(pump(f).status==404);
    GC_HttpResources();f=async_request(port,"models/a.mdl");until([&]{frame();return read_frame>0;});auto began=std::chrono::steady_clock::now();GC_HttpShutdown();CHECK(std::chrono::steady_clock::now()-began<1500ms);f.get();CHECK(opens==closes);
}
int main(){
    WSADATA w;CHECK(WSAStartup(MAKEWORD(2,2),&w)==0);
    for(const auto& u:{"http://example.com/goldcraft/","http://127.0.0.1:27080/goldcraft/"})CHECK(public_url(u));
    for(const auto& u:{"http://localhost:0/goldcraft/","https://example.com/goldcraft/","http://user@x/goldcraft/","http://x/other/","http://x:65536/goldcraft/","http://x/goldcraft/;quit"})CHECK(!public_url(u));
    for(const auto* name:{"models/server.cfg", "models/plugin.dll", "materials/../server.json", "shader/program.glsl", "gfx/tattoo/other.cso", "config/private.json"})CHECK(!resource_name(name));
    server_tests();store_tests();adapter_tests();WSACleanup();
    std::cout<<"HTTP production server/store/adapter checks="<<checks.load()<<" passed\n";
}
