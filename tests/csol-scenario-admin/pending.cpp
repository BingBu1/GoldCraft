// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "interface.h"
#include "../../native/csol/server/game_mode.h"
#include "../csol-scenario-difficulty/support.hpp"
#include <map>

bool g_bAllowedCSBot{};
enginefuncs_t g_engfuncs{};
namespace {
using namespace difficulty_test;
std::map<std::string,cvar_t*> vars;
std::map<std::string,std::string> texts;
int writes{};
bool accept{true};
void Register(cvar_t* value){vars[value->name]=value;}
void Set(const char* name,const char* value){++writes;if(!accept)return;auto& text=texts[name];text=value;vars.at(name)->string=text.data();}
void Cases(){
    Check(!CSOL_WriteScenarioDifficultyRequest(2),"unregistered mode state cannot write a pending request");
    g_engfuncs.pfnCVarRegister=Register;g_engfuncs.pfnCVarSetString=Set;CSOL_InitGameModes();
    Files files;std::string table="header\r\n";for(int row=1;row<=7;++row){for(int col=0;col<64;++col){if(col)table+=',';table+=std::to_string(row);}table+="\r\n";}
    files.contents["maps/ZBS_Difficulty_Default.cso"]=Encrypt(table);
    csol::scenario::DifficultyResources difficulty;csol::scenario::ResourceError error;
    Require(difficulty.BeginMap("map_session_test",2,{Files::Load,Files::Free},error),"actual difficulty snapshot loads selected row2");
    const auto* snapshot=difficulty.Table();std::uint16_t requested{};
    Check(CSOL_WriteScenarioDifficultyRequest(6)&&CSOL_RequestedScenarioDifficulty(requested)&&requested==6,
        "actual pending writer and strict reader agree on six");
    Check(difficulty.Table()==snapshot&&difficulty.SelectedRow()==2,"writing pending cvar cannot mutate loaded difficulty snapshot");
    const int before=writes;
    Check(!CSOL_WriteScenarioDifficultyRequest(0)&&!CSOL_WriteScenarioDifficultyRequest(8)&&writes==before,
        "writer rejects zero and above global seven without engine mutation");
    Check(CSOL_WriteScenarioDifficultyRequest(7)&&CSOL_RequestedScenarioDifficulty(requested)&&requested==7,
        "global writer allows seventh grade while map policy belongs to provider");
    accept=false;Check(!CSOL_WriteScenarioDifficultyRequest(3)&&CSOL_RequestedScenarioDifficulty(requested)&&requested==7,
        "engine refusal is observed rather than reported as a successful pending write");accept=true;
    g_engfuncs.pfnCVarSetString=nullptr;Check(!CSOL_WriteScenarioDifficultyRequest(2),"missing cvar writer rejects request");g_engfuncs.pfnCVarSetString=Set;
    Set("csol_scenario_difficulty","6x");Check(!CSOL_RequestedScenarioDifficulty(requested),"pending reader rejects trailing junk");
    Set("csol_scenario_difficulty","6");Require(difficulty.Clear(),"actual difficulty owner clears cleanly");
    Require(CSOL_RequestedScenarioDifficulty(requested)&&difficulty.BeginMap("map_session_test",requested,{Files::Load,Files::Free},error),"fresh difficulty session reads pending request");
    Check(difficulty.SelectedRow()==6,"only a new load captures the requested row");
}
}
int main(){try{Cases();}catch(const std::exception& e){++difficulty_test::failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"scenario-admin pending "<<difficulty_test::checks<<" checks "<<difficulty_test::failures<<" failures\n";return difficulty_test::failures?1:0;
}
