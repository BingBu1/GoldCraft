#pragma once
#include <cstdlib>
// AMXX 1.10 amx.cpp adds an optional amx_ExecPerf logging wrapper. This
// standalone test calls the real amx_Exec interpreter; engine logging is absent.
// These fields are test adapters, not the engine cvar_t ABI.
struct TestCvar { float value = 0; };
inline TestCvar testDebug, testPerf;
inline TestCvar* amxmodx_debug = &testDebug;
inline TestCvar* amxmodx_perflog = &testPerf;
inline void AMXXLOG_Log(const char*, ...) { std::abort(); }
