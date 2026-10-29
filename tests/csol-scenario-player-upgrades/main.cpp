// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
int main(int argc,char** argv)
{
    using namespace upgrade_test;
    const struct Group{const char* name;void(*run)();} groups[]{
        {"resources",ResourceCases},{"progress",ProgressCases},{"consumers",ConsumerCases},
        {"messages",MessageCases},{"cancellation",CancellationCases}};
    try{
        Require(argc==3,"one explicit group and frozen resource directory");resourceRoot=argv[2];bool found=false;
        for(const auto& group:groups)if(std::string_view(argv[1])==group.name){found=true;group.run();break;}
        Require(found,"known player-upgrade test group");
    }catch(const std::exception& error){++difficulty_test::failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"checks="<<difficulty_test::checks<<" failures="<<difficulty_test::failures<<'\n';
    return difficulty_test::failures?1:0;
}
