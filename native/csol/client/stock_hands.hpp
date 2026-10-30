// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"

namespace csol::client {
void ResetStockHands();
void StockHandDiagnostics(std::ostream& output);
void NativeHandFrameGrid(cl_entity_t* entity);

// Only the synchronous viewmodel draw sees the CSO skeleton. Prediction and
// inventory retain the native model; downloaded aliases have distinct caches.
class StockHandScope
{
public:
    explicit StockHandScope(cl_entity_t* entity);
    ~StockHandScope();
    void ClientEvents();
    void PrepareCustomRender(int flags);
    StockHandScope(const StockHandScope&)=delete;
    StockHandScope& operator=(const StockHandScope&)=delete;
private:
    cl_entity_t* entity{};
    model_t* model{};
    int index{};
    studiohdr_t* nativeHeader{};
    studiohdr_t* aliasHeader{};
    cvar_t* handedness{};
    float savedHandedness{};
};
}
