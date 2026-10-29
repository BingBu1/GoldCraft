// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "../../native/csol/server/scenario_nav_path.h"
#include "../../native/csol/server/scenario_nav_world.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace navigation_test
{
extern int checks, failures;
void Check(bool condition, const char* label);
void Require(bool condition, const char* label);
bool Equal(const Vector& a, const Vector& b);
struct Graph
{
    std::vector<std::unique_ptr<CNavArea>> owned;
    std::vector<CNavArea*> areas;
    CNavArea* Area(const Vector& lo, const Vector& hi, float ne, float sw);
    CNavArea* Flat(float x, float y, float z = 0, float half = 20);
    void Clear();
    ~Graph() { Clear(); }
};
void Connect(CNavArea* a, CNavArea* b, NavDirType direction, bool reverse = true);
CNavLadder Ladder(CNavArea* bottom, CNavArea* top, bool dangling = false);
struct Step
{
    float z{};
    bool solid{};
    float fraction{1};
    edict_t* hit{};
    Vector normal{0, 0, 1};
};
struct TraceCall { Vector start, end; int flags{}; edict_t* ignored{}; };
class Engine
{
public:
    Engine();
    ~Engine();
    std::vector<Step> steps;
    std::vector<TraceCall> calls;
    std::function<void(std::size_t, TraceResult&)> onTrace;
    std::array<edict_t, 4> entities{};
    void Layers(std::initializer_list<float> heights);
    void Solid(unsigned count = 10);
    void ResetTrace();
private:
    static Engine* current;
    static void Trace(const float*, const float*, int, edict_t*, TraceResult*);
    enginefuncs_t previousEngine{};
    globalvars_t* previousGlobals{};
    globalvars_t globals{};
    std::array<char, 128> strings{};
};
void SearchCases();
void WorldCases(const char* navFixture);
void RouteCases();
}
