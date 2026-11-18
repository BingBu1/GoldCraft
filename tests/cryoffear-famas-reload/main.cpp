// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/famas_reload.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using namespace guest::cof;
using Word = std::uint32_t;
unsigned checks{};
float Float(Word value) { return std::bit_cast<float>(value); }
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "FAMAS reload failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
std::vector<Word> Words(const std::string& text)
{
    std::istringstream in(text); std::vector<Word> result; Word value{};
    while (in >> std::hex >> value) result.push_back(value);
    if (!in.eof()) return {};
    return result;
}
void Safety()
{
    for (int clip : {30, 75, 511, 10000, std::numeric_limits<int>::max()})
    {
        MagazineReload state{clip, -1, 10.F, true};
        const auto result = CommitFamasReload(state, 30, 0, 10.F);
        CHECK(result.status == ReloadCommitStatus::Committed && result.rounds == 0);
        CHECK(state.clip == clip && state.reserve == -1 && !state.loading);
    }
    for (int capacity : {-1, 0})
    {
        MagazineReload state{1, 0, 10.F, true};
        CHECK(CommitFamasReload(state, capacity, 0, 10.F).status == ReloadCommitStatus::Invalid);
        CHECK(state.clip == 1 && state.loading && state.reserve == 0);
    }
    MagazineReload state{0, 0, 10.F, true};
    CHECK(CommitFamasReload(state, 30, 0, std::numeric_limits<float>::quiet_NaN()).status == ReloadCommitStatus::Invalid);
    CHECK(state.clip == 0 && state.loading);
    CHECK(CommitFamasReload(state, 30, 0, 10.F).rounds == 30);
    CHECK(state.clip == 30 && state.reserve == 0 && !state.loading);
    CHECK(CommitFamasReload(state, 30, 0, 10.F).status == ReloadCommitStatus::Pending);
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]);
    if (!cases || !expected) return 1;
    std::string input, answer; unsigned count{};
    while (std::getline(cases, input))
    {
        const auto v = Words(input); if (v.size() != 7) return 3;
        if (!std::getline(expected, answer)) return 4;
        MagazineReload state{int(v[0]), int(v[1]), Float(v[5]), v[6] != 0};
        const auto result = CommitFamasReload(state, int(v[2]), v[3], Float(v[4]));
        CHECK(result.status == ReloadCommitStatus::Pending || result.status == ReloadCommitStatus::Committed);
        const std::vector<Word> actual{Word(state.clip), Word(state.reserve), state.loading,
            result.status == ReloadCommitStatus::Committed, Word(result.rounds)};
        const auto wanted = Words(answer);
        if (actual != wanted)
        {
            std::cerr << "Original FAMAS reload mismatch at case " << count << '\n';
            return 5;
        }
        checks += unsigned(actual.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, answer)) return 7;
    Safety();
    std::cout << "originalCases=" << count << " checks=" << checks << " failures=0\n";
}
