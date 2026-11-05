// SPDX-License-Identifier: GPL-3.0-or-later
#define SKULL3_CLIENT_EVENTS_NO_MAIN
#include "client_events.cpp"
int main(){using namespace csol::client;fixture::Setup();int kind,d0,d1,choice,calls{};TEMPENTITY temp{};pmtrace_t trace{};
 engine.pfnRandomLong=+[](int,int){return 0;};
 while(std::cin>>kind>>temp.entity.curstate.frame>>temp.entity.baseline.origin[0]>>temp.entity.baseline.origin[1]>>temp.entity.baseline.origin[2]
 >>trace.plane.normal[0]>>trace.plane.normal[1]>>trace.plane.normal[2]>>smokeWind[0]>>smokeWind[1]>>d0>>d1>>choice){
  fixture::choice=choice;fixture::calls=0;smokeWindDirection[0]=d0!=0;smokeWindDirection[1]=d1!=0;
  engine.pfnRandomLong=[](int,int){++fixture::calls;return fixture::choice;};
  if(kind==0)UpdateSmoke(&temp,.01f,100);else if(kind==1)UpdateWallSmoke(&temp,.01f,100);else HitSmoke(&temp,&trace);
  std::cout<<std::setprecision(9)<<"{\"velocity\":";fixture::Three(temp.entity.baseline.origin);
  std::cout<<",\"wind\":["<<smokeWind[0]<<','<<smokeWind[1]<<"],\"direction\":["<<int(smokeWindDirection[0])<<','<<int(smokeWindDirection[1])<<"],\"rngCalls\":"<<fixture::calls<<"}\n";
 }
}
