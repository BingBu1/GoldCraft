#pragma once
#include "steam_async_state.hpp"
#include "steam_async_wire.hpp"
#include <Windows.h>
#include <string>

namespace goldcraft::steam_async {
// Synchronous from the worker's point of view; overlapped/cancellable at the
// OS boundary. The main thread only signals cancel and never joins Steam IPC.
class ProcessBackend final:public Backend {
public:
    ProcessBackend(std::wstring executable,std::wstring steam,HANDLE cancel)
        :executable_(std::move(executable)),steam_(std::move(steam)),cancel_(cancel){}
    ~ProcessBackend()override;
    bool initialize()override;
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
    bool transfer(void*,unsigned,bool);
    bool request(const wire::Request&,wire::Reply&);
};
}
