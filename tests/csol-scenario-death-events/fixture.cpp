// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <cstring>

extern "C" void zbstrigger(entvars_t*);
namespace death_events_test
{
EventEngine::EventEngine()
{
    self = this;
    globals.maxClients = 4;
    globals.maxEntities = 65536;
    indices[&world] = 0;
    g_engfuncs.pfnCreateNamedEntity = CreateMixed;
    g_engfuncs.pfnLoadFileForMe = Load;
    g_engfuncs.pfnFreeFile = ReleaseFile;
    g_engfuncs.pfnPEntityOfEntIndex = QuerySlot;
    g_engfuncs.pfnIndexOfEdict = QueryIndex;
    g_engfuncs.pfnEntOffsetOfPEntity = QueryOffset;
    g_engfuncs.pfnFindEntityByString = Find;
    g_engfuncs.pfnRegUserMsg = Register;
    g_engfuncs.pfnMessageBegin = Begin;
    g_engfuncs.pfnWriteByte = Byte;
    g_engfuncs.pfnWriteShort = Short;
    g_engfuncs.pfnMessageEnd = End;
    g_engfuncs.pfnIsDedicatedServer = []() { return 1; };
    std::vector<std::string> header(59), row(59, "0");
    header[0] = ";map_id"; header[1] = "map_name"; header[2] = "map_filename"; header[58] = "SubType";
    row[0] = "1"; row[1] = "test"; row[2] = "map_session_test"; row[58] = "1";
    for (const auto& fields : {header, row}) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) files["resource/maplist.csv"] += ',';
            files["resource/maplist.csv"] += fields[i];
        }
        files["resource/maplist.csv"] += "\r\n";
    }
    files["maps/zbs_wave_map_session_test.cso"] = Hex(fixtures::waves);
    files["maps/zbs_map_session_test.cso"] = Hex(fixtures::rounds);
    files["maps/ZBS_ZT_map_session_test.cso"] = Hex(fixtures::types);
    files["maps/ZBS_TRG_map_session_test.cso"] = Hex(fixtures::triggers);
}
EventEngine::~EventEngine()
{
    onQuery = {}; onMessage = {}; findOverride = {};
    if (!allocations.empty() || packetOpen) std::abort();
    DisposeAll(); self = nullptr;
}
std::string EventEngine::Hex(std::string_view text)
{
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1])));
    return result;
}
unsigned char* EventEngine::Load(const char* name, int* length)
{
    Require(*length == 0, "engine file length initialized");
    const auto found = self->files.find(name);
    if (found == self->files.end()) return nullptr;
    auto* data = new unsigned char[found->second.size() + 1];
    std::memcpy(data, found->second.data(), found->second.size()); data[found->second.size()] = 0;
    *length = static_cast<int>(found->second.size()); self->allocations.insert(data); return data;
}
void EventEngine::ReleaseFile(void* memory)
{
    Require(self->allocations.erase(memory) == 1, "owned resource released once");
    delete[] static_cast<unsigned char*>(memory);
}
edict_t* EventEngine::CreateMixed(int name)
{ return std::strcmp(STRING(name), "monster_entity") == 0 ? CreatePoolEntity(name) : Engine::Create(name); }
int EventEngine::QueryIndex(const edict_t* ed)
{
    const int value = self->indexReplies.contains(ed) ? self->indexReplies.at(ed) : ed ? Engine::Index(ed) : 0;
    self->trace.push_back({'i', value});
    const auto callback = self->onQuery; if (callback) callback('i', value);
    return value;
}
int EventEngine::QueryOffset(const edict_t* ed)
{
    const int index = ed ? Engine::Index(ed) : 0;
    const int value = self->zeroOffsets.contains(index) ? 0 : index;
    self->trace.push_back({'o', index, value});
    const auto callback = self->onQuery; if (callback) callback('o', index);
    return value;
}
edict_t* EventEngine::QuerySlot(int slot)
{
    // Deliberately expose raw free/missing-private slots as well. Production
    // must retain its own checks even when an engine hook returns such a slot.
    auto* result = Engine::FromOffset(slot);
    self->trace.push_back({'l', slot});
    const auto callback = self->onQuery; if (callback) callback('l', slot);
    return result;
}
edict_t* EventEngine::Find(edict_t* previous, const char* field, const char* name)
{
    Require(std::strcmp(field, "targetname") == 0, "target query field");
    const int start = previous ? Engine::Index(previous) : 0;
    self->trace.push_back({'f', start, 0, name});
    if (self->findOverride) return self->findOverride(previous, name);
    edict_t* found = self->worldSentinel ? &self->world : nullptr;
    int best = std::numeric_limits<int>::max();
    for (const auto& [ed, index] : self->indices) {
        if (index <= start || index >= best || ed->free || !ed->v.targetname) continue;
        if (std::strcmp(STRING(ed->v.targetname), name) != 0) continue;
        best = index; found = const_cast<edict_t*>(ed);
    }
    const auto callback = self->onQuery; if (callback) callback('f', start);
    return found;
}
int EventEngine::Register(const char* name, int size)
{
    self->trace.push_back({'r', size, self->registeredId, name}); return self->registeredId;
}
void EventEngine::Hook(char kind)
{ const auto callback = onMessage; if (callback) callback(kind); }
void EventEngine::Begin(int destination, int message, const float* origin, edict_t* recipient)
{
    Require(!self->packetOpen, "no nested engine MessageBegin");
    const int slot = recipient ? Engine::Index(recipient) : 0;
    self->packetOpen = true;
    self->packets.push_back({destination, message, slot, origin});
    self->trace.push_back({'b', slot, message}); self->Hook('b');
}
void EventEngine::Byte(int value)
{
    Require(self->packetOpen, "byte belongs to open packet");
    self->packets.back().bytes.push_back(static_cast<std::uint8_t>(value));
    self->trace.push_back({'w', value}); self->Hook('w');
}
void EventEngine::Short(int value)
{
    Require(self->packetOpen, "short belongs to open packet");
    self->packets.back().bytes.push_back(static_cast<std::uint8_t>(value));
    self->packets.back().bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned>(value) >> 8));
    self->trace.push_back({'s', value}); self->Hook('s');
}
void EventEngine::End()
{
    Require(self->packetOpen, "end belongs to open packet");
    self->trace.push_back({'e'}); self->Hook('e');
    self->packets.back().complete = true; self->packetOpen = false;
}
edict_t* EventEngine::Raw(const char* classname, int index)
{
    Require(Engine::FromOffset(index) == nullptr, "fixture edict slot unique");
    auto ed = std::make_unique<edict_t>(); ed->serialnumber = 1; ed->v.pContainingEntity = ed.get();
    auto* raw = ed.get(); indices[raw] = index; edicts.push_back(std::move(ed));
    raw->v.classname = AllocString(classname); return raw;
}
CBasePlayer& EventEngine::Client(int index)
{
    auto* ed = Raw("player", index); auto* player = new(&ed->v) CBasePlayer;
    player->pev = &ed->v; player->m_pEntity = nullptr;
    ed->v.health = 100; ed->v.deadflag = DEAD_NO;
    privateOwners[ed] = [](void* p) { static_cast<CBasePlayer*>(p)->~CBasePlayer(); std::free(p); };
    return *player;
}
Target& EventEngine::AddTarget(int index, const char* name)
{
    auto* ed = Raw("test_death_target", index); auto* target = new(&ed->v) Target;
    target->pev = &ed->v; target->m_pEntity = nullptr; target->index = index; target->calls = &uses;
    ed->v.targetname = AllocString(name);
    privateOwners[ed] = [](void* p) { static_cast<Target*>(p)->~Target(); std::free(p); };
    return *target;
}
void EventEngine::Dispose(edict_t* ed, bool markFree)
{
    const auto found = privateOwners.find(ed);
    if (found != privateOwners.end()) {
        const auto destroy = found->second; privateOwners.erase(found);
        if (ed->pvPrivateData) destroy(ed->pvPrivateData);
        ed->pvPrivateData = nullptr;
    }
    if (markFree) { ++ed->serialnumber; ed->free = TRUE; }
}
void EventEngine::DisposeAll()
{ while (!privateOwners.empty()) Dispose(privateOwners.begin()->first); }
void EventEngine::Markers()
{
    Player(101, 1); Monster(201, 7, 1000);
    for (const int id : {1, 2, 30}) {
        auto* ed = Raw("zbstrigger", 300 + id); ed->v.model = AllocString("*1"); zbstrigger(&ed->v);
        auto* trigger = static_cast<CCSOLScenarioTrigger*>(ed->pvPrivateData); trigger->m_iId = id; trigger->Spawn();
        const float x = static_cast<float>(id * 100); ed->v.mins = Vector(x - 5, -5, -5); ed->v.maxs = Vector(x + 5, 5, 5);
    }
}
void EventEngine::ClearCapture()
{ Require(!packetOpen, "capture reset only between packets"); trace.clear(); packets.clear(); uses.clear(); }
std::vector<int> EventEngine::Queried(char kind) const
{ std::vector<int> result; for (const auto& row : trace) if (row.kind == kind) result.push_back(row.index); return result; }
std::vector<int> EventEngine::Recipients() const
{ std::vector<int> result; for (const auto& packet : packets) result.push_back(packet.slot); return result; }

