#include "steam_async_transport.hpp"
#include "steam_worker_environment.hpp"
#include <array>
#include <vector>

namespace goldcraft::steam_async {
ProcessBackend::~ProcessBackend(){
    if(process_&&pipe_!=INVALID_HANDLE_VALUE&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT){
        // Best-effort microphone release on the worker, with a bounded grace
        // period. A wedged Steam call cannot delay the game's shutdown.
        wire::Request quit;quit.op=wire::Op::quit;
        OVERLAPPED overlapped{};overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        DWORD bytes{};
        if(overlapped.hEvent){
            if(!WriteFile(pipe_,&quit,sizeof(quit),&bytes,&overlapped)&&GetLastError()==ERROR_IO_PENDING){
                if(WaitForSingleObject(overlapped.hEvent,100)!=WAIT_OBJECT_0)CancelIoEx(pipe_,&overlapped);
                GetOverlappedResult(pipe_,&overlapped,&bytes,TRUE);
            }
            CloseHandle(overlapped.hEvent);
        }
        WaitForSingleObject(process_,250);
    }
    // The job contains only this helper, never Steam.exe or the game.
    if(job_)CloseHandle(job_);
    if(pipe_!=INVALID_HANDLE_VALUE)CloseHandle(pipe_);
    if(process_)CloseHandle(process_);
    if(event_)CloseHandle(event_);
}
bool ProcessBackend::transfer(void* data,unsigned bytes,bool write){
    if(dead_)return false;
    auto* p=static_cast<unsigned char*>(data);
    while(bytes){
        if(WaitForSingleObject(cancel_,0)==WAIT_OBJECT_0){dead_=true;return false;}
        OVERLAPPED overlapped{};overlapped.hEvent=event_;ResetEvent(event_);DWORD n{};
        const bool ok=write?WriteFile(pipe_,p,bytes,&n,&overlapped)!=0:ReadFile(pipe_,p,bytes,&n,&overlapped)!=0;
        if(!ok){
            if(GetLastError()!=ERROR_IO_PENDING){dead_=true;return false;}
            HANDLE waits[]{cancel_,event_};
            if(WaitForMultipleObjects(2,waits,FALSE,10000)!=WAIT_OBJECT_0+1){
                CancelIoEx(pipe_,&overlapped);GetOverlappedResult(pipe_,&overlapped,&n,TRUE);dead_=true;return false;
            }
            if(!GetOverlappedResult(pipe_,&overlapped,&n,FALSE)){dead_=true;return false;}
        }
        if(!n){dead_=true;return false;}p+=n;bytes-=n;
    }
    return true;
}
bool ProcessBackend::initialize(){
    const bool ok=initialize_process();
    if(!ok&&initialization_result_==-1)initialization_result_=1000;
    if(status_)status_->initialization=initialization_result_;
    return ok;
}
bool ProcessBackend::initialize_process(){
    event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event_)return false;
    const auto name=L"\\\\.\\pipe\\GoldCraftSteam-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetCurrentThreadId())+L"-"+std::to_wstring(GetTickCount64());
    pipe_=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
    if(pipe_==INVALID_HANDLE_VALUE)return false;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    const auto child_pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,&security,OPEN_EXISTING,0,nullptr);
    if(child_pipe==INVALID_HANDLE_VALUE)return false;
    // Only the stream handle is inherited; never the game/Steam handles.
    SIZE_T size{};InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> storage(size);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)){CloseHandle(child_pipe);return false;}
    HANDLE inherited[]{child_pipe};
    bool ok=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=0;
    job_=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ok=ok&&job_&&SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    auto command=L"\""+executable_+L"\" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(child_pipe))+L" \""+steam_+L"\"";
    PROCESS_INFORMATION process{};
    const auto directory=executable_.substr(0,executable_.find_last_of(L"/\\"));
    auto* inherited_environment=GetEnvironmentStringsW();
    ok=ok&&inherited_environment;
    const auto environment=worker_environment(inherited_environment);
    if(inherited_environment)FreeEnvironmentStringsW(inherited_environment);
    if(ok)ok=CreateProcessW(executable_.c_str(),command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,
        const_cast<wchar_t*>(environment.data()),directory.c_str(),&startup.StartupInfo,&process)!=0;
    DeleteProcThreadAttributeList(startup.lpAttributeList);CloseHandle(child_pipe);
    if(!ok)return false;
    process_=process.hProcess;
    if(!AssignProcessToJobObject(job_,process_)){TerminateProcess(process_,5);CloseHandle(process.hThread);return false;}
    ResumeThread(process.hThread);CloseHandle(process.hThread);
    wire::Reply hello;
    if(!transfer(&hello,sizeof(hello),false)||hello.signature!=wire::magic||hello.protocol!=wire::version)return false;
    initialization_result_=hello.result;
    if(status_){status_->app_id=hello.app_id;status_->logged_on=hello.logged_on;status_->sample_tick_ms=GetTickCount64();++status_->samples;}
    next_status_=Clock::now()+std::chrono::seconds(1);
    return !hello.result;
}
void ProcessBackend::poll(){
    if(dead_||Clock::now()<next_status_)return;
    next_status_=Clock::now()+std::chrono::seconds(1);
    wire::Request command;command.op=wire::Op::status;wire::Reply reply;
    if(request(command,reply)&&!reply.result){
        if(status_){status_->app_id=reply.app_id;status_->logged_on=reply.logged_on;status_->sample_tick_ms=GetTickCount64();++status_->samples;}
    }else {dead_=true;if(status_){status_->logged_on=-1;++status_->failures;}}
}
bool ProcessBackend::request(const wire::Request& request,wire::Reply& reply){
    return transfer(const_cast<wire::Request*>(&request),sizeof(request),true)&&transfer(&reply,sizeof(reply),false)&&
        reply.signature==wire::magic&&reply.protocol==wire::version;
}
void ProcessBackend::avatar(Avatar& result){
    wire::Request command;command.op=wire::Op::avatar;command.id=result.id;command.size=result.size;wire::Reply reply;
    result.handle=-1;
    if(!request(command,reply)||reply.result||reply.width>184||reply.height>184)return;
    if(reply.width&&reply.height&&!transfer(result.rgba.data(),reply.width*reply.height*4,false))return;
    result.width=reply.width;result.height=reply.height;result.handle=reply.handle;result.relationship=reply.relationship;
}
void ProcessBackend::start_voice(){wire::Request command;command.op=wire::Op::start;wire::Reply reply;request(command,reply);}
void ProcessBackend::stop_voice(){wire::Request command;command.op=wire::Op::stop;wire::Reply reply;request(command,reply);}
VoiceResult ProcessBackend::capture(VoicePacket& packet,bool pcm,unsigned rate){
    wire::Request command;command.op=wire::Op::capture;command.pcm=pcm;command.rate=rate;wire::Reply reply;
    if(!request(command,reply)||reply.compressed>voice_bytes||reply.pcm>voice_bytes)return VoiceResult::not_initialized;
    if((reply.compressed&&!transfer(packet.data.data(),reply.compressed,false))||
       (reply.pcm&&!transfer(packet.raw.data(),reply.pcm,false)))return VoiceResult::not_initialized;
    packet.compressed=reply.compressed;packet.pcm=reply.pcm;return static_cast<VoiceResult>(reply.result);
}
}
