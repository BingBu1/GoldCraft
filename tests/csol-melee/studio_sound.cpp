// SPDX-License-Identifier: GPL-3.0-or-later
// Executes the production StudioEvent entry prefix unchanged with real SDK
// event/model/entity layouts. Later rendering handlers are an observed boundary.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/studio_sound.hpp"
#include <cstring>
#include <cstdio>
#include <string>

namespace {
int continued{},checks{},failures{};
void Check(bool value,const char* label)
{++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
void StudioEntry(const mstudioevent_t* event,const cl_entity_t* entity)
{
    using csol::SkipStudioSound;
#include "studio_sound_entry.inc"
    ++continued;
}
void Case(const char* model,int event,const char* sample,bool expected)
{
    model_t mesh{};cl_entity_t entity{};mstudioevent_t notice{};
    std::strncpy(mesh.name,model,sizeof(mesh.name)-1);
    notice.event=event;std::strncpy(notice.options,sample,sizeof(notice.options)-1);
    entity.model=&mesh;continued=0;StudioEntry(&notice,&entity);
    Check((continued==0)==expected,"actual StudioEvent entry routing");
    Check(csol::SkipStudioSound(model,event,sample)==expected,"shared collection/playback rule");
}
}
int main()
{
    Case("models/v_dragonsword.mdl",5004,"weapons/dragonsword_stab.wav",true);
    Case("MODELS\\V_DRAGONSWORD.MDL",5004,"WEAPONS\\DRAGONSWORD_STAB.WAV",true);
    for(int event:{0,1,5003,5005,5008,15004,5639})
        Case("models/v_dragonsword.mdl",event,"weapons/dragonsword_stab.wav",false);
    for(const auto* sample:{"weapons/dragonsword_stab_hit.wav","weapons/dragonsword_draw.wav",
        "weapons/dragonsword_hit1.wav","weapons/dragonsword_slash1.wav","weapons/missing.wav",
        "sound/weapons/dragonsword_stab.wav","weapons/dragonsword_stab.wav.bak",""})
        Case("models/v_dragonsword.mdl",5004,sample,false);
    for(const auto* model:{"models/p_dragonsword.mdl","models/w_dragonsword.mdl","models/v_skullaxe.mdl",
        "models/v_dragonsword2.mdl","models/csol/v_dragonsword.mdl","v_dragonsword.mdl",""})
        Case(model,5004,"weapons/dragonsword_stab.wav",false);
    model_t model{};cl_entity_t entity{};mstudioevent_t event{};
    std::strcpy(model.name,"models/v_dragonsword.mdl");entity.model=&model;event.event=5004;
    std::strcpy(event.options,"weapons/dragonsword_stab.wav");
    continued=0;StudioEntry(nullptr,&entity);Check(continued==1,"null event forwards safely");
    continued=0;StudioEntry(&event,nullptr);Check(continued==1,"null entity forwards safely");
    entity.model=nullptr;continued=0;StudioEntry(&event,&entity);Check(continued==1,"null model forwards safely");
    entity.model=&model;
    std::memset(event.options,'x',sizeof(event.options));continued=0;StudioEntry(&event,&entity);
    Check(continued==1,"unterminated event options remain bounded and unfiltered");
    std::strcpy(event.options,"weapons/dragonsword_stab.wav");
    std::memset(model.name,'x',sizeof(model.name));continued=0;StudioEntry(&event,&entity);
    Check(continued==1,"unterminated model name remains bounded and unfiltered");
    std::memset(event.options,'x',sizeof(event.options));continued=0;StudioEntry(&event,&entity);
    Check(continued==1,"both full buffers remain bounded and unfiltered");
    const std::string targetModel="models/v_dragonsword.mdl",targetSample="weapons/dragonsword_stab.wav";
    for(std::size_t n=0;n<targetModel.size();++n)
        Check(!csol::SkipStudioSound(std::string_view(targetModel).substr(0,n),5004,targetSample),"truncated model never matches");
    for(std::size_t n=0;n<targetSample.size();++n)
        Check(!csol::SkipStudioSound(targetModel,5004,std::string_view(targetSample).substr(0,n)),"truncated sample never matches");
    std::printf("%s studio sound: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
