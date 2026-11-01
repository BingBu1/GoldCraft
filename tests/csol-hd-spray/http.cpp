// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../../native/csol/hd_spray_http_server.hpp"
#include "../../native/csol/client/hd_spray_http.hpp"
#include "../../native/engine/http_download_server.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <functional>
#include <future>
// Pause precisely after production relay registration and before the Winsock
// connect. Only that OS boundary is wrapped; the production parser/relay and
// generation checks below are compiled unchanged from their real source.
std::atomic<bool> pauseConnect{},connectReached{},releaseConnect{};
std::atomic<SOCKET> gatedSocket{INVALID_SOCKET};std::atomic<bool> gatedClosed{};
int EngineConnect(SOCKET s,const sockaddr* address,int size){
    if(pauseConnect.exchange(false)){gatedSocket=s;gatedClosed=false;connectReached=true;while(!releaseConnect)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return ::connect(s,address,size);
}
int EngineClose(SOCKET socket){const auto result=::closesocket(socket);if(socket==gatedSocket.load())gatedClosed=true;return result;}
#define connect EngineConnect
#define closesocket EngineClose
#ifndef GOLDCRAFT_TEST_ENGINE_HTTP_SOURCE
#include "../../native/engine/http_download_server.cpp"
#else
#include GOLDCRAFT_TEST_ENGINE_HTTP_SOURCE
#endif
#undef connect
#undef closesocket
using namespace csol;
using namespace std::chrono_literals;
namespace http=spray::http;
namespace transport= csol::client::spray_http;
std::atomic<unsigned> checks{};
void require(bool b){const auto n=++checks;if(!b)throw std::runtime_error("HTTP check "+std::to_string(n));}
void rejects(const std::function<void()>& f){bool bad=false;try{f();}catch(...){bad=true;}require(bad);}
void wait(const std::shared_ptr<transport::Job>& job){const auto deadline=std::chrono::steady_clock::now()+20s;while(!job->done.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);require(job->done);}
std::shared_ptr<transport::Job> job(unsigned short port,bool upload,const http::Token& token,const spray::Bytes& bytes,const std::string& host="127.0.0.1"){
    auto j=std::make_shared<transport::Job>();j->id=7;j->epoch=100;j->host=host;j->port=port;j->upload=upload;j->size=unsigned(bytes.size());j->checksum=spray::Checksum(bytes);j->path=std::string(upload?"/spray/upload/":"/spray/image/")+spray::Hex(token);if(upload)j->bytes=bytes;return j;
}
SOCKET connectTo(unsigned short port){auto s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);require(s!=INVALID_SOCKET);sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);require(connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);DWORD t=2000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&t),sizeof(t));return s;}
int raw(unsigned short port,const std::string& header){auto s=connectTo(port);require(send(s,header.data(),int(header.size()),0)==int(header.size()));shutdown(s,SD_SEND);char data[1024]{};int n=recv(s,data,sizeof(data)-1,0);closesocket(s);require(n>12);return std::atoi(data+9);}
bool fastdl(unsigned short port){auto s=connectTo(port);const std::string q="GET /goldcraft/models/test.mdl HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ::send(s,q.data(),int(q.size()),0);std::string response;char chunk[16384];int n;while((n=recv(s,chunk,sizeof(chunk),0))>0)response.append(chunk,n);closesocket(s);
    const auto at=response.find("\r\n\r\n");return response.starts_with("HTTP/1.1 200")&&at!=std::string::npos&&response.substr(at+4)==std::string(1024*1024,char(17));
}
// Controlled loopback peer stalls TLS setup, response headers, response body,
// or upload consumption. No outside addresses or timing-dependent Internet.
struct PausedPeer {
    SOCKET listener=INVALID_SOCKET;std::atomic<SOCKET> peer{INVALID_SOCKET};unsigned short port{};
    std::atomic<bool> ready{},stop{};std::thread thread;
    PausedPeer(bool body,unsigned length){
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);require(listener!=INVALID_SOCKET);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        require(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0&&listen(listener,1)==0);
        int n=sizeof(address);require(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&n)==0);port=ntohs(address.sin_port);
        thread=std::thread([&,body,length]{auto socket=accept(listener,nullptr,nullptr);peer=socket;if(socket==INVALID_SOCKET)return;
            DWORD timeout=100;setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
            char request[4096];while(!stop){const int got=recv(socket,request,sizeof(request),0);if(got>0)break;if(got==0){closesocket(socket);peer=INVALID_SOCKET;return;}}
            if(body){const auto h="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(length)+"\r\n\r\nq";::send(socket,h.data(),int(h.size()),0);}
            ready=true;while(!stop)std::this_thread::sleep_for(5ms);closesocket(socket);peer=INVALID_SOCKET;
        });
    }
    ~PausedPeer(){stop=true;shutdown(listener,SD_BOTH);const auto socket=peer.load();if(socket!=INVALID_SOCKET)shutdown(socket,SD_BOTH);if(thread.joinable())thread.join();closesocket(listener);}
};
spray::Bytes tenMiB(){spray::Bytes bytes{'q','a','n','m'};spray::Put32(bytes,1);spray::Put32(bytes,8);spray::Put32(bytes,0);
    for(unsigned f=0;f<8;++f){spray::Bytes frame{'q','o','i','f',0,0,2,0,0,0,2,0,4,0};
        for(unsigned i=0;i<512*512;++i){const bool rgb=f==7&&i<256;frame.push_back(rgb?254:255);frame.insert(frame.end(),{1,2,3});if(!rgb)frame.push_back(255);}
        frame.insert(frame.end(),{0,0,0,0,0,0,0,1});spray::Put32(bytes,100);spray::Put32(bytes,unsigned(frame.size()));bytes.insert(bytes.end(),frame.begin(),frame.end());}
    require(bytes.size()==spray::MaxBytes);return bytes;
}
int main(){try{
    http::Server server;require(server.Start("127.0.0.1",0));transport::Worker worker;
    goldcraft::http::Server gateway;auto model=std::make_shared<const goldcraft::http::Bytes>(1024*1024,17);
    require(gateway.start("127.0.0.1",0,[&](const std::string& name){return name=="models/test.mdl"?model:goldcraft::http::Body{};}));
    gateway.spray_backend(server.Port());const auto port=gateway.port();require(port&&port!=server.Port());
    require(http::SharedEndpoint("http://tunnel.example.test:12345/goldcraft/")=="http://tunnel.example.test:12345/spray/");
    require(http::SharedEndpoint("").empty());rejects([]{http::SharedEndpoint("http://host/other/");});
    for(const auto* url:{"http://127.0.0.1:10080/spray/","http://localhost:443/spray/","https://sprays.example.test:9443/spray/"}){auto e=http::ParseEndpoint(url);require(!e.host.empty()&&e.port);}
    for(const auto* url:{"http://user:password@host/spray/","file:///spray/","http://host:0/spray/","http://host:65536/spray/","http://host/../spray/","http://host/spray/?x=1","http://host/spray/#x","http://host\r\n/spray/","http://host..x/spray/"})rejects([&]{http::ParseEndpoint(url);});
    const auto bytes=tenMiB();auto upload=server.Offer(unsigned(bytes.size()),spray::Checksum(bytes),{1024,1024});require(server.Reserved()==bytes.size());
    auto send=job(port,true,upload->token,bytes,"localhost");auto start=std::chrono::steady_clock::now();require(worker.Submit(send));auto resource=std::async(std::launch::async,[&]{return fastdl(port);});wait(send);require(resource.get());
    const double uploadSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();require(send->success&&send->transferred==bytes.size());require(upload->state==http::State::Ready&&upload->body->bytes==bytes);
    auto token=server.Publish(upload->body);auto a=job(port,false,token,bytes),b=job(port,false,token,bytes,"localhost");start=std::chrono::steady_clock::now();require(worker.Submit(a)&&worker.Submit(b));wait(a);wait(b);
    const double downloads=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();require(a->success&&b->success&&a->animation->frames.size()==8&&b->animation->frames.size()==8);
    require(a->animation->frames[0].image.RGBA==send->animation->frames[0].image.RGBA);
    auto replay=job(port,true,upload->token,bytes);require(worker.Submit(replay));wait(replay);require(!replay->success);
    auto wrong=token;wrong[0]^=1;auto denied=job(port,false,wrong,bytes);require(worker.Submit(denied));wait(denied);require(!denied->success);
    const auto tiny=spray::Encode(Image{1,1,{255,0,0,255}});auto pending=server.Offer(unsigned(tiny.size()),spray::Checksum(tiny),{1,1});
    const auto path="/spray/upload/"+spray::Hex(pending->token);
    require(raw(port,"PUT "+path+" HTTP/1.1\r\nContent-Length: 10485761\r\n\r\n")==400);
    require(raw(port,"PUT "+path+" HTTP/1.1\r\nContent-Length: 22\r\nContent-Length: 22\r\n\r\n")==400);
    require(raw(port,"PUT "+path+" HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n")==400);
    require(raw(port,"PUT /spray/upload/../../secret HTTP/1.1\r\nContent-Length: 22\r\n\r\n")==404);
    require(raw(port,"POST "+path+" HTTP/1.1\r\nContent-Length: 22\r\n\r\n")==404);
    require(raw(port,"PUT /goldcraft/models/test.mdl HTTP/1.1\r\nContent-Length: 22\r\n\r\n")==405);
    require(raw(port,"PUT "+path+" HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 22\r\n\r\n")==400);
    auto coalesced=server.Offer(unsigned(tiny.size()),spray::Checksum(tiny),{1,1});
    require(raw(port,"PUT /spray/upload/"+spray::Hex(coalesced->token)+" HTTP/1.1\r\nContent-Length: "+std::to_string(tiny.size())+"\r\n\r\n"+std::string(tiny.begin(),tiny.end()))==200);
    require(coalesced->state==http::State::Ready&&coalesced->body->bytes==tiny);
    // Deterministic ABA: withdraw the route before connect, then restore the
    // same port. Checking port equality alone would accept this old upload.
    auto retired=server.Offer(unsigned(tiny.size()),spray::Checksum(tiny),{1,1});
    const auto retiredSocket=connectTo(port);const auto retiredRequest="PUT /spray/upload/"+spray::Hex(retired->token)+" HTTP/1.1\r\nContent-Length: "+std::to_string(tiny.size())+"\r\n\r\n"+std::string(tiny.begin(),tiny.end());
    connectReached=false;releaseConnect=false;pauseConnect=true;require(::send(retiredSocket,retiredRequest.data(),int(retiredRequest.size()),0)==int(retiredRequest.size()));
    const auto connectBy=std::chrono::steady_clock::now()+2s;while(!connectReached&&std::chrono::steady_clock::now()<connectBy)std::this_thread::sleep_for(1ms);
    const bool reached=connectReached;gateway.spray_backend(0);gateway.spray_backend(server.Port());releaseConnect=true;require(reached);
    char retiredResponse[64];recv(retiredSocket,retiredResponse,sizeof(retiredResponse),0);closesocket(retiredSocket);
    // Observe production cleanup of this exact backend socket before checking
    // the ticket. Stopping the gateway here would hide the original race.
    const auto closedBy=std::chrono::steady_clock::now()+2s;while(!gatedClosed&&std::chrono::steady_clock::now()<closedBy)std::this_thread::sleep_for(1ms);
    require(gatedClosed);
    if(retired->state!=http::State::Waiting)throw std::runtime_error("ABA: retired upload ticket changed after same-port route replacement");
    require(retired->state==http::State::Waiting);gatedSocket=INVALID_SOCKET;
    pending->cancelled=true;auto cancel=job(port,true,pending->token,tiny);require(worker.Submit(cancel));wait(cancel);require(!cancel->success);
    auto corrupt=server.Offer(unsigned(tiny.size()),spray::Checksum(tiny)^1,{1,1});auto bad=job(port,true,corrupt->token,tiny);require(worker.Submit(bad));wait(bad);require(!bad->success&&corrupt->state==http::State::Failed);
    auto mismatch=server.Offer(unsigned(tiny.size()),spray::Checksum(tiny),{512,512});auto aspect=job(port,true,mismatch->token,tiny);require(worker.Submit(aspect));wait(aspect);require(!aspect->success&&mismatch->state==http::State::Failed);
    std::vector<SOCKET> slow;std::vector<std::shared_ptr<http::Upload>> slowUploads;
    for(int i=0;i<4;++i){auto stalled=server.Offer(unsigned(bytes.size()),spray::Checksum(bytes),{1024,1024});auto s=connectTo(port);
        const auto header="PUT /spray/upload/"+spray::Hex(stalled->token)+" HTTP/1.1\r\nContent-Length: "+std::to_string(bytes.size())+"\r\n\r\n";require(::send(s,header.data(),int(header.size()),0)>0);slow.push_back(s);slowUploads.push_back(stalled);}
    const auto until=std::chrono::steady_clock::now()+2s;while(std::any_of(slowUploads.begin(),slowUploads.end(),[](auto& u){return u->state!=http::State::Receiving;})&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
    for(auto& u:slowUploads)require(u->state==http::State::Receiving);
    require(fastdl(port));require(raw(port,"GET /spray/image/"+spray::Hex(token)+" HTTP/1.1\r\n\r\n")==503);
    start=std::chrono::steady_clock::now();gateway.spray_backend(0);server.Stop();for(auto s:slow)closesocket(s);
    require(std::chrono::steady_clock::now()-start<2s);for(auto& u:slowUploads)require(u->cancelled);
    require(fastdl(port));require(raw(port,"GET /spray/image/"+spray::Hex(token)+" HTTP/1.1\r\n\r\n")==503);
    require(server.Start("127.0.0.1",0));gateway.spray_backend(server.Port());auto stale=job(port,false,token,bytes);require(worker.Submit(stale));wait(stale);require(!stale->success);
    std::vector<std::shared_ptr<http::Upload>> reserved;while(server.Reserved()+bytes.size()<=spray::CacheBytes)reserved.push_back(server.Offer(unsigned(bytes.size()),0,{1024,1024}));rejects([&]{server.Offer(unsigned(bytes.size()),0,{1024,1024});});
    auto cancelled=job(server.Port(),false,token,bytes);cancelled->cancelled=true;require(worker.Submit(cancelled));wait(cancelled);require(!cancelled->success);
    double maxCancel=0;
    for(unsigned mode=0;mode<4;++mode){PausedPeer stalled(mode==2,unsigned(bytes.size()));transport::Worker cancellable;
        auto pendingJob=job(stalled.port,mode==3,token,bytes);pendingJob->secure=mode==0;
        require(cancellable.Submit(pendingJob));const auto readyBy=std::chrono::steady_clock::now()+3s;
        while(!stalled.ready&&std::chrono::steady_clock::now()<readyBy)std::this_thread::sleep_for(1ms);require(stalled.ready);
        std::this_thread::sleep_for(100ms);require(!pendingJob->done);start=std::chrono::steady_clock::now();
        if(mode%2)cancellable.Shutdown();else {cancellable.Cancel();wait(pendingJob);cancellable.Shutdown();}
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();maxCancel=std::max(maxCancel,elapsed);
        require(pendingJob->done&&pendingJob->cancelled&&!pendingJob->success&&elapsed<2);
    }
    worker.Shutdown();gateway.stop();server.Stop();
    std::cout<<checks<<" shared-port HTTP/WinHTTP checks passed; upload10MiB="<<uploadSeconds<<"s; two parallel downloads="<<downloads<<"s; maxCancelAndJoin="<<maxCancel<<"s; concurrent FastDL/spray limits checked; game/Internet not measured\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
