#pragma once
#include "steam_async_state.hpp"
#include "steam_async_wire.hpp"
#include <Windows.h>
#include <string>

namespace goldcraft::steam_async {
struct HelperStatus {
    std::atomic<int> initialization{-1},logged_on{-1};
    std::atomic<unsigned> app_id{},failures{},samples{};
    std::atomic<std::uint64_t> sample_tick_ms{};
};
// Synchronous from the worker's point of view; overlapped/cancellable at the
// OS boundary. The main thread only signals cancel and never joins Steam IPC.
class ProcessBackend final:public Backend {
public:
    ProcessBackend(std::wstring executable,std::wstring steam,HANDLE cancel,HelperStatus* status=nullptr)
        :executable_(std::move(executable)),steam_(std::move(steam)),cancel_(cancel),status_(status){}
    ~ProcessBackend()override;
    bool initialize()override;
    void poll()override;
    bool healthy()const override{return !dead_;}
    void avatar(Avatar&)override;
    void start_voice()override;
    void stop_voice()override;
    VoiceResult capture(VoicePacket&,bool,unsigned)override;
    int initialization_result()const{return initialization_result_;}
private:
    std::wstring executable_,steam_;
    HANDLE cancel_{},pipe_=INVALID_HANDLE_VALUE,job_{},process_{},event_{};
    bool dead_{};
    int initialization_result_=-1;
    HelperStatus* status_{};
    Clock::time_point next_status_{};
    bool initialize_process();
    bool transfer(void*,unsigned,bool);
    bool request(const wire::Request&,wire::Reply&);
};
}
