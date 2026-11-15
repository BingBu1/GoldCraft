// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/stamina.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace csol::scenario::custom;
namespace
{
int checks{}, failures{};
void Check(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
constexpr LifeKey Life{{7,11},19};
void Test()
{
    StaminaState state;
    Check(state.Begin(Life,Phase::Night,10),"normal finite life begins with original profile");
    Check(state.View(Life)->amount==100&&state.View(Life)->maximum==100,"original maximum100");
    for(int i=0;i<10;++i)Check(state.DebitKick(Life),"each accepted finite kick costs10");
    Check(state.View(Life)->amount==0&&!state.CanKick(Life)&&!state.DebitKick(Life),"insufficient stamina explicitly declines without negative amount");
    Check(!state.Begin(Life,Phase::Night,10),"weapon switch/duplicate preparation cannot refill");
    StaminaInput input;input.key=Life;input.command=1;input.now=10;input.phase=Phase::Night;
    Check(state.Step(input)->amount==0,"strict equality at cycle does not recover");
    // Original exhausted path seeds0.1 and waits5seconds; it is not replaced
    // with arbitrary catchup or a kick-specific instant full recovery.
    input.command++;input.now=std::nextafter(10.f,11.f);
    Check(state.Step(input)->amount==.1f,"original zero stamina seeds exhaustion only after strict deadline");
    StaminaState recovery;Check(recovery.Begin(Life,Phase::Night,10)&&recovery.DebitKick(Life),"recovery fixture debits accepted kick");
    input.command=1;input.now=10.25f;
    Check(recovery.Step(input)->amount==91,"ordinary one point recovery per accepted original cycle");
    input.command++;input.now=10.5f;
    Check(recovery.Step(input)->amount==91,"cycle equality cannot recover again");
    input.command++;input.now=std::nextafter(10.5f,11.f);input.ducked=true;
    Check(recovery.Step(input)->amount==93,"fully ducked state recovers original two points");
    input.command++;input.now=100;
    Check(recovery.Step(input)->amount==95,"large elapsed interval grants exactly one cycle, never catchup");
    StaminaConfig config;
    Check(recovery.Configure(Life,config,Phase::Dusk)&&recovery.View(Life)->maximum==150&&recovery.View(Life)->amount==95,"dusk expands capacity without refill");
    Check(recovery.DebitKick(Life)&&recovery.View(Life)->amount==85,"dusk kick remains finite");
    input.command++;input.now=101;input.phase=Phase::Dusk;
    Check(recovery.Step(input)->amount==87.5f,"dusk original duck recovery times custom1.25");
    Check(recovery.Configure(Life,config,Phase::Noon)&&recovery.CanKick(Life),"latest user rule allows kick in unlimited noon");
    for(int i=0;i<100;++i)Check(recovery.DebitKick(Life),"noon accepts without charging stamina");
    Check(recovery.View(Life)->amount==87.5f&&recovery.View(Life)->infinite,"noon infinite does not debit or refill");
    Check(recovery.Configure(Life,config,Phase::Night)&&recovery.CanKick(Life)&&!recovery.View(Life)->infinite,"time disabled/fallback returns finite normal profile without refill");
    config.maximum=80;
    Check(recovery.Configure(Life,config,Phase::Night)&&recovery.View(Life)->amount==80,"configuration update clamps rather than refills");
    config.recovery=0;config.duckRecovery=0;config.duskRecovery=0;
    Check(Valid(config)&&recovery.Configure(Life,config,Phase::Night),"explicit zero recovery is valid finite configuration");
    config.maximum=std::numeric_limits<float>::infinity();
    Check(!recovery.Configure(Life,config,Phase::Night)&&recovery.View(Life)->maximum==80,"invalid cold profile does not mutate state");
    Check(recovery.Retire(Life)&&!recovery.CanKick(Life)&&!recovery.DebitKick(Life)&&!recovery.Begin(Life,Phase::Noon,200),"retired life cannot debit or be rebound full");
}
}
int main(){Test();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;}
