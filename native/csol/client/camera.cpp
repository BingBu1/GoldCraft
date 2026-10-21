// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera.hpp"
#include <event_api.h>
#include <pm_defs.h>
#include <algorithm>
#include <cmath>
#include <ostream>
#include <filesystem>
#include <fstream>

namespace csol::client
{
namespace
{
cvar_t* command{};
cvar_t* distance{};
cvar_t* height{};
bool requested{}, active{};
std::filesystem::path testCommandPath;
unsigned long long testCommand{};
double nextTestPoll{};
float cameraAngles[3]{}, eye[3]{}, origin[3]{}, actualDistance{};
float collisionFraction=1;

float Setting(const cvar_t* variable, float fallback, float minimum, float maximum)
{
    return variable && std::isfinite(variable->value)
        ? std::clamp(variable->value, minimum, maximum) : fallback;
}
}

void InitializeCamera()
{
    command=engine.pfnGetCvarPointer("cam_command");
    if(!command)command=engine.pfnRegisterVariable("cam_command","0",0);
    distance=engine.pfnRegisterVariable("csol_cam_distance","128",FCVAR_ARCHIVE);
    height=engine.pfnRegisterVariable("csol_cam_height","12",FCVAR_ARCHIVE);
    wchar_t path[32768]{};
    const auto length=GetEnvironmentVariableW(L"CSOL_CAMERA_TEST_COMMAND",path,DWORD(std::size(path)));
    if(length>0 && length<std::size(path))testCommandPath=path;
}

void ResetCamera()
{
    requested=active=false;
    actualDistance=0;
    collisionFraction=1;
    nextTestPoll=0;
}

void CameraThink()
{
    // Explicit local fixture input, like CSOL_DIAGNOSTIC_PATH. Only camera
    // requests are accepted; normal launches do no file I/O here.
    if(!testCommandPath.empty() && engine.GetClientTime()>=nextTestPoll)
    {
        nextTestPoll=engine.GetClientTime()+.1;
        std::ifstream input(testCommandPath);
        unsigned long long serial{};int mode{};
        if(input>>serial>>mode && serial>testCommand && mode>=1 && mode<=3)
        {
            testCommand=serial;
            auto text=std::string("cam_command ")+std::to_string(mode)+"\n";
            engine.pfnClientCmd(text.data());
        }
    }
    if(command && command->value==3)
    {
        requested=true;
        // End any native chase camera before applying our own local view.
        engine.Cvar_SetValue("cam_command",2);
    }
    else if(command && (command->value==1 || command->value==2))
    {
        ResetCamera();
    }
    if(downstream.CAM_Think)downstream.CAM_Think();
    // cam_command is a one-shot request, not an archived camera mode.
    if(requested && command && command->value==2)engine.Cvar_SetValue("cam_command",0);
}

int CameraIsThirdPerson()
{
    return active ? 1 : downstream.CL_IsThirdPerson ? downstream.CL_IsThirdPerson() : 0;
}

void CameraOffsets(float* offsets)
{
    if(!active)
    {
        if(downstream.CL_CameraOffset)downstream.CL_CameraOffset(offsets);
        return;
    }
    offsets[0]=cameraAngles[0];offsets[1]=cameraAngles[1];offsets[2]=actualDistance;
}

void CameraRefdef(ref_params_t* params)
{
    const auto* player=engine.GetLocalPlayer();
    active=requested && player && player->model && params->health>0
        && !params->intermission && !params->spectator && !player->curstate.iuser1
        && !params->onlyClientDraw && params->viewentity==player->index;
    actualDistance=0;
    collisionFraction=1;
    std::copy_n(params->vieworg,3,eye);
    std::copy_n(params->vieworg,3,origin);
    if(!active)return;

    std::copy_n(params->viewangles,3,cameraAngles);
    float forward[3]{}, right[3]{}, up[3]{}, target[3]{};
    engine.pfnAngleVectors(cameraAngles,forward,right,up);
    const float range=Setting(distance,128,16,256);
    const float lift=Setting(height,12,-32,64);
    for(int i=0;i<3;++i)target[i]=eye[i]-forward[i]*range;
    target[2]+=lift;

    // Use the current predicted solid list, including doors. Ignore studio
    // models so the local player's body cannot push its own camera forward.
    auto* events=engine.pEventAPI;
    events->EV_SetUpPlayerPrediction(0,1);
    events->EV_PushPMStates();
    events->EV_SetSolidPlayers(player->index-1);
    events->EV_SetTraceHull(2);
    pmtrace_t trace{};
    events->EV_PlayerTrace(eye,target,PM_STUDIO_IGNORE,-1,&trace);
    events->EV_PopPMStates();
    collisionFraction=trace.fraction;
    float squaredDistance=0;
    for(int i=0;i<3;++i)
    {
        origin[i]=trace.startsolid || trace.allsolid ? eye[i] : trace.endpos[i];
        if(!trace.startsolid && !trace.allsolid && trace.fraction<1)
            origin[i]+=trace.plane.normal[i]*4;
        params->vieworg[i]=origin[i];
        const float delta=origin[i]-eye[i];
        squaredDistance+=delta*delta;
    }
    actualDistance=std::sqrt(squaredDistance);
}

void CameraDiagnostics(std::ostream& out)
{
    out<<",\"camera\":{\"requested\":"<<requested<<",\"active\":"<<active
        <<",\"command\":"<<(command?command->value:-1)
        <<",\"testCommand\":"<<testCommand
        <<",\"distance\":"<<actualDistance<<",\"traceFraction\":"<<collisionFraction
        <<",\"eye\":["<<eye[0]<<','<<eye[1]<<','<<eye[2]
        <<"],\"origin\":["<<origin[0]<<','<<origin[1]<<','<<origin[2]<<"]}";
}
}
