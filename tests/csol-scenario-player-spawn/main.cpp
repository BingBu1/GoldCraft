// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace spawn_test { void OccupancyInitializationCases(); }
int main(int argc,char** argv)
{
    using namespace spawn_test;
    try
    {
        if(argc==2 && std::string_view(argv[1])=="--occupancy") OccupancyCases();
        else if(argc==2 && std::string_view(argv[1])=="--occupancy-initialization") OccupancyInitializationCases();
        else if(argc==2 && std::string_view(argv[1])=="--selector") SelectorCases();
        else if(argc==2 && std::string_view(argv[1])=="--proxy-cancel-negative") ProxyCancellationNegative();
        else if(argc==6 && std::string_view(argv[1])=="--actual") ActualCases(argv[2],argv[3],argv[4],argv[5]);
        else throw std::runtime_error("expected --occupancy | --occupancy-initialization | --selector | --actual common SPNT TRGV NAVG");
    }
    catch(const std::exception& error) { ++failures; std::cerr<<"EXCEPTION "<<error.what()<<'\n'; }
    std::cout<<"player-spawn "<<checks<<" checks "<<failures<<" failures\n";
    return failures?1:0;
}
