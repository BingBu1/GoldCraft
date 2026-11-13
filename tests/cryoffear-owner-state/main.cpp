// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/server/owner_state.hpp"

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
    std::array<std::size_t, 4> counts{};
    std::size_t checks = 0;
    const auto asFloat = [](std::uint32_t value) { return std::bit_cast<float>(value); };
    const auto asInt = [](std::uint32_t value) { return std::bit_cast<std::int32_t>(value); };
    while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
    {
        std::array<std::uint32_t, 12> row{};
        for (auto& value : row)
            if (!(input >> std::hex >> value))
                return 3;
        bool matched = false;
        switch (row[0])
        {
        case 0:
        {
            const auto result = guest::cof::ComputeOwnerButtonEdges(row[1], row[2]);
            matched = result.pressed == row[3] && result.released == row[4];
            checks += 2;
            break;
        }
        case 1:
        {
            const guest::cof::OwnerJumpInputs inputs{static_cast<std::uint8_t>(row[1]),
                row[2], asInt(row[3]), asInt(row[4]), row[5], row[6] != 0};
            matched = guest::cof::CanBeginOwnerJump(inputs) == (row[7] != 0);
            ++checks;
            break;
        }
        case 2:
        {
            guest::cof::OwnerJumpState state{static_cast<std::uint8_t>(row[1]), asFloat(row[2])};
            guest::cof::RecordAcceptedOwnerJump(state, asFloat(row[3]));
            matched = state.justJumped == row[4] &&
                std::bit_cast<std::uint32_t>(state.deadline) == row[5];
            checks += 2;
            break;
        }
        case 3:
        {
            guest::cof::OwnerJumpState state{static_cast<std::uint8_t>(row[1]), asFloat(row[2])};
            guest::cof::AdvanceOwnerJumpAfterMovement(state, row[3], asFloat(row[4]));
            matched = state.justJumped == row[5] &&
                std::bit_cast<std::uint32_t>(state.deadline) == row[6];
            checks += 2;
            break;
        }
        default:
            return 4;
        }
        if (!matched)
        {
            std::cerr << "Original instruction mismatch kind " << row[0]
                      << " case " << counts[row[0]] << '\n';
            return 1;
        }
        ++counts[row[0]];
    }
    for (const auto count : counts)
        if (count == 0)
            return 5;
    std::cout << checks << " original-instruction checks; 0 failures\n";
}
