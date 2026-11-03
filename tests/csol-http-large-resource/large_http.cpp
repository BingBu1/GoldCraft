// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the existing engine API fixture and Winsock client, but do not rerun
// its unrelated transport/backpressure suites. Production TUs are unchanged.
#define main previous_http_fixture_main
#include "http_download_tests.cpp"
#undef main
#include "launcher_http.h"
#include <filesystem>
#include <fstream>

static unsigned behavior_checks;
#define REQUIRE(x) do { ++behavior_checks; CHECK(x); } while (false)

static Bytes read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(bool(file));
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

static void exact_length(const Response& response, std::size_t expected)
{
    const auto line = "Content-Length: " + std::to_string(expected) + "\r\n";
    REQUIRE((response.header + "\r\n").find(line) != std::string::npos);
}

static void store_boundaries()
{
    constexpr std::size_t limit = 64u * 1024 * 1024;
    REQUIRE(max_file_size == limit);
    Store store;
    store.publish({{"models/at_limit.mdl", limit}, {"models/above_limit.mdl", limit+1},
        {"models/one.mdl", 1}, {"models/zero.mdl", 0}});
    REQUIRE(store.stats().files == 2);
    REQUIRE(!store.request("models/above_limit.mdl"));
    REQUIRE(!store.request("models/zero.mdl"));
    auto request = std::async(std::launch::async, [&]{ return store.request("models/at_limit.mdl"); });
    std::shared_ptr<Load> pending;
    until([&]{ pending=store.take(); return bool(pending); });
    REQUIRE(pending->size == limit && store.stats().reserved == limit);
    pending->bytes->assign(limit, 0x93);
    store.finish(pending, true);
    pending.reset();
    auto held = request.get();
    REQUIRE(held && held->size() == limit && held->front() == 0x93 && held->back() == 0x93);
    REQUIRE(!store.request("models/one.mdl"));
    REQUIRE(store.stats().reserved == limit);
    // A map transition cancels the cache but cannot release bytes held by an
    // in-flight sender; the same fixed budget still applies to the next map.
    store.publish({{"models/one.mdl", 1}});
    REQUIRE(store.stats().reserved == limit && !store.request("models/one.mdl"));
    held.reset();
    REQUIRE(store.stats().reserved == 0);
    request=std::async(std::launch::async,[&]{return store.request("models/one.mdl");});
    until([&]{pending=store.take();return bool(pending);});
    store.publish({});
    REQUIRE(!request.get());
    pending->bytes->assign(1,42);
    store.finish(pending,true);pending.reset();
    REQUIRE(store.stats().reserved == 0 && !store.request("models/one.mdl"));
    Store too_small(22309735);
    too_small.publish({{"resource/Font/sarasa_ui_sc.ttf",22309736}});
    REQUIRE(!too_small.request("resource/Font/sarasa_ui_sc.ttf"));
    REQUIRE(too_small.stats().reserved == 0);
}

static void launcher_limits(const std::filesystem::path& root)
{
    const auto folder=root/"blobs"/std::string(64,'a');
    std::filesystem::create_directories(folder);
    auto resize = [](const std::filesystem::path& path,std::uintmax_t size) {
        std::ofstream(path,std::ios::binary).put('A');std::filesystem::resize_file(path,size);
    };
    const auto payload=folder/"GoldCraft.dll";
    LauncherFiles launcher;
    REQUIRE(launcher.configure(root));
    const auto name="launcher/v1/blobs/"+std::string(64,'a')+"/GoldCraft.dll";
    resize(payload,(1u<<24)-1);
    auto body=launcher.request(name);
    REQUIRE(body && body->size()==(1u<<24)-1);
    body.reset();resize(payload,1u<<24);
    REQUIRE(!launcher.request(name));
    resize(payload,22309736);
    REQUIRE(!launcher.request(name));
    resize(root/"manifest.json",1024*1024);
    body=launcher.request("launcher/v1/manifest.json");
    REQUIRE(body && body->size()==1024*1024);
    body.reset();resize(root/"manifest.json",1024*1024+1);
    REQUIRE(!launcher.request("launcher/v1/manifest.json"));
}

