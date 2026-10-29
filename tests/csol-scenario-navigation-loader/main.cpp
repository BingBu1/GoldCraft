// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.hpp"
#include "../csol-scenario-monster-events/fixture.hpp"
#include "../csol-scenario-monster-obstacles/fixture.hpp"
int main(int argc,char** argv)
{
    using namespace nav_loader_test;
    try
    {
        Require(argc>=2,"one NAV loader test group required");const std::string_view group=argv[1];
        if(group=="--parser"&&argc==3)ParserCases(argv[2]);
        else if(group=="--owner"&&argc==3)OwnerCases(argv[2]);
        else if(group=="--lifetime"&&argc==2)LifetimeCases();
        else if(group=="--ladders"&&argc==2)LadderCases();
        else if(group=="--ladder-cancel"&&argc==2)LadderCancellationCases();
        else if(group=="--session"&&argc==2)SessionCases();
        else if(group=="--events-scan"&&argc==2)model_events_test::ScanCases();
        else if(group=="--events-validation"&&argc==2)model_events_test::ValidationCases();
        else if(group=="--events-cancel"&&argc==2)model_events_test::CancellationCases();
        else if(group=="--events-owner"&&argc==2)model_events_test::OwnerIntegrationCases();
        else if(group=="--obstacles"&&argc==2)obstacle_test::ObstacleCases();
        else if(group=="--obstacle-cancel"&&argc==2)obstacle_test::ObstacleCancellationCases();
        else throw std::runtime_error("unknown NAV loader group or missing frozen resource argument");
    }
    catch(const std::exception& e){++difficulty_test::failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    const auto checks=difficulty_test::checks+death_events_test::checks;
    const auto failures=difficulty_test::failures+death_events_test::failures;
    std::cout<<"navigation-idle-events checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
