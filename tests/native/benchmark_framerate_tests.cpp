// Execute production benchmark policy against the exact public engine SDK types.
// The extractor supplies complete functions; this fixture only supplies engine,
// cvar and transport state. No game process, engine patch, config or input is used.
#include <metahook.h>
#include <net_api.h>
#include <demo_api.h>
#include "../../native/include/goldcraft/wire.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

cl_enginefunc_t gEngfuncs{};
namespace {
using goldcraft::protocol_version;
net_api_t network{};
demo_api_t demo{};
net_status_t network_status{};
std::map<std::string,float> cvars;
std::vector<std::pair<std::string,float>> writes;
std::vector<std::string> errors;
std::string server_protocol;
bool playing_demo=false;
double benchmark_now=1;
double Seconds(){return benchmark_now;}
std::uint64_t world=0,last_link_generation=0;
struct EndpointAdapter {
    bool ready=false;std::uint64_t current_generation=0;
    bool connected() const{return ready;}
    std::uint64_t generation() const{return current_generation;}
} link;
void Log(const std::string& text){errors.push_back(text);}
float GetCvar(const char* name){return cvars.at(name);}
void SetCvar(const char* name,float value){cvars.at(name)=value;writes.emplace_back(name,value);}
const char* ServerInfo(const char* key){return std::string(key)=="mc_protocol"?server_protocol.c_str():nullptr;}
void NetworkStatus(net_status_t* status){*status=network_status;}
int IsPlayingback(){return playing_demo?1:0;}

#include "benchmark_framerate.inc"

unsigned checks=0,cases=0;
void require(bool condition,const char* message){
    ++checks;if(!condition)throw std::runtime_error(message);
}
void baseline(float original_override=0,float original_vsync=1){
    framerate_benchmark={};
    cvars={{"fps_max",99.5f},{"fps_override",original_override},{"gl_vsync",original_vsync}};
    writes.clear();errors.clear();
    server_protocol=std::to_string(protocol_version);playing_demo=false;benchmark_now=1;
    world=101;last_link_generation=7;link={true,7};
    network_status={};network_status.connected=1;
    network_status.remote_address.type=NA_IP;
    network_status.remote_address.ip[0]=127;network_status.remote_address.ip[3]=1;
    network_status.remote_address.port=27015;
    network={};network.Status=NetworkStatus;
    demo={};demo.IsPlayingback=IsPlayingback;
    gEngfuncs={};gEngfuncs.pNetAPI=&network;gEngfuncs.pDemoAPI=&demo;
    gEngfuncs.pfnGetCvarFloat=GetCvar;gEngfuncs.Cvar_SetValue=SetCvar;
    gEngfuncs.ServerInfo_ValueForKey=ServerInfo;
    require(_putenv_s("GOLDCRAFT_TEST_COMMAND","benchmark-adapter-only")==0,"set sandbox environment");
}
void original(float expected_override=0,float expected_vsync=1){
    require(!framerate_benchmark.active,"benchmark lease must be inactive");
    require(cvars.at("fps_max")==99.5f,"original fractional fps_max must restore exactly");
    require(cvars.at("fps_override")==expected_override,"original override must restore exactly");
    require(cvars.at("gl_vsync")==expected_vsync,"original vsync must restore exactly");
}
template<class Change> void reject(const char* label,Change change,int fps=200){
    baseline();change();SetBenchmarkFramerate(fps);original();
    require(writes.empty(),"rejected benchmark must not write cvars");++cases;
    std::cout<<label<<" rejected\n";
}
template<class Change> void revoke(const char* label,Change change){
    baseline();SetBenchmarkFramerate(200);require(framerate_benchmark.active,"valid benchmark must start");
    writes.clear();change();CheckBenchmarkFramerate();original();
    require(writes.size()==3,"revocation restores exactly three original values");
    CheckBenchmarkFramerate();RestoreBenchmarkFramerate();
    require(writes.size()==3,"restoration must be idempotent");++cases;
    std::cout<<label<<" restored\n";
}
}

