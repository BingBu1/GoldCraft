// SPDX-License-Identifier: GPL-3.0-or-later
#include "../csol-scenario-death-events/fixture.hpp"
#include "allocation_probe.hpp"

namespace ordinary_test
{
using death_events_test::Check;
using death_events_test::Require;

namespace
{
struct TouchLifetimeProbe final : CCSOLMonsterTouchComponent
{
    int& destroyed;
    explicit TouchLifetimeProbe(int& value) : destroyed(value) {}
    ~TouchLifetimeProbe() override { ++destroyed; }
    void Touch(CCSOLScenarioMonster&, CBaseEntity*) override
    { throw std::runtime_error("unexpected contact in private-data lifetime case"); }
};
}

// Model the engine's actual private-data boundary: virtual OnDestroy, delete
// the distinct ReAPI wrapper, then free the allocation. Deliberately never
// invoke the monster C++ destructor unless the compatibility case asks for it.
void RawFree(edict_t& edict, bool ordinaryCppDestruction = false)
{
    auto* monster = static_cast<CCSOLScenarioMonster*>(edict.pvPrivateData);
    if (!monster) return;
    auto* wrapper = monster->m_pEntity;
    monster->OnDestroy();
    if (ordinaryCppDestruction) monster->~CCSOLScenarioMonster();
    delete wrapper;
    std::free(monster);
    edict.pvPrivateData = nullptr;
    ++edict.serialnumber;
    edict.free = TRUE;
}

void PrivateFreeCases()
{
    // Empty MSVC list sentinel + deque storage must be destroyed even when no
    // timed effects ever existed. Capture actual global allocations made by
    // the real entity export, excluding only its separately owned API wrapper.
    for (int populated : {0, 1})
        for (int compatibility : {0, 1})
        {
            Engine engine;
            edict_t edict{};
            edict.serialnumber = 1;
            edict.v.pContainingEntity = &edict;
            int componentDeaths{};
            allocation_probe::Begin();
            monster_entity(&edict.v);
            auto* monster = static_cast<CCSOLScenarioMonster*>(edict.pvPrivateData);
            for (std::size_t i = 0; i < 6; ++i)
                Require(monster->SetTouchComponent(i, std::make_shared<TouchLifetimeProbe>(componentDeaths)),
                    "each actual host contact slot owns its lifetime probe");
            if (populated)
            {
                for (int i = 0; i < 160; ++i)
                {
                    monster->SpeedState().limits.push_back({i, 100, 90, 1, 0, -1});
                    monster->SpeedState().effects.push_back({i, 80, 0, 100, 1, 80});
                }
            }
            allocation_probe::End();
            Require(!allocation_probe::Overflowed(), "allocation observer capacity sufficient");
            const auto outstanding = allocation_probe::Outstanding(monster->m_pEntity);
            Check(outstanding > 0, "real empty/populated speed state allocated storage");
            monster->OnDestroy();
            Check(allocation_probe::Outstanding(monster->m_pEntity) == 0,
                "all speed and contact allocations released inside OnDestroy before raw free");
            Check(componentDeaths == 6 && !monster->SetTouchComponent(0, {}),
                "OnDestroy releases all six contact components and rejects reinstallation");
            monster->OnDestroy();
            Check(allocation_probe::Outstanding(monster->m_pEntity) == 0,
                "repeated OnDestroy does not recreate or double-release speed state");
            RawFree(edict, compatibility != 0);
            Check(allocation_probe::Outstanding() == 0, "wrapper and raw entity release leave no tagged allocations");
            Check(componentDeaths == 6, "raw-free and optional C++ destructor never release contact components twice");
        }

    // The pool remains real. Tag only newly added effect nodes, excluding
    // logging/maps owned by the engine fixture, then exercise its real map
    // shutdown and delayed private-data removal for every one of 45 slots.
    {
        PoolFixture fixture;
        std::array<edict_t*, CCSOLMonsterPool::Capacity> edicts{};
        for (std::size_t i = 0; i < edicts.size(); ++i) edicts[i] = fixture.pool.At(i)->edict();
        allocation_probe::Begin();
        for (auto* edict : edicts)
            static_cast<CCSOLScenarioMonster*>(edict->pvPrivateData)->SpeedState().effects.push_back({1, 80, 0, 100, 1, 80});
        allocation_probe::End();
        Require(!allocation_probe::Overflowed(), "pool allocation observer capacity sufficient");
        Check(allocation_probe::Outstanding() == edicts.size(), "one actual list node tagged per slot");
        Require(fixture.pool.Shutdown(), "pool map shutdown succeeds");
        Check(allocation_probe::Outstanding() == edicts.size(), "shutdown retains private storage until engine callback");
        for (auto* edict : edicts) RawFree(*edict);
        Check(allocation_probe::Outstanding() == 0, "all pool effect nodes released by raw-free path");
        Check(fixture.pool.ForgetMap(), "pool forgets already freed map without entity access");
    }

    // A behavior destructor may call OnDestroy again. Keep the underlying
    // edict alive until the outer callback has returned, as the engine does.
    {
        PoolFixture fixture;
        auto* monster = fixture.pool.Acquire(fixture.types[0], 7, fixture.points);
        Require(monster != nullptr, "reentrant destruction borrows real pool slot");
        BehaviorStats stats;
        stats.onDestroy = [&]
        {
            Check(monster->Pool() == nullptr, "owner detached before behavior destructor reentry");
            Check(allocation_probe::Outstanding() == 0, "effect storage released before behavior destructor");
            monster->OnDestroy();
        };
        Require(monster->InstallBehavior(std::make_unique<BehaviorProbe>(stats)), "install explicit destructor probe");
        allocation_probe::Begin();
        monster->SpeedState().effects.push_back({1, 80, 0, 100, 1, 80});
        allocation_probe::End();
        RawFree(*monster->edict());
        Check(stats.destroyed == 1, "reentrant OnDestroy destroys behavior exactly once");
        Check(allocation_probe::Outstanding() == 0, "reentrant raw-free leaves no effect allocation");
    }
}
}
