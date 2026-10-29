// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include "../sequence_owners.hpp"

namespace csol::client
{
void InstallSequenceEngineGuards();
void InstallSequenceClientGuards();
void InstallSequenceStudioGuards();
void RemoveSequenceStudioGuards();
void RemoveSequenceClientGuards();
void ResetSequenceOwners();
// A nested load token is local to the exact model cache owner. The original
// loader call must finish within this scope before publishing a receipt.
class SequenceLoadScope
{
    model_t* model_{};
    PreparedSequenceModel prepared_;
    SequenceOwnerRegistry::LoadToken token_{};
    bool completed_{};
public:
    SequenceLoadScope(model_t* model,std::span<const std::byte> bytes);
    ~SequenceLoadScope();
    void Complete();
    SequenceLoadScope(const SequenceLoadScope&)=delete;
    SequenceLoadScope& operator=(const SequenceLoadScope&)=delete;
};
}
