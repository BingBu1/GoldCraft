// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/settings.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace csol::scenario::custom
{
struct StoreError
{
    std::string message;
    std::filesystem::path path;
    std::uint32_t systemCode{};
    Error validation;
};
struct StoreResult
{
    std::uint64_t transaction{},revision{};
    bool success{};
    SnapshotPtr snapshot;
    StoreError error;
};
// Trusted server adapter supplies an absolute local game directory. The only
// managed target is addons/goldcraft/config/gameplay.json below it. Open is
// cold startup IO; edits use one worker and one outstanding transaction.
class SettingsStore
{
public:
    static std::unique_ptr<SettingsStore> Open(const std::filesystem::path& gameDirectory,StoreError& error);
    ~SettingsStore();
    SettingsStore(const SettingsStore&)=delete;
    SettingsStore& operator=(const SettingsStore&)=delete;
    SnapshotPtr Snapshot() const;
    std::uint64_t Revision() const;
    bool Busy() const;
    // Owner-thread cold edit. Zero rejects (including busy/stale revision).
    // Authorization linearizes here in the trusted adapter; no engine or
    // permission callbacks run on the worker, and rename cannot be revoked.
    std::uint64_t Submit(std::uint64_t expectedRevision,SnapshotPtr candidate,StoreError& error);
    std::uint64_t PrepareEdit(std::uint64_t expectedRevision,const Draft& draft,StoreError& error);
    // Owner thread polls without IO/wait. Successful persistence alone yields
    // the candidate for adapter publication; failure keeps Snapshot/Revision.
    // Final result is consumed once; until consumed, another edit stays busy.
    std::optional<StoreResult> Poll();
    const std::filesystem::path& Path() const;
private:
    struct State;
    explicit SettingsStore(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};
}
