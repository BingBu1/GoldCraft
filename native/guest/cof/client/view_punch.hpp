// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
struct event_args_s;
struct ref_params_s;
namespace guest::cof::client
{
struct ViewPunchState
{
    std::array<float, 3> angle{};
    std::array<float, 3> velocity{};
};
enum class PunchResult { Applied, CameraBlocked, InvalidInput };
// Caller owns this state on the view thread and authenticates event/camera state.
// No weapon/death reset: the original globals have no such observed clear.
PunchResult ViewPunchEvent(ViewPunchState&, const event_args_s&,
                           bool attachedCamera, int attachMode) noexcept;
// Invoke only at the original CalcRefdef stage, after engine punch was added.
// In particular, the original nextView==1 early return never reaches this stage.
// Adds the current angle before advancing it; does not add engine punch again.
PunchResult ViewPunchAtStage(ViewPunchState&, ref_params_s&, int maxClients) noexcept;
}
