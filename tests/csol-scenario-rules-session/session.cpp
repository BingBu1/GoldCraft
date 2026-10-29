// SPDX-License-Identifier: GPL-3.0-or-later
// Layer A: real session/map/pool/model/message objects. The inherited old
// Engine DispatchSpawn and unrelated SDK virtual methods remain substitutes.
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../../native/csol/server/scenario_session.h"
#include "resources.hpp"
#include <cstring>
#include <iostream>

extern "C" void zbstrigger(entvars_t*);
extern "C" void playerspawn(entvars_t*);
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
struct SessionEngine : PoolEngine
{
    inline static SessionEngine* self{};
    std::map<std::string, std::string> files{session_test::Resources()};
    std::map<void*, std::string> buffers;
    std::vector<std::string> order;
    std::function<void()> callback;
    int messageId{100}, modelFailureAt{}, modelCount{};
    SessionEngine()
    {
        self = this;
        g_engfuncs.pfnLoadFileForMe = Load; g_engfuncs.pfnFreeFile = Free;
        g_engfuncs.pfnRegUserMsg = Register; g_engfuncs.pfnPrecacheModel = Model;
        g_engfuncs.pfnCreateNamedEntity = CreateAny;
        g_engfuncs.pfnIsDedicatedServer = [] { return 1; };
        onCreate = [&] { order.emplace_back("pool"); if (callback) callback(); };
    }
    static edict_t* CreateAny(int classname)
    {
        auto& e = *self;
        if (std::strcmp(STRING(classname), "monster_entity") == 0) return PoolEngine::CreatePoolEntity(classname);
        if (std::strcmp(STRING(classname), "playerspawn") != 0) throw std::runtime_error("unexpected session entity export");
        ++e.creates;
        auto ed = std::make_unique<edict_t>(); ed->v.pContainingEntity = ed.get(); ed->serialnumber = 1;
        auto* raw = ed.get(); e.indices[raw] = 10000 + e.creates; e.edicts.push_back(std::move(ed));
        raw->v.classname = classname; playerspawn(&raw->v); return raw;
    }
    ~SessionEngine()
    {
        Check(buffers.empty(), "session released all file buffers");
        for (auto& [p, name] : buffers) delete[] static_cast<unsigned char*>(p);
        self = nullptr;
    }
    static unsigned char* Load(const char* name, int* length)
    {
        auto& e = *self; e.order.push_back(std::string("file:") + name);
        if (e.callback) e.callback();
        const auto found = e.files.find(name); if (found == e.files.end()) return nullptr;
        *length = static_cast<int>(found->second.size()); auto* p = new unsigned char[*length + 1];
        std::memcpy(p, found->second.data(), *length); p[*length] = 0; e.buffers.emplace(p, name); return p;
    }
    static void Free(void* p)
    {
        auto& e = *self; if (!e.buffers.erase(p)) throw std::runtime_error("unknown file free");
        delete[] static_cast<unsigned char*>(p); if (e.callback) e.callback();
    }
    static int Register(const char* name, int size)
    {
        auto& e = *self; e.order.emplace_back("message");
        Check(std::strcmp(name, "MPToCL") == 0 && size == -1, "real message registration contract");
        if (e.callback) e.callback(); return e.messageId;
    }
    static int Model(const char* name)
    {
        auto& e = *self; e.order.emplace_back("model"); ++e.modelCount;
        const int id = PoolEngine::Precache(name); if (e.callback) e.callback();
        return e.modelCount == e.modelFailureAt ? 0 : id;
    }
    void Markers()
    {
        Player(101, 1, 2); Monster(201, 1, 5, 1);
        for (int id : {1, 2, 30})
        {
            auto ed = std::make_unique<edict_t>(); ed->v.pContainingEntity = ed.get(); ed->serialnumber = 1;
            auto* raw = ed.get(); indices[raw] = 300 + id; edicts.push_back(std::move(ed));
            raw->v.classname = AllocString("zbstrigger"); raw->v.model = AllocString("*1"); zbstrigger(&raw->v);
            auto* trigger = static_cast<CCSOLScenarioTrigger*>(raw->pvPrivateData);
            trigger->m_iId = id; trigger->Spawn(); trigger->pev->mins = Vector(-5,-5,-5); trigger->pev->maxs = Vector(5,5,5);
        }
    }
};
void DenyReentry(CCSOLScenarioSession& session)
{
    const auto state = session.State(); const auto* tables = session.Map().Tables();
    Check(!session.Precache("map_session_test") && !session.Activate() && !session.Deactivate(), "busy session rejects nested lifecycle");
    Check(state == session.State() && tables == session.Map().Tables(), "nested rejection preserves state and tables");
}
void Lifecycle()
{
    SessionEngine e; CCSOLScenarioSession s;
    Check(s.State() == CSOLScenarioSessionState::Empty && !s.Activate(), "empty cannot activate");
    Check(s.Deactivate() && s.Deactivate(), "empty repeated deactivate");
    int reentries{}; e.callback = [&] { ++reentries; DenyReentry(s); };
    Check(s.Precache("map_session_test"), "precache real five resources and pool"); e.callback = {};
    Check(reentries > 45 && e.creates == 45 && e.dispatches == 45, "load and creation callbacks exercised with 45 initial dispatches");
    Check(s.State() == CSOLScenarioSessionState::Precached && s.Monsters().AvailableCount() == 45 && s.Models().Ready(), "precache commits state and resources");
    auto model = std::find(e.order.begin(), e.order.end(), "model"), message = std::find(e.order.begin(), e.order.end(), "message"), pool = std::find(e.order.begin(), e.order.end(), "pool");
    Check(model != e.order.end() && model < message && message < pool, "models before message before pool");
    Check(!s.Precache("map_session_test"), "duplicate precache rejected"); e.Markers();
    e.onRemove = [&](CBaseEntity&) { DenyReentry(s); };
    Check(s.Activate(), "activate collects exported markers"); e.onRemove = {};
    const auto removes = e.removes, creates = e.creates, searches = e.searches;
    Check(s.Activate() && e.removes == removes && e.creates == creates && e.searches == searches, "active activate is idempotent");
    const auto* tables = s.Map().Tables(); Check(tables && !tables->monsterTypes.rows.empty(), "map type table live");
    auto* monster = s.Monsters().Acquire(tables->monsterTypes.rows.front(), 1, s.Map().SpawnPoints().Points());
    Check(monster != nullptr, "borrow a slot using map type record");
    BehaviorStats stats;
    if (monster)
    {
        const auto* type = monster->Type();
        stats.onDestroy = [&] {
            Check(s.Map().Tables() == tables && type->id == 1, "borrowed type alive in behavior destructor");
            DenyReentry(s);
        };
        Check(monster->InstallBehavior(std::make_unique<BehaviorProbe>(stats)), "install lifecycle-only behavior probe");
    }
    e.onRemove = [&](CBaseEntity&) { Check(s.Map().Tables() == tables, "pool removal precedes map clear"); DenyReentry(s); };
    Check(s.Deactivate(), "active session deactivates"); e.onRemove = {};
    Check(stats.destroyed == 1 && s.State() == CSOLScenarioSessionState::Empty && !s.Map().Tables() && !s.Models().Ready(), "clear occurs after behavior destruction");
    const auto after = e.removes; Check(s.Deactivate() && e.removes == after, "second deactivate performs no removals");
    Check(!s.Messages().SendWaveEvent(std::array<std::uint8_t,4>{0x1b,7,1,0}), "deactivate invalidates message ID");
    Check(s.Precache("map_session_test") && s.Deactivate(), "same owner can load again after complete cleanup");
}
void Failures()
{
    for (int fault = 0; fault < 6; ++fault)
    {
        SessionEngine e; CCSOLScenarioSession s;
        if (fault == 0) e.files.erase("maps/zbs_wave_map_session_test.cso");
        if (fault == 1) e.modelFailureAt = 1;
        if (fault == 2) e.messageId = 63;
        if (fault >= 3) { e.failAt = fault == 3 ? 1 : fault == 4 ? 22 : 45; e.createFault = Engine::CreateFailure::Null; }
        Check(!s.Precache("map_session_test") && s.State() == CSOLScenarioSessionState::Failed, "precache stage failure is explicit");
        const auto error = s.Error().detail.message; Check(!error.empty(), "failure records error");
        Check(!s.Activate() && !s.Precache("map_session_test") && s.Error().detail.message == error, "failed state cannot continue or lose original error");
        Check(s.Deactivate() && s.Deactivate() && !s.Map().Tables() && !s.Models().Ready(), "partial acquisition cleans up repeatedly");
    }
    SessionEngine e;
    { CCSOLScenarioSession s; Check(s.Precache("map_session_test"), "destructor case owns live pool"); }
    for (const auto& ed : e.edicts)
        if (auto* monster = dynamic_cast<CCSOLScenarioMonster*>(static_cast<CBaseEntity*>(ed->pvPrivateData)))
            Check(monster->Pool() == nullptr && (ed->v.flags & FL_KILLME), "destructor detaches pool while edicts live");
}
}
int main()
{
    try { Lifecycle(); Failures(); }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "session-combination " << checks << " checks " << failures << " failures\n";
    return failures ? 1 : 0;
}