int main(){
    try{
        reject("ordinary remote",[]{network_status.remote_address.ip[0]=203;network_status.remote_address.ip[1]=0;network_status.remote_address.ip[2]=113;network_status.remote_address.ip[3]=5;});
        reject("other 127 address",[]{network_status.remote_address.ip[3]=2;});
        reject("listen loopback",[]{network_status.remote_address.type=NA_LOOPBACK;});
        reject("broadcast",[]{network_status.remote_address.type=NA_BROADCAST;});
        reject("demo",[]{playing_demo=true;});
        reject("wrong protocol",[]{server_protocol=std::to_string(protocol_version-1);});
        reject("missing protocol",[]{server_protocol.clear();});
        reject("no sandbox environment",[]{_putenv_s("GOLDCRAFT_TEST_COMMAND","");});
        reject("disconnected engine",[]{network_status.connected=0;});
        reject("disconnected bridge",[]{link.ready=false;});
        reject("stale bridge generation",[]{++link.current_generation;});
        reject("missing world",[]{world=0;});
        reject("missing network API",[]{gEngfuncs.pNetAPI=nullptr;});
        reject("missing status API",[]{network.Status=nullptr;});
        reject("missing remote port",[]{network_status.remote_address.port=0;});
        reject("invalid fps",[]{},144);

        for(const auto originals:std::array<std::pair<float,float>,3>{{{0,1},{1,0},{2,-1}}}){
            baseline(originals.first,originals.second);
            for(const int fps:{100,200,300,100,300}){
                SetBenchmarkFramerate(fps);
                require(framerate_benchmark.active,"valid exact loopback must enable benchmark");
                require(cvars.at("fps_max")==fps,"requested cap must apply");
                require(cvars.at("fps_override")==float(fps>100),"override follows requested cap");
                require(cvars.at("gl_vsync")==0,"benchmark vsync is disabled");
                require(framerate_benchmark.fps_max==99.5f&&framerate_benchmark.fps_override==originals.first&&framerate_benchmark.vsync==originals.second,"gear changes must retain original settings");
            }
            writes.clear();SetBenchmarkFramerate(0);original(originals.first,originals.second);
            require(writes.size()==3,"explicit end restores three cvars");
            SetBenchmarkFramerate(0);require(writes.size()==3,"explicit end is idempotent");++cases;
        }
        revoke("engine disconnect",[]{network_status.connected=0;});
        revoke("bridge disconnect",[]{link.ready=false;});
        revoke("world change",[]{++world;});
        revoke("link generation change",[]{++link.current_generation;});
        revoke("accepted generation change",[]{++link.current_generation;++last_link_generation;});
        revoke("remote port change",[]{++network_status.remote_address.port;});
        revoke("ordinary remote transition",[]{network_status.remote_address.ip[0]=192;network_status.remote_address.ip[1]=0;network_status.remote_address.ip[2]=2;});
        revoke("listen transition",[]{network_status.remote_address.type=NA_LOOPBACK;});
        revoke("protocol change",[]{server_protocol=std::to_string(protocol_version+1);});
        revoke("demo transition",[]{playing_demo=true;});
        revoke("lease deadline",[]{benchmark_now+=30;});
        revoke("late lease check",[]{benchmark_now+=120;});

        baseline();SetBenchmarkFramerate(200);benchmark_now+=29.999;CheckBenchmarkFramerate();
        require(framerate_benchmark.active,"lease must remain active before its deadline");
        SetBenchmarkFramerate(300);benchmark_now+=.001;CheckBenchmarkFramerate();
        require(framerate_benchmark.active,"new gear renews the bounded lease");
        require(framerate_benchmark.fps_max==99.5f,"lease renewal preserves the original fractional cap");
        benchmark_now+=30;CheckBenchmarkFramerate();original();++cases;

        baseline();SetBenchmarkFramerate(200);network_status.connected=0;writes.clear();
        SetBenchmarkFramerate(0);original();require(writes.size()==3,"explicit end works even after gate fails");++cases;
        baseline();SetBenchmarkFramerate(300);RestoreBenchmarkFramerate();original();++cases;
        std::cout<<"{\"passed\":true,\"cases\":"<<cases<<",\"checks\":"<<checks
                 <<",\"boundary\":\"Production policy with public SDK adapters; no live connection or quit/config ordering evidence\"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
