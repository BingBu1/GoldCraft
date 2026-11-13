// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/magazine_reload.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
struct Observation { int count{}, clip{-1}, reserve{-1}, loading{-1}; };
void Observe(void* opaque, const guest::cof::MagazineReload& state) noexcept
{
    auto& observed = *static_cast<Observation*>(opaque);
    ++observed.count;
    observed.clip = state.clip;
    observed.reserve = state.reserve;
    observed.loading = state.loading;
}

bool RejectedHostInputs()
{
    using namespace guest::cof;
    MagazineReload invalid[]{ {-1, 2, 1, true}, {0, -1, 1, true},
        {0, 1, std::numeric_limits<float>::infinity(), true} };
    for (auto& state : invalid)
    {
        const auto before = state;
        Observation observed;
        const auto result = CommitMagazineReload(state, 15, 0, 2, Observe, &observed);
        if (result.status != ReloadCommitStatus::Invalid || observed.count ||
            state.clip != before.clip || state.reserve != before.reserve ||
            state.loading != before.loading || state.nextAttack != before.nextAttack) return false;
    }
    MagazineReload state{0, 10, 1, true};
    return CommitMagazineReload(state, 0, 0, 2).status == ReloadCommitStatus::Invalid &&
        CommitMagazineReload(state, 15, 0, std::numeric_limits<float>::quiet_NaN()).status ==
            ReloadCommitStatus::Invalid && state.clip == 0 && state.reserve == 10 && state.loading;
}
bool AuthoritativeOverfill()
{
    using namespace guest::cof;
    for(int capacity:{8,15,18})for(int clip:{capacity+1,capacity*5,std::numeric_limits<int>::max()})
    {
        MagazineReload state{clip,500,1,true};Observation observed;
        const auto result=CommitMagazineReload(state,capacity,0,2,Observe,&observed);
        if(result.status!=ReloadCommitStatus::Committed || result.rounds!=0 ||
           state.clip!=clip || state.reserve!=500 || state.loading || observed.count!=1 ||
           observed.clip!=clip || observed.reserve!=500) return false;
        Observation again;const auto repeated=CommitMagazineReload(state,capacity,0,3,Observe,&again);
        if(repeated.status==ReloadCommitStatus::Committed || again.count || state.clip!=clip || state.reserve!=500)return false;
    }
    return true;
}
}

// Batch driver for the original-byte oracle. No original binary or generated
// third-party instructions are copied into public source.
int main()
{
    if (!RejectedHostInputs() || !AuthoritativeOverfill()) return 2;
    int clip{}, reserve{}, capacity{}, loading{};
    unsigned flags{}, nowBits{}, deadlineBits{};
    while (std::scanf("%d %d %d %u %u %u %d", &clip, &reserve, &capacity,
        &flags, &nowBits, &deadlineBits, &loading) == 7)
    {
        guest::cof::MagazineReload state{clip, reserve, std::bit_cast<float>(deadlineBits), loading != 0};
        Observation observed;
        auto result = guest::cof::CommitMagazineReload(state, capacity, flags,
            std::bit_cast<float>(nowBits), Observe, &observed);
        const auto first = state;
        Observation second;
        auto repeated = guest::cof::CommitMagazineReload(state, capacity, flags,
            std::bit_cast<float>(nowBits), Observe, &second);
        if (second.count || repeated.status == guest::cof::ReloadCommitStatus::Committed ||
            state.clip != first.clip || state.reserve != first.reserve || state.loading != first.loading)
            return 3;
        std::printf("%d %d %d %d %d %d %d %d %d\n", state.clip, state.reserve, state.loading,
            static_cast<int>(result.status), result.rounds, observed.count,
            observed.clip, observed.reserve, observed.loading);
    }
}
