// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#include "transfer.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace parallel_download {
using Clock = std::chrono::steady_clock;
Job::~Job() { if (!temporary.empty()) { std::error_code ec; std::filesystem::remove(temporary, ec); } }
bool ValidPath(const std::string& path) {
    if (path.empty() || path.size() > 240 || path.front() == '/' || path.back() == '/') return false;
    for (unsigned char c : path) if (c < 32 || c >= 127 || std::string_view("\\:%?#\"<>|*").find(c) != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        auto stem = part.substr(0, part.find('.'));
        for (char& c : stem) if (c >= 'a' && c <= 'z') c -= 'a'-'A';
        if (stem == "CON" || stem == "NUL" || stem == "AUX" || stem == "PRN" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
namespace {
struct Failure { DWORD code; };
void Require(bool ok, DWORD error = 0) { if (!ok) throw Failure{error ? error : GetLastError()}; }
struct Handle { HINTERNET h{}; ~Handle(){ if(h) WinHttpCloseHandle(h); } operator HINTERNET()const{return h;} };
struct File { HANDLE h{INVALID_HANDLE_VALUE}; ~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
struct Request {
    HINTERNET h{};
    std::mutex mutex;
    std::condition_variable wake;
    DWORD event{}, count{}, error{};
    bool armed{}, closed{};
    explicit Request(HINTERNET value):h(value){}
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD event, void* data, DWORD length) {
        if (!context) return;
        auto& r = *reinterpret_cast<Request*>(context);
        std::lock_guard lock(r.mutex);
        if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) r.closed = true;
        else if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
            r.error = data ? static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError : ERROR_GEN_FAILURE;
        else if (event == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || event == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE || event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            r.event = event; r.count = length;
        }
        r.wake.notify_all();
    }
    void Arm() {
        Require(h != nullptr);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        Require(WinHttpSetOption(h,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)) != FALSE);
        Require(WinHttpSetStatusCallback(h,Callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)!=WINHTTP_INVALID_STATUS_CALLBACK);
        armed = true;
    }
    void Begin(){ std::lock_guard lock(mutex); event=count=error=0; }
    DWORD Wait(Job& job,DWORD expected,Clock::time_point deadline,unsigned timeout) {
        deadline = std::min(deadline, Clock::now()+std::chrono::milliseconds(timeout));
        std::unique_lock lock(mutex);
        while (!closed && !error && event != expected && !job.cancelled && Clock::now()<deadline)
            wake.wait_for(lock,std::chrono::milliseconds(20));
        Require(!job.cancelled, ERROR_CANCELLED);
        Require(!error, error);
        Require(!closed && event==expected && Clock::now()<deadline, ERROR_TIMEOUT);
        return count;
    }
    ~Request(){
        if (!h) return;
        WinHttpCloseHandle(h);
        // WinHTTP's HANDLE_CLOSING notification is the ownership fence: no
        // callback may touch this stack object after it is destroyed.
        if (armed) { std::unique_lock lock(mutex); wake.wait(lock,[&]{return closed;}); }
    }
};
void Started(BOOL ok){Require(ok || GetLastError()==ERROR_IO_PENDING);}
void Transfer(Job& job, const Limits& limits, Clock::time_point deadline) {
    Require(!job.cancelled, ERROR_CANCELLED);
    Require(job.url.size()<2048,ERROR_INVALID_NAME);
    for(unsigned char c:job.url)Require(c>=32&&c<127,ERROR_INVALID_NAME);
    const std::wstring url(job.url.begin(),job.url.end());
    URL_COMPONENTS p{}; p.dwStructSize=sizeof(p);
    p.dwHostNameLength=p.dwUrlPathLength=p.dwExtraInfoLength=p.dwUserNameLength=p.dwPasswordLength=DWORD(-1);
    Require(WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&p) && p.dwHostNameLength && !p.dwUserNameLength && !p.dwPasswordLength && !p.dwExtraInfoLength &&
        (p.nScheme==INTERNET_SCHEME_HTTP || p.nScheme==INTERNET_SCHEME_HTTPS), ERROR_INVALID_NAME);
    std::wstring host(p.lpszHostName,p.dwHostNameLength),path(p.lpszUrlPath,p.dwUrlPathLength);
    Handle session{WinHttpOpen(L"Half-Life GoldCraftParallelDownload/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC)};
    Require(session.h!=nullptr);
    Require(WinHttpSetTimeouts(session,limits.timeout_ms,limits.timeout_ms,limits.timeout_ms,limits.timeout_ms)!=FALSE);
    Handle connection{WinHttpConnect(session,host.c_str(),p.nPort,0)}; Require(connection.h!=nullptr);
    Request request{WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,p.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};
    request.Arm();
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER, disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    Require(WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)) && WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)));
    request.Begin();
    Started(WinHttpSendRequest(request.h,L"Cache-Control: no-cache\r\n",DWORD(-1),nullptr,0,0,reinterpret_cast<DWORD_PTR>(&request)));
    request.Wait(job,WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline,limits.timeout_ms);
    request.Begin(); Started(WinHttpReceiveResponse(request.h,nullptr));
    request.Wait(job,WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline,limits.timeout_ms);
    DWORD status{}, n=sizeof(status);
    Require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&n,WINHTTP_NO_HEADER_INDEX)!=FALSE);
    job.status=status; Require(status==200,ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
    wchar_t length[32]{};n=sizeof(length);
    const bool hasLength=WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,length,&n,WINHTTP_NO_HEADER_INDEX)!=FALSE;
    std::uint64_t size{};
    if(hasLength){
        Require(length[0]!=0,ERROR_INVALID_DATA);
        for(const auto c:length){if(!c)break;Require(c>=L'0'&&c<=L'9'&&size<=limits.max_bytes/10,ERROR_INVALID_DATA);size=size*10+(c-L'0');}
        Require(size>0&&size<=limits.max_bytes,ERROR_FILE_TOO_LARGE);
    }
    job.length=size;job.received=0;
    File file{CreateFileW(job.temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    Require(file.h!=INVALID_HANDLE_VALUE);
    std::array<char,64*1024> buffer{};
    std::uint64_t received=0;
    for(;;){
        Require(!job.cancelled,ERROR_CANCELLED);
        request.Begin(); Started(WinHttpReadData(request.h,buffer.data(),DWORD(buffer.size()),nullptr));
        const auto read=request.Wait(job,WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline,limits.timeout_ms);
        if(!read)break;
        Require(received+read<=limits.max_bytes&&(!hasLength||received+read<=size),ERROR_FILE_TOO_LARGE);
        DWORD written{};Require(WriteFile(file.h,buffer.data(),read,&written,nullptr)&&written==read);
        received+=read;job.received=received;
    }
    Require(received>0&&(!hasLength||received==size),ERROR_HANDLE_EOF);
    Require(FlushFileBuffers(file.h)!=FALSE);
    Require(!job.cancelled,ERROR_CANCELLED);
}
}
struct Pool::Impl {
    Limits limits;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Job>> queue;
    std::vector<std::shared_ptr<Job>> active;
    std::vector<std::thread> threads;
    bool stopping{};
    explicit Impl(Limits value):limits(value) {
        limits.workers=std::clamp(limits.workers,1u,16u);
        try{ for(unsigned i=0;i<limits.workers;++i)threads.emplace_back([this]{Run();}); }
        catch(...){ {std::lock_guard lock(mutex);stopping=true;}wake.notify_all();for(auto& t:threads)t.join();throw; }
    }
    void Run(){
        for(;;){
            std::shared_ptr<Job> job;
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||!queue.empty();});if(stopping)return;
                job=queue.front();queue.pop_front();active.push_back(job);}
            job->active=true;
            const auto deadline=Clock::now()+std::chrono::milliseconds(limits.total_ms);
            for(unsigned attempt=0;attempt<=limits.retries&&!job->cancelled&&Clock::now()<deadline;++attempt){
                job->attempts=attempt+1;job->status=job->error=0;
                try{Transfer(*job,limits,deadline);job->success=true;break;}
                catch(const Failure& e){job->error=e.code;}
                catch(...){job->error=ERROR_NOT_ENOUGH_MEMORY;}
                if(job->status>=400&&job->status<500)break;
            }
            job->active=false;
            job->done.store(true,std::memory_order_release);
            {std::lock_guard lock(mutex);std::erase(active,job);}
        }
    }
    void CancelLocked(){
        for(auto& j:queue){j->cancelled=true;j->done.store(true,std::memory_order_release);}queue.clear();
        for(auto& j:active)j->cancelled=true;
    }
    ~Impl(){ {std::lock_guard lock(mutex);stopping=true;CancelLocked();}wake.notify_all();for(auto& t:threads)t.join(); }
};
Pool::Pool(Limits limits):impl(std::make_unique<Impl>(limits)){}
Pool::~Pool()=default;
bool Pool::Submit(const std::shared_ptr<Job>& job){std::lock_guard lock(impl->mutex);if(impl->stopping||impl->queue.size()>=32768)return false;impl->queue.push_back(job);impl->wake.notify_one();return true;}
void Pool::Cancel(){std::lock_guard lock(impl->mutex);impl->CancelLocked();}
}
