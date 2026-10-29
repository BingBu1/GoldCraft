// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace position_test
{
void SchedulingCases();
void RouteCases();
void JumpCases();
void CancellationCases();
void LadderCases();
}
int main(int argc, char** argv)
{
    using namespace ordinary_test;
    try
    {
        if (argc != 2) throw std::runtime_error("one test group required");
        const std::string_view group = argv[1];
        if (group == "--owner-clock") OwnerClockCases();
        else if (group == "--owner-cancel") OwnerCancellationCases();
        else if (group == "--targets") TargetCases();
        else if (group == "--private-free") PrivateFreeCases();
        else if (group == "--gait") GaitCases();
        else if (group == "--effects") DamageEffectCases();
        else if (group == "--effects-dispatch") DamageEffectDispatchCases();
        else if (group == "--schedule") position_test::SchedulingCases();
        else if (group == "--routes") position_test::RouteCases();
        else if (group == "--jump") position_test::JumpCases();
        else if (group == "--position-cancel") position_test::CancellationCases();
        else if (group == "--ladders") position_test::LadderCases();
        else throw std::runtime_error("unknown test group");
    }
    catch (const std::exception& error)
    { ++death_events_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "monster_ordinary checks=" << death_events_test::checks << " failures=" << death_events_test::failures << '\n';
    return death_events_test::failures ? 1 : 0;
}
