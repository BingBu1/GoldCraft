#include "../../native/client/work_history.hpp"
#include <cassert>
#include <sstream>
#include <iostream>

int main(){
    goldcraft::timing::History<2> history;
    constexpr std::array<const char*,2> names{"native","plugin"};
    history.record(0,275,1,27);
    for(unsigned i=0;i<200000;++i)history.record(1,.05,2+i);
    assert(history.counters[0].max_ms==275&&history.counters[0].slow==1&&history.slow_calls==1);
    std::ostringstream quiet;history.write(quiet,names);
    assert(quiet.str().find("\"stage\":\"native\",\"elapsedMs\":275")!=std::string::npos);
    assert(quiet.str().find("\"key\":27")!=std::string::npos);
    for(unsigned i=0;i<80;++i)history.record(1,50+i,300000+i,int(i));
    assert(history.slow_calls==81&&history.counters[1].slow==80);
    std::ostringstream wrapped;history.write(wrapped,names);
    assert(wrapped.str().find("\"overwritten\":17")!=std::string::npos);
    assert(wrapped.str().find("\"key\":15}")==std::string::npos);
    assert(wrapped.str().find("\"key\":16}")!=std::string::npos);
    assert(history.counters[0].max_ms==275);
    std::cout<<quiet.str()<<'\n'<<wrapped.str()<<'\n';
}
