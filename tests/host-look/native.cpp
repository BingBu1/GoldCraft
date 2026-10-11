// Runs the exact CreateMove body extracted from plugin.cpp by Test-HostLook.py.
// Only the engine/window/transport adapters below are simulated; no game input.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "goldcraft/wire.hpp"
using namespace goldcraft;
using DWORD=unsigned long;
constexpr unsigned IN_USE=32;
struct usercmd_t {float viewangles[3]{},forwardmove=0,sidemove=0,upmove=0;unsigned buttons=0,impulse=0;};
bool controlling=true,focused=true,window_focused=false,ui_active=false,console=false,camera_fresh=true;
float engine_angles[3]{20,100,0},mouse_pitch=0,mouse_yaw=0;
float ui_angles[3]{20,100,0},ui_x=.5f,ui_y=.5f;
unsigned viewport_width=1000,viewport_height=1000,host_use_sources=0,input_buttons_observed=0,original_calls=0;
std::array<bool,10> menu_pressed{};
bool last_input_active=false;
unsigned last_input_buttons=0;
float last_input_forward=0,last_input_side=0;
std::uint64_t world=1,input_sequence=0;
struct InputTime {std::uint64_t sequence;std::chrono::steady_clock::time_point time;};
std::array<InputTime,256> input_times{};
struct Camera{struct Angles{float x=30,y=-90;}angles;}camera_pose;
bool HasControl(){return controlling;}bool CameraFresh(){return camera_fresh;}
int GetForegroundWindow(){return 0;}DWORD GetCurrentProcessId(){return 17;}
void GetWindowThreadProcessId(int,DWORD* process){*process=focused?17:18;}
void UpdateUi(){}void SendUi(int){}void ReleaseMinecraftKeys(){}void ReleaseHostUse(){}void UpdateHostUse(){}
namespace host_ui{struct State{bool game_menu=false;};State current;State state(){return current;}void update_mouse_capture(bool,bool){}}
struct Engine{
    bool Con_IsVisible(){return console;}
    float pfnGetCvarFloat(const char* name){return std::string(name)=="sensitivity"?1:std::string(name)=="m_rawinput"?1:.022f;}
    void GetViewAngles(float* output){std::copy(std::begin(engine_angles),std::end(engine_angles),output);}
    void SetViewAngles(float* input){std::copy(input,input+3,engine_angles);}
}gEngfuncs;
struct Exports{
    void CL_CreateMove(float,usercmd_t* cmd,int){
        ++original_calls;
        engine_angles[0]=std::clamp(engine_angles[0]+mouse_pitch,-89.0f,89.0f);
        engine_angles[1]=std::fmod(engine_angles[1]+mouse_yaw+360.0f,360.0f);
        std::copy(std::begin(engine_angles),std::end(engine_angles),cmd->viewangles);
        cmd->forwardmove=400;cmd->sidemove=-400;cmd->buttons=1|IN_USE;
    }
}gExportfuncs;
struct Link {Bytes payload;bool live=true;bool connected(){return live;}bool send(Type type,const Bytes& bytes){assert(type==Type::input);payload=bytes;return true;}}link;
#include "create_move.inc"
struct Sample{unsigned flags,buttons;float pitch,yaw,forward,side;};
Sample sample(){
    Reader r(link.payload);assert(r.u64()==world);assert(r.u64()==input_sequence);r.f32();
    Sample s{};s.flags=r.u32();s.buttons=r.u32();s.pitch=r.f32();s.yaw=r.f32();s.forward=r.f32();s.side=r.f32();r.f32();r.finish();return s;
}
void near(float a,float b){assert(std::abs(a-b)<.001f);}
int main(){
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    usercmd_t cmd;mouse_pitch=2;mouse_yaw=-3;
    CreateMove(.01f,&cmd,1);auto s=sample();assert(original_calls==1&&s.flags==3);
    near(s.pitch,2);near(s.yaw,-3);near(s.forward,400);near(s.side,-400);
    assert(cmd.forwardmove==0&&cmd.buttons==0);near(engine_angles[0],30);near(engine_angles[1],-90);
    // An Epic camera already looking vertically must not saturate host delta.
    camera_pose.angles.x=89;camera_pose.angles.y=170;
    for(int i=0;i<150;i++){CreateMove(.01f,&cmd,1);s=sample();near(s.pitch,2);near(s.yaw,-3);near(engine_angles[0],89);}
    // Temporary lease loss sends absolute INPUT, followed by an explicit delta.
    controlling=false;CreateMove(.01f,&cmd,1);s=sample();assert(s.flags==1);near(s.pitch,89);near(s.yaw,167);assert(cmd.forwardmove==400&&cmd.buttons!=0);
    controlling=true;mouse_pitch=mouse_yaw=0;CreateMove(.01f,&cmd,1);s=sample();assert(s.flags==3);near(s.pitch,0);near(s.yaw,0);
    for(int mode=0;mode<4;mode++){
        focused=mode!=0;console=mode==1;host_ui::current.game_menu=mode==2;ui_active=mode==3;
        mouse_pitch=1;mouse_yaw=2;CreateMove(.01f,&cmd,1);s=sample();near(s.pitch,0);near(s.yaw,0);
        if(!focused||console)assert(s.flags==2);
        if(ui_active){near(engine_angles[0],ui_angles[0]);near(engine_angles[1],ui_angles[1]);assert(ui_x!=.5f&&ui_y!=.5f);}
    }
    focused=true;console=ui_active=host_ui::current.game_menu=false;mouse_pitch=mouse_yaw=0;
    CreateMove(.01f,&cmd,1);s=sample();assert(s.flags==3);near(s.pitch,0);near(s.yaw,0);
    // Ordinary CS keeps both its absolute camera and native movement/buttons.
    controlling=false;engine_angles[0]=12;engine_angles[1]=35;mouse_pitch=2;mouse_yaw=-3;
    CreateMove(.01f,&cmd,1);s=sample();assert(s.flags==1);near(s.pitch,14);near(s.yaw,32);near(engine_angles[1],32);assert(cmd.forwardmove==400&&cmd.buttons!=0);
    std::cout<<"PASS exact CreateMove adapter: relative motion, 150 pitch-edge frames, movement/button routing, control loss/reacquisition, focus/console/menu, GUI cursor and native CS.\n";
}
