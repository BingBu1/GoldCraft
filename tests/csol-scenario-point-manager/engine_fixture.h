// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "precompiled.h"
#include "../../native/csol/server/scenario_point_manager.h"

struct Engine
{
    static Engine* active;
    enum class CreateFailure { None, Null, PrivateMissing };
    enum class SpawnFailure { None, Reject, Kill, Free, Recycle, ChangeClass, PrivateMissing, ReplaceWithBase };
    globalvars_t globals{};
    std::array<char,65536> strings{};
    std::size_t nextString{1};
    std::vector<std::unique_ptr<edict_t>> edicts;
    std::map<const edict_t*,int> indices;
    std::vector<void*> orphanedPrivate;
    // Only runtime CREATE_NAMED_ENTITY entries use this explicit registration
    // seam. Map-marker traversal remains the existing controlled list; the
    // real ReGameDLL hash implementation/order is not run by this fixture.
    std::set<const edict_t*> namedPending;
    std::map<const edict_t*,std::string> namedRegistrations;
    std::map<const edict_t*,int> spawnCalls;
    std::vector<std::string> calls;
    std::vector<int> removed;
    std::function<void(CBaseEntity&)> onRemove;
    std::function<void(edict_t*)> onNamedConstruction;
    CreateFailure createFailure{};
    SpawnFailure spawnFailure{};
    int creates{}, dispatches{}, searches{}, removes{}, setOrigins{};
    Engine();
    ~Engine();
    CBaseEntity& Construct(const char* classname, int index, bool named=false);
    CBaseEntity& Add(const char* classname, int index);
    CCSOLPlayerSpawn& Player(int index, int id=0, int team=2);
    CCSOLMonsterSpawn& Monster(int index, int id=0, int maxSpawn=0, int team=1);
    void ClearEdicts();
    CBaseEntity& ReplaceWithBase(edict_t*);
    static void* Allocate(edict_t*,int32);
    static int Index(const edict_t*);
    static int Offset(const edict_t*);
    static edict_t* FromOffset(int);
    static int AllocString(const char*);
    static edict_t* Create(int);
    static void SetOrigin(edict_t*,const float*);
};
