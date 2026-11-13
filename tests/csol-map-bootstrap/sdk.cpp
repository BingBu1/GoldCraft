// SPDX-License-Identifier: GPL-3.0-or-later
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include <iostream>
#include <string_view>

namespace bootstrap
{
using namespace sdk_test;
int behavior{};
void Check(bool value,const char* text){++behavior;sdk_test::Check(value,text);}
void Run(std::string_view group)
{
    Engine::startupArguments.clear();
    Engine::startupArguments["-csol_game_mode"]="2";
    std::optional<int> expected=113;
    bool valid=true;
    if(group=="minus") Engine::startupArguments["-csol_scenario_map_id"]="113";
    else if(group=="plus") Engine::startupArguments["+csol_scenario_map_id"]="113";
    else if(group=="precedence")
    { Engine::startupArguments["-csol_scenario_map_id"]="197"; Engine::startupArguments["+csol_scenario_map_id"]="113"; expected=197; }
    else if(group=="absent") expected.reset();
    else if(group=="zero") {Engine::startupArguments["-csol_scenario_map_id"]="0";expected.reset();}
    else if(group=="boundary") {Engine::startupArguments["-csol_scenario_map_id"]="65535";expected=65535;}
    else
    {
        valid=false;
        if(group=="missing") Engine::startupArguments["-csol_scenario_map_id"]=std::nullopt;
        else if(group=="empty") Engine::startupArguments["-csol_scenario_map_id"]="";
        else if(group=="negative") Engine::startupArguments["-csol_scenario_map_id"]="-1";
        else if(group=="overflow") Engine::startupArguments["-csol_scenario_map_id"]="65536";
        else if(group=="garbage") Engine::startupArguments["-csol_scenario_map_id"]="113x";
        else if(group=="fraction") Engine::startupArguments["-csol_scenario_map_id"]="113.0";
        else if(group=="plus-sign") Engine::startupArguments["-csol_scenario_map_id"]="+113";
        else throw std::runtime_error("unknown bootstrap group");
    }
    // Engine's constructor executes the actual CSOL_InitGameModes, real
    // CVAR registration and host argument consumer; no manual post-init set.
    auto storage=std::make_unique<Engine>();
    auto& engine=*storage;
    std::optional<int> value=77;
    const bool read=CSOL_RequestedScenarioMapId(value);
    Check(read==valid,"real cold-start getter accepts/rejects the original argument value");
    Check(value==(valid?expected:std::optional<int>(77)),"first-map identity is ready before command-buffer execution");
    Check(engine.Modes().BeginMap()==2,"map bootstrap preserves the actual initial scenario mode");
    Require(engine.Modes().ConfirmPluginIsolation(2),"real scenario isolation confirmed");
    engine.Set("csol_scenario_map_id","197");
    Engine::startupArguments["-csol_scenario_map_id"]="113";
    CSOL_InitGameModes();
    value.reset();
    Check(CSOL_RequestedScenarioMapId(value)&&value==197,"later initialization cannot overwrite an administrator/map-vote selection");
}
}
int main(int argc,char** argv)
{
    try{sdk_test::Require(argc==2,"one explicit bootstrap group");bootstrap::Run(argv[1]);}
    catch(const std::exception& e){++sdk_test::failures;std::cerr<<"ERROR "<<e.what()<<'\n';}
    std::cout<<"behaviorChecks="<<bootstrap::behavior<<" checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
