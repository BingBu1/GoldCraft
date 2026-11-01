// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include "hd_spray_http_server.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <charconv>

namespace csol::spray::http {
namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned Workers=8;
Token RandomToken(){Token token{};if(BCryptGenRandom(nullptr,token.data(),ULONG(token.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("spray random source failed");return token;}
bool Retry(){const auto e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAETIMEDOUT||e==WSAEINTR;}
void Timeout(SOCKET s){DWORD ms=250;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&ms),sizeof(ms));}
struct Lease {std::shared_ptr<std::atomic<size_t>> counter;size_t size;~Lease(){*counter-=size;}};
std::string Lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
struct Request {bool put{};std::string token;unsigned length{};size_t bodyAt{};};
Request Parse(const std::string& header){
    const auto end=header.find("\r\n\r\n"),first=header.find("\r\n");Check(end!=std::string::npos&&end<=8192&&first<end);
    const auto line=header.substr(0,first);const bool put=line.starts_with("PUT "),get=line.starts_with("GET ");Check(put||get);
    const auto start=line.find(' ')+1,space=line.find(' ',start);Check(space!=std::string::npos&&line.substr(space)==" HTTP/1.1");
    const auto target=line.substr(start,space-start);const std::string prefix=put?"/spray/upload/":"/spray/image/";
    Check(target.starts_with(prefix)&&target.size()==prefix.size()+32);const auto token=target.substr(prefix.size());
    Check(token.find_first_not_of("0123456789abcdef")==std::string::npos);
    bool haveLength=false;unsigned length=0;
    for(auto pos=first+2;pos<end;){const auto next=header.find("\r\n",pos),colon=header.find(':',pos);Check(next<=end&&colon<next&&colon>pos);
        const auto key=Lower(header.substr(pos,colon-pos));Check(key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos);
        auto value=header.substr(colon+1,next-colon-1);Check(std::none_of(value.begin(),value.end(),[](unsigned char c){return (c<32&&c!='\t')||c==127;}));
        const auto a=value.find_first_not_of(" \t"),b=value.find_last_not_of(" \t");value=a==std::string::npos?"":value.substr(a,b-a+1);
        Check(key!="transfer-encoding"&&key!="expect");
        if(key=="content-length"){Check(!haveLength&&!value.empty());const auto r=std::from_chars(value.data(),value.data()+value.size(),length);Check(r.ec==std::errc{}&&r.ptr==value.data()+value.size());haveLength=true;}
        pos=next+2;
    }
    Check(put?(haveLength&&length>=22&&length<=MaxBytes):length==0);
    return {put,token,length,end+4};
}
}
struct Server::Impl {
    std::atomic<bool> stopping{true};SOCKET listener=INVALID_SOCKET;unsigned short port{};bool winsock{};
    std::thread acceptor;std::vector<std::thread> workers;std::mutex mutex;std::condition_variable wake;
    std::deque<SOCKET> queue;std::unordered_set<SOCKET> connections;
    struct Offered {std::weak_ptr<Upload> upload;Clock::time_point expires;};
    std::unordered_map<std::string,Offered> uploads;std::unordered_map<std::string,std::weak_ptr<const Payload>> assets;
    std::shared_ptr<std::atomic<size_t>> reserved=std::make_shared<std::atomic<size_t>>(0);
    void Prune(){std::erase_if(uploads,[](const auto& x){return x.second.upload.expired()||Clock::now()>x.second.expires;});std::erase_if(assets,[](const auto& x){return x.second.expired();});}
    bool Send(SOCKET socket,const void* data,size_t length,Clock::time_point deadline){
        const auto* p=static_cast<const char*>(data);auto progress=Clock::now();
        while(length&&!stopping&&Clock::now()<deadline&&Clock::now()-progress<std::chrono::seconds(5)){
            const int n=send(socket,p,int(std::min(length,size_t(64*1024))),0);
            if(n>0){p+=n;length-=n;progress=Clock::now();}else if(n==0||!Retry())return false;
        }return length==0;
    }
    void Reply(SOCKET socket,int code,size_t length=0){
        const auto head="HTTP/1.1 "+std::to_string(code)+(code==200?" OK":" Rejected")+"\r\nContent-Type: application/octet-stream\r\nContent-Length: "+std::to_string(length)+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
        Send(socket,head.data(),head.size(),Clock::now()+std::chrono::seconds(5));
    }
    void Serve(SOCKET socket){
        std::shared_ptr<Upload> upload;
        try{
            std::string head;std::array<char,1024> buffer;const auto headerDeadline=Clock::now()+std::chrono::seconds(5);
            while(head.find("\r\n\r\n")==std::string::npos){Check(!stopping&&Clock::now()<headerDeadline&&head.size()<8192);
                const int n=recv(socket,buffer.data(),int(buffer.size()),0);if(n>0)head.append(buffer.data(),n);else Check(n!=0&&Retry());}
            const auto request=Parse(head);
            if(!request.put){Body body;{std::lock_guard lock(mutex);auto it=assets.find(request.token);if(it!=assets.end())body=it->second.lock();}
                if(!body){Reply(socket,404);return;}Check(head.size()==request.bodyAt);Reply(socket,200,body->bytes.size());Send(socket,body->bytes.data(),body->bytes.size(),Clock::now()+std::chrono::seconds(120));return;
            }
            {std::lock_guard lock(mutex);Prune();const auto it=uploads.find(request.token);if(it!=uploads.end())upload=it->second.upload.lock();}
            if(!upload||upload->cancelled){Reply(socket,403);return;}
            if(request.length!=upload->size){Reply(socket,413);return;}
            auto expected=State::Waiting;if(!upload->state.compare_exchange_strong(expected,State::Receiving)){Reply(socket,409);return;}
            Bytes data;data.reserve(request.length);Check(head.size()-request.bodyAt<=request.length);
            data.insert(data.end(),head.begin()+request.bodyAt,head.end());std::array<char,64*1024> chunk;
            auto progress=Clock::now();const auto deadline=progress+std::chrono::seconds(120);
            while(data.size()<request.length){Check(!stopping&&!upload->cancelled&&Clock::now()<deadline&&Clock::now()-progress<std::chrono::seconds(5));
                const int n=recv(socket,chunk.data(),int(std::min(chunk.size(),size_t(request.length)-data.size())),0);
                if(n>0){data.insert(data.end(),chunk.begin(),chunk.begin()+n);progress=Clock::now();}else Check(n!=0&&Retry());}
            Check(Checksum(data)==upload->checksum);CheckDimensions(upload->original,DecodeAnimation(data,false));
            Check(!stopping&&!upload->cancelled);upload->body=std::make_shared<const Payload>(Payload{std::move(data),upload->reservation});
            upload->state.store(State::Ready,std::memory_order_release);Reply(socket,200);
        }catch(...){if(upload&&upload->state.load()==State::Receiving)upload->state.store(State::Failed,std::memory_order_release);Reply(socket,400);}
    }
};
Server::Server():impl(std::make_unique<Impl>()){}
Server::~Server(){Stop();}
bool Server::Start(const std::string& bind,unsigned short port){
    Stop();auto& p=*impl;
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return false;p.winsock=true;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
    if(inet_pton(AF_INET,bind.c_str(),&address.sin_addr)!=1){Stop();return false;}
    p.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(p.listener==INVALID_SOCKET){Stop();return false;}
    BOOL exclusive=TRUE;setsockopt(p.listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive));
    if(::bind(p.listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(p.listener,16)){Stop();return false;}
    int size=sizeof(address);if(getsockname(p.listener,reinterpret_cast<sockaddr*>(&address),&size)){Stop();return false;}
    p.port=ntohs(address.sin_port);p.stopping=false;
    try{
        for(unsigned i=0;i<Workers;++i)p.workers.emplace_back([&p]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
            for(;;){SOCKET s;{std::unique_lock lock(p.mutex);p.wake.wait(lock,[&]{return p.stopping||!p.queue.empty();});if(p.stopping)return;s=p.queue.front();p.queue.pop_front();}
                p.Serve(s);std::lock_guard lock(p.mutex);p.connections.erase(s);closesocket(s);}
        });
        p.acceptor=std::thread([&p]{
            while(!p.stopping){fd_set ready;FD_ZERO(&ready);FD_SET(p.listener,&ready);timeval wait{0,100000};if(select(0,&ready,nullptr,nullptr,&wait)<=0)continue;
                auto s=accept(p.listener,nullptr,nullptr);if(s==INVALID_SOCKET)continue;Timeout(s);bool accepted=false;
                {std::lock_guard lock(p.mutex);if(!p.stopping&&p.connections.size()<Workers){p.connections.insert(s);p.queue.push_back(s);accepted=true;}}
                if(accepted)p.wake.notify_one();else {const char text[]="HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send(s,text,sizeof(text)-1,0);closesocket(s);}
            }
        });
    }catch(...){Stop();return false;}
    return true;
}
void Server::Stop(){auto& p=*impl;p.stopping=true;
    if(p.acceptor.joinable())p.acceptor.join();if(p.listener!=INVALID_SOCKET){closesocket(p.listener);p.listener=INVALID_SOCKET;}
    {std::lock_guard lock(p.mutex);for(auto& [key,value]:p.uploads)if(auto upload=value.upload.lock())upload->cancelled=true;
        for(auto socket:p.connections)shutdown(socket,SD_BOTH);p.uploads.clear();p.assets.clear();}
    p.wake.notify_all();for(auto& worker:p.workers)worker.join();p.workers.clear();
    for(auto socket:p.queue)closesocket(socket);p.queue.clear();p.connections.clear();p.port=0;
    if(p.winsock){WSACleanup();p.winsock=false;}
}
unsigned short Server::Port()const{return impl->port;}
size_t Server::Reserved()const{return impl->reserved->load();}
std::shared_ptr<Upload> Server::Offer(unsigned size,unsigned checksum,Dimensions original){
    Check(impl->port&&size>=22&&size<=MaxBytes);TextureDimensions(original);
    const auto token=RandomToken();auto counter=impl->reserved;auto lease=std::make_shared<Lease>();lease->counter=counter;lease->size=0;auto old=counter->load();
    do {Check(old<=CacheBytes&&size<=CacheBytes-old);}while(!counter->compare_exchange_weak(old,old+size));
    lease->size=size;
    auto upload=std::make_shared<Upload>();upload->token=token;upload->size=size;upload->checksum=checksum;upload->original=original;upload->reservation=std::move(lease);
    std::lock_guard lock(impl->mutex);impl->Prune();impl->uploads[Hex(token)]={upload,Clock::now()+std::chrono::seconds(120)};return upload;
}
Token Server::Publish(Body body){Check(impl->port&&body&&!body->bytes.empty()&&body->bytes.size()<=MaxBytes);
    const auto token=RandomToken();std::lock_guard lock(impl->mutex);impl->Prune();impl->assets[Hex(token)]=std::move(body);return token;
}
}
