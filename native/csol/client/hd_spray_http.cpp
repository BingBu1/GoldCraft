// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winhttp.h>
#include "hd_spray_http.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <charconv>

namespace csol::client::spray_http {
namespace {
struct Handle {HINTERNET value{};~Handle(){if(value)WinHttpCloseHandle(value);}operator HINTERNET()const{return value;}};
void Check(bool ok){if(!ok)throw std::runtime_error("spray HTTP transfer failed");}
using Clock=std::chrono::steady_clock;
struct AsyncRequest {
    HINTERNET value{};bool armed{};std::mutex mutex;std::condition_variable wake;
    DWORD status{},count{};bool error{},closed{};
    static void CALLBACK Callback(HINTERNET,DWORD_PTR context,DWORD status,void* info,DWORD length){
        if(!context)return;auto& self=*reinterpret_cast<AsyncRequest*>(context);std::lock_guard lock(self.mutex);
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)self.closed=true;
        else if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)self.error=true;
        else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE||
                status==WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE||status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE){
            self.status=status;self.count=status==WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE&&info&&length==sizeof(DWORD)?*static_cast<DWORD*>(info):length;
        }
        self.wake.notify_all();
    }
    explicit AsyncRequest(HINTERNET handle):value(handle){}
    void Arm(){Check(value!=nullptr);const DWORD_PTR context=reinterpret_cast<DWORD_PTR>(this);
        Check(WinHttpSetOption(value,WINHTTP_OPTION_CONTEXT_VALUE,const_cast<DWORD_PTR*>(&context),sizeof(context))!=FALSE);
        Check(WinHttpSetStatusCallback(value,Callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)!=WINHTTP_INVALID_STATUS_CALLBACK);armed=true;
    }
    void Begin(){std::lock_guard lock(mutex);status=count=0;error=false;}
    DWORD Wait(Job& job,DWORD expected,Clock::time_point deadline){
        std::unique_lock lock(mutex);
        while(!error&&!closed&&status!=expected&&!job.cancelled&&Clock::now()<deadline)wake.wait_for(lock,std::chrono::milliseconds(25));
        Check(!job.cancelled&&!error&&!closed&&status==expected&&Clock::now()<deadline);return count;
    }
    ~AsyncRequest(){
        if(!value)return;WinHttpCloseHandle(value);
        // HANDLE_CLOSING is the final callback for this handle. Do not release
        // callback state or unload the DLL while WinHTTP can still access it.
        if(armed){std::unique_lock lock(mutex);wake.wait(lock,[&]{return closed;});}
    }
    operator HINTERNET()const{return value;}
};
void Started(BOOL result){Check(result||GetLastError()==ERROR_IO_PENDING);}
// HTTP never receives a URL or file path from another player. The caller uses
// the game server's advertised endpoint and an opaque session ticket only.
void Transfer(Job& job){
    const auto endpoint=spray::http::ParseEndpoint(std::string(job.secure?"https://":"http://")+job.host+":"+std::to_string(job.port)+"/spray/");
    Check(job.size>=22&&job.size<=spray::MaxBytes);
    const std::string prefix=job.upload?"/spray/upload/":"/spray/image/";
    Check(job.path.starts_with(prefix)&&job.path.size()==prefix.size()+32&&job.path.substr(prefix.size()).find_first_not_of("0123456789abcdef")==std::string::npos);
    Check(!job.cancelled);Handle session{WinHttpOpen(L"GoldCraftSpray/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC)};Check(session.value!=nullptr);
    Check(WinHttpSetTimeouts(session,3000,3000,5000,5000)!=FALSE);
    const std::wstring host(job.host.begin(),job.host.end()),path(job.path.begin(),job.path.end());
    Handle connection{WinHttpConnect(session,host.c_str(),job.port,0)};Check(connection.value!=nullptr);
    AsyncRequest request{WinHttpOpenRequest(connection,job.upload?L"PUT":L"GET",path.c_str(),L"HTTP/1.1",WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,job.secure?WINHTTP_FLAG_SECURE:0)};request.Arm();
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    Check(WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))!=FALSE);
    Check(WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable))!=FALSE);
    const wchar_t* headers=L"Content-Type: application/octet-stream\r\nConnection: close\r\n";
    const auto deadline=Clock::now()+std::chrono::seconds(120);
    request.Begin();Started(WinHttpSendRequest(request,headers,DWORD(-1),WINHTTP_NO_REQUEST_DATA,0,job.upload?job.size:0,reinterpret_cast<DWORD_PTR>(&request)));
    request.Wait(job,WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline);
    if(job.upload){Check(job.bytes.size()==job.size);
        for(size_t at=0;at<job.bytes.size();){Check(!job.cancelled&&Clock::now()<deadline);request.Begin();
            Started(WinHttpWriteData(request,job.bytes.data()+at,DWORD(std::min(size_t(128*1024),job.bytes.size()-at)),nullptr));
            const auto written=request.Wait(job,WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE,deadline);Check(written>0);at+=written;job.transferred=unsigned(at);}
    }
    Check(!job.cancelled);request.Begin();Started(WinHttpReceiveResponse(request,nullptr));request.Wait(job,WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline);DWORD status{},length=sizeof(status);
    Check(WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)&&status==200);
    DWORD size{};length=sizeof(size);Check(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&size,&length,WINHTTP_NO_HEADER_INDEX));
    Check(size==(job.upload?0:job.size));
    if(!job.upload){job.bytes.resize(job.size);size_t at=0;
        while(at<job.bytes.size()){Check(!job.cancelled&&Clock::now()<deadline);request.Begin();
            Started(WinHttpReadData(request,job.bytes.data()+at,DWORD(std::min(size_t(128*1024),job.bytes.size()-at)),nullptr));
            const auto got=request.Wait(job,WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline);Check(got>0);at+=got;job.transferred=unsigned(at);}
        // Fixed Content-Length was verified before reading; no unbounded EOF
        // wait is needed on a server keeping the connection open incorrectly.
        Check(spray::Checksum(job.bytes)==job.checksum);
    }
    Check(!job.cancelled);Check(spray::InspectAnimation(job.bytes).memory<=job.decodedLimit);
    job.animation=Decode(job.bytes,job.reservation);spray::Bytes{}.swap(job.bytes);
}
}
struct Worker::Impl {
    std::mutex mutex;std::condition_variable wake;bool stopping{};
    std::deque<std::shared_ptr<Job>> queue;std::vector<std::shared_ptr<Job>> active;
    std::vector<std::thread> threads;
    void Run(){SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        for(;;){std::shared_ptr<Job> job;{std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||!queue.empty();});if(stopping)return;job=queue.front();queue.pop_front();active.push_back(job);}
            // A transient full server can be retried without a game-thread wait.
            for(unsigned attempt=0;attempt<3&&!job->cancelled;++attempt){try{Transfer(*job);job->success=true;break;}catch(...){
                if(attempt<2){std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(200),[&]{return stopping||job->cancelled;});}}}
            job->done.store(true,std::memory_order_release);
            std::lock_guard lock(mutex);std::erase(active,job);
        }
    }
};
Worker::Worker():impl(std::make_unique<Impl>()){}
Worker::~Worker(){Shutdown();}
bool Worker::Submit(std::shared_ptr<Job> job){std::lock_guard lock(impl->mutex);if(impl->stopping||impl->queue.size()+impl->active.size()>=4)return false;
    if(impl->threads.empty())try{for(int i=0;i<2;++i)impl->threads.emplace_back([this]{impl->Run();});}catch(...){return false;}
    impl->queue.push_back(std::move(job));impl->wake.notify_one();return true;
}
void Worker::Cancel(){std::lock_guard lock(impl->mutex);for(auto& job:impl->active)job->cancelled=true;for(auto& job:impl->queue){job->cancelled=true;job->done=true;}impl->queue.clear();impl->wake.notify_all();}
void Worker::Shutdown(){Cancel();{std::lock_guard lock(impl->mutex);impl->stopping=true;}impl->wake.notify_all();for(auto& t:impl->threads)t.join();impl->threads.clear();}
}
