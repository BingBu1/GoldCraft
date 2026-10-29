// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
int main(int argc,char** argv)
{
    using namespace wall_test;
    struct Group{const char* name;void(*run)();};
    const Group groups[]{{"numeric",NumericCases},{"metadata",MetadataCases},{"entities",EntityCases},{"damage",DamageCases},
        {"targets",TargetCases},{"delays",DelayCases},{"cancellation",CancellationCases},{"rewards",RewardCases}};
    try{
        Require(argc==2,"one explicit group per process");bool found=false;
        for(const auto& group:groups)if(std::string_view(argv[1])==group.name){found=true;group.run();break;}
        Require(found,"known wall group");
    }catch(const std::exception& error){++difficulty_test::failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"checks="<<difficulty_test::checks<<" failures="<<difficulty_test::failures<<'\n';
    return difficulty_test::failures?1:0;
}
