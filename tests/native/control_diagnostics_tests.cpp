#include "goldcraft/control_diagnostics.hpp"
#include <cassert>
#include <cstdlib>
#include <new>
#include <sstream>
#include <string>
#include <iostream>
static std::size_t allocations=0;
void* operator new(std::size_t bytes){++allocations;if(auto* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
int main(){
 using namespace goldcraft;
 for(unsigned mask=0;mask<32;++mask){
  const bool f=(mask&1)==0,l=(mask&2)==0,p=(mask&4)==0,c=(mask&8)==0,t=(mask&16)==0;
  assert(control_blocked(f,l,p,c,t)==mask);
  assert((control_blocked(f,l,p,c,t)==0)==(f&&l&&p&&c&&t));
 }
 ControlTrace trace;ControlSample state;state.world=9;state.connection=3;state.life=state.host_life=2;
 const auto before=allocations;trace.observe(state);
 for(int i=0;i<4096;++i){state.seconds=i*.01;state.pose_ms=i*.0001;trace.observe(state);}
 state.blocked=control_pose_stale;trace.observe(state);
 for(int i=0;i<4096;++i)trace.hud_fallback(state,1280);
 state.blocked=0;trace.observe(state);
 assert(allocations==before);
 std::ostringstream out;trace.write(out);const auto text=out.str();
 assert(text.find("\"events\":4")!=std::string::npos);
 assert(text.find("\"losses\":1")!=std::string::npos);
 assert(text.find("\"hudFallbacks\":4096")!=std::string::npos);
 assert(text.find("\"blocked\":16")!=std::string::npos);
 assert(text.find("\"error\":1280")!=std::string::npos);
 // A life or connection reset records evidence without growing a history.
 for(int i=0;i<96;++i){state.life++;state.connection++;trace.observe(state);}
 std::ostringstream bounded;trace.write(bounded);const auto history=bounded.str();
 assert(history.find("\"events\":100")!=std::string::npos);
 assert(history.find("\"overwritten\":68")!=std::string::npos);
 std::size_t count=0,pos=0;while((pos=history.find("\"sequence\":",pos))!=std::string::npos){++count;++pos;}
 assert(count==32&&history.find("\"sequence\":100")!=std::string::npos);
 ServerControlTrace server;ServerControlEvent event;event.world=9;event.slot=1;event.life=3;
 event.cause=ControlCause::form_amxx;event.previous_form=1;event.requested_form=0;event.result=1;
 const auto serverBefore=allocations;server.record(event);
 event.cause=ControlCause::form_change;event.active=true;server.record(event);
 for(int i=0;i<4096;++i){event.life++;event.cause=ControlCause::pose_stale;server.record(event);}
 assert(allocations==serverBefore);
 std::ostringstream serverOut;server.write(serverOut);auto serverText=serverOut.str();
 assert(serverText.find("\"events\":4098")!=std::string::npos);
 count=0;pos=0;while((pos=serverText.find("\"sequence\":",pos))!=std::string::npos){++count;++pos;}
 assert(count==32&&serverText.find("\"sequence\":4098")!=std::string::npos);
 std::cout<<"control diagnostics:32 predicate cases,4096 stable observations,4096 bounded fallbacks,100 transitions; no observation allocations\n";
}
