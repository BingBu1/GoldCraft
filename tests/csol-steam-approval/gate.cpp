// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "steam_approval.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace gate_test { RehldsFuncs_t functions{}; unsigned registrations{}; void* published{}; }
#define g_RehldsApiFuncs gate_test::functions
#include "../../native/engine/steam_approval.cpp"
#undef g_RehldsApiFuncs
#include "steam-init-call.inc"
server_static_t g_psvs{};
cvar_t sv_lan={"sv_lan","0",0,0,nullptr};
CSteam3Server* Steam3Server(){return nullptr;}
static unsigned checks{};
static void Require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
int main(){
    try{
        gate_test::functions.RegisterPluginApi=[](const char* name,void* value){
            Require(std::strcmp(name,goldcraft::steamapproval::ApiName)==0,"wrong registered API name");
            ++gate_test::registrations;gate_test::published=value;
        };
        for(unsigned restart=0;restart<3;++restart){
            InvokeApprovalFromServerInit();
            Require(gate_test::registrations==(GC_SteamApprovalEnabled()?restart*2+1:0),"SV_Init registration gate failed");
            GC_SteamApprovalInit();
            Require(gate_test::registrations==(GC_SteamApprovalEnabled()?restart*2+2:0),"direct initialization gate failed");
            GC_SteamApprovalClear();
        }
        if(GC_SteamApprovalEnabled()){
            auto* published=static_cast<goldcraft::steamapproval::Api*>(gate_test::published);
            Require(published && published->version==1 && published->size==sizeof(*published),"published API shape changed");
            Require(published->getApproved && published->isCurrent,"published API callbacks missing");
            goldcraft::steamapproval::Identity identity{};
            Require(!published->getApproved(0,1,&identity,sizeof(identity)),"unapproved identity accepted");
            Require(!published->isCurrent(nullptr,0),"missing identity accepted");
        }else Require(!gate_test::published,"disabled API leaked to plugins");
        std::cout<<"Steam approval gate "<<(GC_SteamApprovalEnabled()?"ON":"OFF")<<": "<<checks<<" checks passed; native authentication not exercised\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
