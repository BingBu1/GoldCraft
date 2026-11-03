// Real HTTP adapter and Winsock; only the new sentence-table route is tested.
#define main prior_fixture_main
#include "http_download_tests.cpp"
#undef main
#include <filesystem>
#include <fstream>

static unsigned behavior;
#define REQUIRE(x) do { ++behavior; CHECK(x); } while(false)

int main(int argc,char** argv)
{
    REQUIRE(argc==4);
    const bool fixed=std::string_view(argv[2])=="fixed";
    const std::filesystem::path output=argv[3];std::filesystem::create_directories(output);
    WSADATA ws{};REQUIRE(WSAStartup(MAKEWORD(2,2),&ws)==0);
    std::ifstream source(argv[1],std::ios::binary);REQUIRE(bool(source));
    Bytes payload(std::istreambuf_iterator<char>(source),{});REQUIRE(payload.size()==146922);
    const std::string route="sound/sentences.json", upper="SOUND/SENTENCES.JSON", ordinary="sound/emotion/test.wav";
    for(const auto& name:{route,upper,ordinary})files[name].bytes=payload;
    for(auto v:{&sv_downloadurl,&sv_allow_download,&sv_send_resources,&sv_allow_dlfile})Cvar_RegisterVariable(v);
    for(auto key:{"GOLDCRAFT_HTTP_ENABLED","GOLDCRAFT_HTTP_BIND","GOLDCRAFT_HTTP_PORT","GOLDCRAFT_HTTP_PUBLIC_URL"})_putenv_s(key,"");
    Server probe;REQUIRE(probe.start("127.0.0.1",0,{}));auto port=probe.port();probe.stop();
    GC_HttpInit();Cvar_Set("mc_http_port",std::to_string(port).c_str());Cvar_Set("mc_http_bind","127.0.0.1");
    add(ordinary.c_str(),t_generic,payload.size());
    REQUIRE(resource_name(route)==fixed);REQUIRE(resource_name(upper)==fixed);
    REQUIRE(resource_name(ordinary));
    GC_HttpResources();frame();
    auto prior_opens=opens;
    auto pending=async_request(port,route);auto response=pump(pending);
    REQUIRE(response.status==404 && response.body.empty());REQUIRE(opens==prior_opens);
    for(const auto& name:{route,upper})add(name.c_str(),t_generic,payload.size());
    const std::array invalid{
        "sound/private.json","sound/other/sentences.json","sound/sentences.json.exe",
        "sound/sentences.json.bak","sound/sentences.json/other.json","sound//sentences.json",
        "sound/./sentences.json","sound/../sentences.json","sound/sentences.json:stream",
        "sound\\sentences.json","sound/sentences.json.","sound/sentences.json ","soundx/sentences.json"};
    for(const auto* value:invalid){files[value].bytes={1,2,3};add(value,t_generic,3);REQUIRE(!resource_name(value));}
    GC_HttpResources();frame();
    pending=async_request(port,route);response=pump(pending);
    REQUIRE(response.status==(fixed?200:404));
    std::ofstream(output/"sentences-status.txt")<<response.status<<'\n';
    std::ofstream(output/"sentences-header.txt")<<response.header;
    std::ofstream(output/"sentences-body.bin",std::ios::binary).write(reinterpret_cast<const char*>(response.body.data()),response.body.size());
    if(fixed){REQUIRE(response.body==payload);REQUIRE(response.header.find("Content-Length: 146922")!=std::string::npos);}
    else REQUIRE(response.body.empty());
    pending=async_request(port,ordinary);response=pump(pending);REQUIRE(response.status==200&&response.body==payload);
    pending=async_request(port,upper);response=pump(pending);REQUIRE(response.status==(fixed?200:404));
    if(fixed){
        REQUIRE(response.body==payload);
        pending=std::async(std::launch::async,[=]{return request(port,"/goldcraft/"+route,"","HEAD");});
        response=pump(pending);REQUIRE(response.status==200&&response.body.empty());
        REQUIRE(response.header.find("Content-Length: 146922")!=std::string::npos);
        pending=std::async(std::launch::async,[=]{return request(port,"/goldcraft/"+route,"Range: bytes=146900-\r\n");});
        response=pump(pending);REQUIRE(response.status==206);REQUIRE(response.body==Bytes(payload.begin()+146900,payload.end()));
        REQUIRE(response.header.find("Content-Range: bytes 146900-146921/146922")!=std::string::npos);
    }
    prior_opens=opens;
    for(const auto* value:invalid){pending=async_request(port,value);REQUIRE(pump(pending).status==404);}
    for(const auto* value:{"sound/%2e%2e/sentences.json","sound/sentences.json?x=1",
        "sound/sentences.json#x","sound/sentences.json%00.exe","sound%2fsentences.json"})
    {pending=async_request(port,value);REQUIRE(pump(pending).status==404);}
    REQUIRE(opens==prior_opens);
    GC_HttpReset();pending=async_request(port,route);REQUIRE(pump(pending).status==404);
    GC_HttpShutdown();REQUIRE(opens==closes);WSACleanup();
    std::cout<<"sentences expected="<<(fixed?200:404)<<" behavior="<<behavior<<" engine-boundary="<<checks.load()<<" failed=0\n";
}
