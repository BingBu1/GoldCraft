// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production diagnostics header; only clock and logger are fake.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

double Sys_FloatTime();
void Log_Printf(const char*,...);
#include "../../native/engine/model_load_profile.h"

namespace
{
double now{};
unsigned clockCalls{},checks{};
std::vector<std::string> lines;
void Check(bool value,const char* message)
{++checks;if(!value)throw std::runtime_error(message);}
void Reset(const char* value)
{
    if(_putenv_s("GOLDCRAFT_MAP_LOAD_TIMING",value)!=0)throw std::runtime_error("environment setup failed");
    now=0;clockCalls=0;lines.clear();
}
bool Has(const char* text)
{for(const auto& line:lines)if(line.find(text)!=std::string::npos)return true;return false;}
}
double Sys_FloatTime(){++clockCalls;return now;}
void Log_Printf(const char* format,...)
{
    char text[512];va_list args;va_start(args,format);
    const auto written=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(written<0 || written>=int(sizeof(text)))throw std::runtime_error("log overflow");
    lines.emplace_back(text);
}
int main()
{
    using namespace goldcraft::model_profile;
    try
    {
        for(const char* value:{"","0","true","01","1 "})
        {
            Reset(value);
            {Phase phase("off");Scope scope(Stage::Decode);now=17;}
            Check(clockCalls==0,"disabled diagnostics read the clock");
            Check(lines.empty(),"disabled diagnostics emitted a log");
            Check(!enabled,"disabled phase left recording active");
        }
        Reset("1");
        {
            Phase phase("aggregate");
            {Scope scope(Stage::FileRead);now+=.5;}
            {Scope scope(Stage::FileRead);now+=1;}
            {Scope scope(Stage::Decode);now+=2;{Scope child(Stage::DecodeSafety);now+=4;}now+=3;}
        }
        Check(clockCalls==8,"normal scopes have wrong clock call count");
        Check(lines.size()==3,"normal phase has wrong nonempty stage count");
        Check(Has("stage=file_read calls=2 seconds=1.500000"),"same-stage aggregation differs");
        Check(Has("stage=decode calls=1 seconds=9.000000"),"parent duration must include nested stage");
        Check(Has("stage=decode_safety calls=1 seconds=4.000000"),"nested duration differs");
        Check(!enabled,"normal phase left recording active");

        Reset("1");
        try{Phase phase("unwind");Scope scope(Stage::Prepare);now+=2;throw std::runtime_error("expected");}
        catch(const std::runtime_error&){}
        Check(lines.empty(),"unwound phase reported as normally completed");
        Check(!enabled,"unwound phase left recording active");
        {Phase phase("after_unwind");Scope scope(Stage::Commit);now+=.25;}
        Check(lines.size()==1 && Has("phase=after_unwind stage=commit calls=1 seconds=0.250000"),"unwound counters leaked into next phase");

        // A locally caught failure remains an attempted call in a completed phase.
        Reset("1");
        {
            Phase phase("attempts");
            try{Scope scope(Stage::Prepare);now+=.5;throw std::runtime_error("expected");}
            catch(const std::runtime_error&){}
            {Scope scope(Stage::Prepare);now+=.75;}
        }
        Check(lines.size()==1 && Has("stage=prepare calls=2 seconds=1.250000"),"attempt counting semantics changed");

        Reset("1");
        {
            Phase outer("outer");Scope oldScope(Stage::Decode);now+=1;
            {Phase inner("inner");Scope scope(Stage::Copy);now+=2;}
            const auto clocks=clockCalls;
            {Scope discarded(Stage::Commit);now+=3;}
            Check(clockCalls==clocks,"cancelled outer phase resumed recording");
        }
        Check(lines.size()==1 && Has("phase=inner stage=copy calls=1 seconds=2.000000"),"nested phase mixed old counters");
        Check(clockCalls==3,"stale outer scope read the clock");

        // Keep stale objects alive past a replacement phase to simulate skipped
        // destructors. No actual longjmp over C++ objects is used by this fixture.
        Reset("1");
        alignas(Phase) unsigned char phaseStorage[sizeof(Phase)];
        alignas(Scope) unsigned char scopeStorage[sizeof(Scope)];
        auto* stalePhase=new(phaseStorage) Phase("abandoned");
        auto* staleScope=new(scopeStorage) Scope(Stage::Decode);now+=10;
        {Scope prior(Stage::Prepare);now+=3;}
        const auto staleGeneration=generation;
        {Phase current("replacement");Scope scope(Stage::Storage);now+=.125;}
        Check(generation!=staleGeneration,"replacement did not invalidate abandoned generation");
        const auto completedClocks=clockCalls;
        staleScope->~Scope();stalePhase->~Phase();
        Check(clockCalls==completedClocks,"stale destructor read the clock");
        Check(lines.size()==1 && Has("phase=replacement stage=storage calls=1 seconds=0.125000"),"abandoned phase contaminated replacement");
        Check(!enabled,"replacement left recording active");
        _putenv_s("GOLDCRAFT_MAP_LOAD_TIMING","");
        std::printf("model-load profile: %u checks passed; actual header; fake clock/logger; no game execution\n",checks);
        return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"profile fixture failed: %s\n",error.what());return 1;}
}