Fixture::Fixture()
{
    engine.Markers();
    Require(map.BeginMap("map_session_test") && map.CollectEntities() && map.BeginRound(0, 0, 0, events), "real map begins with matched proxy layout");
    Require(pool.Initialize(), "real monster pool initialized");
    Require(messages.Register(), "real message registration"); engine.ClearCapture();
}
Fixture::~Fixture()
{
    engine.onQuery = {}; engine.onMessage = {}; engine.findOverride = {}; engine.onRemove = {};
    requests.Reset(); pool.Shutdown(); map.EndMap(); engine.DisposeAll(); engine.DestroyWorld(); pool.ForgetMap();
}
CCSOLScenarioMonster& Fixture::Monster(int priority, int id, int suffix)
{
    types.emplace_back(); auto& type = types.back(); type.id = id;
    type.priority = static_cast<std::int8_t>(priority); type.column19 = suffix;
    auto* result = pool.Acquire(type, 7, map.SpawnPoints().Points());
    Require(result != nullptr, "actual monster acquired"); return *result;
}
void Fixture::Reborrow(CCSOLScenarioMonster& monster)
{
    while (pool.AvailableCount()) {
        const int priority = pool.CategoryCount(0) < 39 ? 0 : pool.CategoryCount(1) < 4 ? 1 : 2;
        Monster(priority, static_cast<int>(types.size()) + 100);
    }
    const auto* type = monster.Type(); const auto lease = pool.Lease(monster);
    Require(pool.Free(monster), "free exact death slot before reuse");
    auto* replacement = pool.Acquire(*type, 7, map.SpawnPoints().Points());
    Require(replacement == &monster && pool.Lease(monster) != lease, "same raw entity and type receive different lease");
}
void Fixture::DestroyMapStorage()
{
    Require(requests.Reset(), "dispatcher reset before releasing map storage");
    Require(map.EndMap() && pool.Shutdown(), "map generation changes and pool detaches before release");
    engine.DisposeAll(); engine.DestroyWorld();
    Require(pool.ForgetMap(), "pool forgets released map without reading old handles");
}
}
