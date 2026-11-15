// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/standalone_stamina_state.hpp"
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
    StandaloneStaminaState state;
    Check(state.Begin(Life,Phase::Night,10),"normal finite life begins with original profile");
    Check(state.View(Life)->amount==100&&state.View(Life)->maximum==100,"original maximum100");
    for(int i=0;i<10;++i)Check(state.DebitKick(Life),"each accepted finite kick costs10");
    Check(state.View(Life)->amount==0&&!state.CanKick(Life)&&!state.DebitKick(Life),"insufficient stamina explicitly declines without negative amount");
    Check(!state.Begin(Life,Phase::Night,10),"weapon switch/duplicate preparation cannot refill");
    Check(state.Step(Life,1,10,.1f)->amount==0,"debit observation does not recover immediately");
    Check(state.Step(Life,2,13,.1f)->amount==0&&!state.RecoveryVoice(),"original strict three-second wait retains zero");
    Check(state.Step(Life,3,13.1f,.1f)->amount>0&&state.RecoveryVoice(),"actual CSOL recovery starts with the original accumulator");
    StandaloneStaminaState recovery;
    Check(recovery.Begin(Life,Phase::Night,10)&&recovery.DebitKick(Life),"recovery fixture debits accepted kick");
    Check(recovery.Step(Life,1,10,.1f)->amount==90,"ordinary depletion restarts original recovery wait");
    Check(recovery.Step(Life,2,13,.1f)->amount==90,"original deadline equality cannot recover");
    const auto resumed=recovery.Step(Life,3,13.1f,.1f);
    Check(resumed&&resumed->amount>90&&resumed->amount<91,"original recovery increments per frame rather than per CoF cycle");
    const float balance=resumed->amount;
    Check(recovery.Step(Life,4,13.1f,.1f)->amount==balance,"same engine frame cannot add recovery twice");
    StandaloneStaminaConfig config;
    Check(recovery.Configure(Life,config,Phase::Dusk)&&recovery.View(Life)->maximum==150&&recovery.View(Life)->amount==balance,"dusk expands capacity without refill");
    Check(recovery.DebitKick(Life)&&recovery.View(Life)->amount==balance-10,"dusk kick remains finite");
    Check(recovery.Configure(Life,config,Phase::Noon)&&recovery.CanKick(Life),"latest user rule allows kick in unlimited noon");
    for(int i=0;i<100;++i)Check(recovery.DebitKick(Life),"noon accepts without charging stamina");
    Check(recovery.View(Life)->amount==balance-10&&recovery.View(Life)->infinite,"noon infinite does not debit or refill");
    Check(recovery.Configure(Life,config,Phase::Night)&&recovery.CanKick(Life)&&!recovery.View(Life)->infinite,"time disabled/fallback returns finite normal profile without refill");
    config.recovery.maximum=80;
    Check(recovery.Configure(Life,config,Phase::Night)&&recovery.View(Life)->amount==80,"configuration update clamps rather than refills");
    config.recoveryPerSecond=0;
    Check(Valid(config)&&recovery.Configure(Life,config,Phase::Night),"explicit zero recovery is valid finite configuration");
    config.recovery.maximum=std::numeric_limits<float>::infinity();
    Check(!recovery.Configure(Life,config,Phase::Night)&&recovery.View(Life)->maximum==80,"invalid cold profile does not mutate state");
    Check(recovery.Retire(Life)&&!recovery.CanKick(Life)&&!recovery.DebitKick(Life)&&!recovery.Begin(Life,Phase::Noon,200),"retired life cannot debit or be rebound full");
}
}
int main(){Test();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;}
