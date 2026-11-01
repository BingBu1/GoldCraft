#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "launcher_http.h"
#include <fstream>
#include <iostream>
using namespace goldcraft::http;
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << "\n"; std::exit(1); } } while (false)
static std::string get(unsigned short port,const std::string& path) {
    const auto socket_ = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); CHECK(socket_!=INVALID_SOCKET);
    DWORD timeout=3000;setsockopt(socket_,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    CHECK(connect(socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    const auto request="GET "+path+" HTTP/1.1\r\nHost: localhost\r\n\r\n";
    CHECK(send(socket_,request.data(),static_cast<int>(request.size()),0)==request.size());
    std::string response;char bytes[8192];int count;
    while((count=recv(socket_,bytes,sizeof(bytes),0))>0)response.append(bytes,count);
    closesocket(socket_);return response;
}
int main(int argc,char** argv) {
    CHECK(argc>=2);const std::filesystem::path root=std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(root/"blobs"/std::string(64,'a'));
    std::ofstream(root/"manifest.json") << "{\"payload\":\"fixture\",\"signature\":\"fixture\"}";
    std::ofstream(root/"blobs"/std::string(64,'a')/"GoldCraft.dll",std::ios::binary) << "fixture DLL";
    std::ofstream(root/"private.pem") << "must never be served";
    LauncherFiles files;CHECK(files.configure(root));files.publish_status(true,"test \"server\"","zs_deadend",1,32,"localhost:27015");
    CHECK(!resource_name("launcher/v1/manifest.json"));CHECK(launcher_name("launcher/v1/manifest.json"));
    Server server;CHECK(server.start("127.0.0.1",0,[&](const std::string& name)->Body{
        if(launcher_name(name))return files.request(name);
        if(name=="models/fixture.mdl")return std::make_shared<const Bytes>(Bytes{1,2,3});
        return {};
    }));
    auto status=get(server.port(),"/goldcraft/launcher/v1/status.json");
    CHECK(status.find("200 OK")!=std::string::npos&&status.find("zs_deadend")!=std::string::npos&&status.find("\\\"server\\\"")!=std::string::npos);
    CHECK(get(server.port(),"/goldcraft/launcher/v1/manifest.json").find("200 OK")!=std::string::npos);
    CHECK(get(server.port(),"/goldcraft/launcher/v1/blobs/"+std::string(64,'a')+"/GoldCraft.dll").find("fixture DLL")!=std::string::npos);
    for(const auto& path:{"/goldcraft/launcher/v1/private.pem","/goldcraft/launcher/v1/../private.pem","/goldcraft/launcher/v1/manifest.json?x=1","/goldcraft/launcher/v1/blobs/../GoldCraft.dll","/goldcraft/models/private.dll","/goldcraft/launcher/v1/%2e%2e/private.pem"})
        CHECK(get(server.port(),path).find("404 Not Found")!=std::string::npos);
    CHECK(get(server.port(),"/goldcraft/models/fixture.mdl").find("200 OK")!=std::string::npos);
    files.publish_status(false,"","",0,32,"");CHECK(get(server.port(),"/goldcraft/launcher/v1/status.json").find("\"online\":false")!=std::string::npos);
    // The same process can expose a real fixture endpoint for launcher integration/UI tests.
    if(argc==3&&std::string(argv[2])=="--serve") {std::cout << server.port() << std::endl;std::string line;std::getline(std::cin,line);}
    server.stop();std::cout << checks << " checks passed\n";
}
