// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
int main(int argc,char** argv)
{
    using namespace spawn_test;
    try
    {
        if(argc==2&&std::string_view(argv[1])=="--selector") SelectorCases();
        else if(argc==2&&std::string_view(argv[1])=="--proxy-cancel") ProxyCancellationNegative();
        else throw std::runtime_error("expected --selector | --proxy-cancel");
    }
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"proxy lifecycle "<<checks<<" checks "<<failures<<" failures\n";
    return failures?1:0;
}
