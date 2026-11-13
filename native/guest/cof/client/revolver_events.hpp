// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct cl_enginefuncs_s;
struct event_args_s;
namespace guest::cof::client
{
enum class RevolverEventResult { Issued,Ignored,MissingBackend,Cancelled };
struct RevolverEventHost
{
    cl_enginefuncs_s* engine{};
    const char* shellSample{}; // Actual cold-prepared logical/native resource mapping.
    void* context{};
    bool (*current)(void*) noexcept {};
};
// Original client d2a04641/10052750; event name does not imply model ejection.
// Host/context/module storage remains borrowed throughout this synchronous call.
RevolverEventResult RevolverShellEvent(const RevolverEventHost&,const event_args_s&) noexcept;
}
