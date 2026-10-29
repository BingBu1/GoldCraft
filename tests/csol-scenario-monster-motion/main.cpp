// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
int main(int argc,char** argv)
{
    using namespace motion_test;
    try
    {
        if(argc!=2)throw std::runtime_error("one group required");
        const std::string_view group=argv[1];
        if(group=="--speed")SpeedCases();
        else if(group=="--neighbors")NeighborCases();
        else if(group=="--velocity")VelocityCases();
        else if(group=="--cancellation")CancellationCases();
        else if(group=="--cleanup")CleanupCases();
        else throw std::runtime_error("unknown motion group");
    }
    catch(const std::exception& e){++death_events_test::failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"monster_motion checks="<<death_events_test::checks<<" failures="<<death_events_test::failures<<'\n';
    return death_events_test::failures?1:0;
}
