// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
int main(int argc,char** argv)
{
    using namespace actions_test;
    try
    {
        if(argc!=2)throw std::runtime_error("one test group required");
        const std::string_view group=argv[1];
        if(group=="--heading")HeadingCases();
        else if(group=="--melee")MeleeCases();
        else if(group=="--melee-cancel")MeleeCancellationCases();
        else if(group=="--follow")FollowCases();
        else if(group=="--follow-cancel")FollowCancellationCases();
        else if(group=="--motion")MotionCases();
        else throw std::runtime_error("unknown group");
    }
    catch(const std::exception& error){++death_events_test::failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"monster_actions checks="<<death_events_test::checks<<" failures="<<death_events_test::failures<<'\n';
    return death_events_test::failures?1:0;
}
