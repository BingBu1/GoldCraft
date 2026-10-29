// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

int main(int argc, char** argv)
{
    using namespace position_test;
    try
    {
        if (argc != 2) throw std::runtime_error("one position group required");
        const std::string_view group = argv[1];
        if (group == "--schedule") SchedulingCases();
        else if (group == "--routes") RouteCases();
        else if (group == "--jump") JumpCases();
        else if (group == "--position-cancel") CancellationCases();
        else if (group == "--ladders") LadderCases();
        else throw std::runtime_error("unknown position group");
    }
    catch (const std::exception& error)
    {
        ++death_events_test::failures;
        std::cerr << "EXCEPTION " << error.what() << '\n';
    }
    std::cout << "monster_position checks=" << death_events_test::checks
        << " failures=" << death_events_test::failures << '\n';
    return death_events_test::failures ? 1 : 0;
}