static void large_adapter(const std::filesystem::path& font_path,const std::filesystem::path& scheme_path,
    const std::filesystem::path& output)
{
    const std::string font="resource/Font/sarasa_ui_sc.ttf";
    const std::string scheme="resource/csol/hud/v1/CSOLHudScheme.res";
    files[font].bytes=read_file(font_path);files[scheme].bytes=read_file(scheme_path);
    REQUIRE(files[font].bytes.size()==22309736);
    for(auto v:{&sv_downloadurl,&sv_allow_download,&sv_send_resources,&sv_allow_dlfile})Cvar_RegisterVariable(v);
    for(auto key:{"GOLDCRAFT_HTTP_ENABLED","GOLDCRAFT_HTTP_BIND","GOLDCRAFT_HTTP_PORT","GOLDCRAFT_HTTP_PUBLIC_URL"})_putenv_s(key,"");
    Server probe;REQUIRE(probe.start("127.0.0.1",0,{}));auto port=probe.port();probe.stop();
    GC_HttpInit();Cvar_Set("mc_http_port",std::to_string(port).c_str());Cvar_Set("mc_http_bind","127.0.0.1");
    add(font.c_str(),t_generic,files[font].bytes.size());add(scheme.c_str(),t_generic,files[scheme].bytes.size());
    for(const auto* invalid:{"resource/csol/hud/v1/other.res","resource/csol/hud/v2/CSOLHudScheme.res",
        "resource/csol/hud/v1/CSOLHudScheme.res.bak","resource/csol/hud/v1/CSOLHudScheme.res.exe",
        "resource/clientscheme.res","resource/trackerscheme.res","resource/private.res"})
    {files[invalid].bytes=Bytes{1,2};add(invalid,t_generic,2);REQUIRE(!resource_name(invalid));}
    REQUIRE(resource_name(scheme));
    REQUIRE(resource_name("RESOURCE/CSOL/HUD/V1/CSOLHUDSCHEME.RES"));
    GC_HttpResources();frame();
    auto future=async_request(port,font);auto response=pump(future);
    REQUIRE(response.status==200);exact_length(response,files[font].bytes.size());
    REQUIRE(response.body==files[font].bytes);
    std::ofstream(output/"font-http-body.bin",std::ios::binary).write(reinterpret_cast<const char*>(response.body.data()),response.body.size());
    std::ofstream(output/"font-http-header.txt")<<response.header;
    REQUIRE(largest_read_frame==256*1024 && seeks>=86 && opens==closes);
    for(const auto& range:{std::string("bytes=16777200-16777280"),std::string("bytes=-4097"),std::string("bytes=22309700-")})
    {
        future=std::async(std::launch::async,[=]{return request(port,"/goldcraft/"+font,"Range: "+range+"\r\n");});
        response=pump(future);REQUIRE(response.status==206);
        const std::size_t start=range=="bytes=16777200-16777280"?16777200:range=="bytes=-4097"?22309736-4097:22309700;
        const std::size_t end=range=="bytes=16777200-16777280"?16777281:22309736;
        REQUIRE(response.body==Bytes(files[font].bytes.begin()+start,files[font].bytes.begin()+end));exact_length(response,end-start);
        REQUIRE(response.header.find("Content-Range: bytes "+std::to_string(start)+"-"+std::to_string(end-1)+"/22309736")!=std::string::npos);
    }
    future=std::async(std::launch::async,[=]{return request(port,"/goldcraft/"+font,"","HEAD");});
    response=pump(future);REQUIRE(response.status==200 && response.body.empty());exact_length(response,22309736);
    future=std::async(std::launch::async,[=]{return request(port,"/goldcraft/"+font,"Range: bytes=22309736-\r\n");});
    REQUIRE(pump(future).status==416);
    future=async_request(port,scheme);response=pump(future);
    REQUIRE(response.status==200 && response.body==files[scheme].bytes);exact_length(response,files[scheme].bytes.size());
    const auto before=opens;
    for(const auto* invalid:{"resource/csol/hud/v1/other.res","resource/csol/hud/v2/CSOLHudScheme.res",
        "resource/csol/hud/v1/CSOLHudScheme.res.bak","resource/csol/hud/v1/CSOLHudScheme.res.exe",
        "resource/clientscheme.res","resource/trackerscheme.res","resource/private.res",
        "resource/csol/hud/v1/../CSOLHudScheme.res","resource/csol/hud/v1/CSOLHudScheme.res?x=1"})
    {future=async_request(port,invalid);REQUIRE(pump(future).status==404);}
    REQUIRE(opens==before);
    // A queued request is cancelled through the actual adapter reset, not by
    // completing a synthetic Store load on its behalf.
    GC_HttpResources();future=async_request(port,font);until([&]{frame();return read_frame>0;});
    GC_HttpReset();REQUIRE(pump(future).status==404 && opens==closes);
    GC_HttpResources();future=async_request(port,font);until([&]{frame();return read_frame>0;});
    files[font].bytes.resize(256*1024);REQUIRE(pump(future).status==404 && opens==closes);
    files[font].bytes=read_file(font_path);
    GC_HttpResources();future=async_request(port,font);until([&]{frame();return read_frame>0;});
    ++files[font].time;REQUIRE(pump(future).status==404 && opens==closes);
    GC_HttpResources();future=async_request(port,font);response=pump(future);
    REQUIRE(response.status==200 && response.body==files[font].bytes);
    GC_HttpShutdown();REQUIRE(opens==closes);
}

int main(int argc,char** argv)
{
    try {
        REQUIRE(argc==4);
        WSADATA data;REQUIRE(WSAStartup(MAKEWORD(2,2),&data)==0);
        const std::filesystem::path output=std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(output);
        store_boundaries();launcher_limits(output/"launcher");large_adapter(argv[1],argv[2],output);
        WSACleanup();std::cout<<"large HTTP behavior="<<behavior_checks<<" engine-boundary="<<checks.load()<<" failed=0\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';GC_HttpShutdown();WSACleanup();return 1;
    }
}
