// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace equipment_test { void WeaponStateCases(); }
int main(int argc,char** argv)
{
    const struct Group{const char* name;void(*run)();} groups[]{
        {"resources",upgrade_test::ResourceCases},{"progress",upgrade_test::ProgressCases},
        {"consumers",upgrade_test::ConsumerCases},{"messages",upgrade_test::MessageCases},
        {"cancellation",upgrade_test::CancellationCases},{"equipment-resources",equipment_test::ResourceCases},
        {"equipment-selection",equipment_test::SelectionCases},{"equipment-effects",equipment_test::EffectCases},
        {"equipment-services",equipment_test::ServiceCases},{"weapon-state",equipment_test::WeaponStateCases}};
    try{
        difficulty_test::Require(argc==3,"one explicit combined group and frozen resource directory");
        upgrade_test::resourceRoot=argv[2];bool found=false;
        for(const auto& group:groups)if(std::string_view(argv[1])==group.name){found=true;group.run();break;}
        difficulty_test::Require(found,"known combined upgrade/equipment group");
    }catch(const std::exception& e){++difficulty_test::failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"checks="<<difficulty_test::checks<<" failures="<<difficulty_test::failures<<'\n';
    return difficulty_test::failures?1:0;
}
