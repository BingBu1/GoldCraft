// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/spread.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    std::ifstream input(argv[1]);
    if (!input)
        return 2;
    std::size_t count = 0;
    while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
    {
        std::array<std::uint32_t, 26> row{};
        for (auto& value : row)
            if (!(input >> std::hex >> value))
                return 3;
        const auto asFloat = [](std::uint32_t bits) { return std::bit_cast<float>(bits); };
        guest::cof::SpreadState weapon{asFloat(row[0]), asFloat(row[1]),
            {asFloat(row[2]), asFloat(row[3]), asFloat(row[4])}};
        guest::cof::SpreadTransitions transitions{std::bit_cast<std::int32_t>(row[5]),
            row[6] != 0, row[7] != 0, row[8] != 0, row[9] != 0};
        const guest::cof::SpreadFrame frame{asFloat(row[10]), asFloat(row[11]), asFloat(row[12]),
            row[13], std::bit_cast<std::int32_t>(row[14]), row[15] != 0};
        guest::cof::UpdateSpread(weapon, transitions, frame);
        const std::array<std::uint32_t, 10> actual{
            std::bit_cast<std::uint32_t>(weapon.scalar), std::bit_cast<std::uint32_t>(weapon.recoveryDeadline),
            std::bit_cast<std::uint32_t>(weapon.vector[0]), std::bit_cast<std::uint32_t>(weapon.vector[1]),
            std::bit_cast<std::uint32_t>(weapon.vector[2]), std::bit_cast<std::uint32_t>(transitions.minimum),
            transitions.moving, transitions.airborne, transitions.ducking, transitions.sprinting};
        for (std::size_t i = 0; i < actual.size(); ++i)
            if (actual[i] != row[i + 16])
            {
                std::cerr << "Original instruction mismatch row " << count << " field " << i
                          << ": " << std::hex << actual[i] << " != " << row[i + 16] << '\n';
                return 1;
            }
        ++count;
    }
    if (count < 10000)
        return 4;
    std::cout << count << " original-instruction cases; " << count * 10 << " bit-exact checks; 0 failures\n";
}
