// SPDX-License-Identifier: GPL-3.0-or-later
// Expected to fail against preserved old production. This is never a product target.
#include "controller.hpp"
#include <iostream>
using namespace csol::scenario;
int main()
{
    int checks{},failures{},dispatches{};
    const auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';}};
    WaveTable global;WaveRow row;row.waveId=7;row.spawnPoint=1;row.startTime=0;row.endTime=0;row.interval=0;
    row.types={40};row.weights={1};row.weightTotal=1;global.rows.push_back(row);
    WaveController controller;ConfigError error;
    check(controller.Activate(global,{},error)&&!controller.FindWave(7),"empty runtime initialized despite globally defined wave7");
    check(controller.ActivateWave(global,7,5.5f,error),"missing runtime activation accepted");
    const auto* runner=controller.FindWave(7);
    check(runner&&runner->Rows().empty()&&runner->StartOffset()==5.5f,"runtime-absent source-present activation must stay empty");
    const auto callback=[](void* ctx,const WaveRow&,RowClock&){++*static_cast<int*>(ctx);};
    const auto tick=controller.Tick(5.5f,callback,&dispatches);
    check(tick.valid&&tick.finished,"empty runtime work should complete");
    check(tick.attempts==0&&dispatches==0,"global source must not dispatch when current round did not preload it");
    std::cout<<"runtime_cache_negative checks="<<checks<<" failures="<<failures<<" dispatches="<<dispatches<<'\n';
    return failures?1:0;
}
