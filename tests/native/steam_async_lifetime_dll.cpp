#include "steam_async.cpp"
using namespace goldcraft::steam_async;
extern "C" int test_start(const wchar_t* helper){
    state=std::make_shared<WorkerState>();state->executable=helper;state->steam=L"avatar-block";
    state->state.avatar(123,1);game_thread=GetCurrentThreadId();
    return start_worker(false);
}
extern "C" int test_ready(){return state&&state->state.counters.avatar_ready.load();}
extern "C" void test_stop(){shutdown();}
