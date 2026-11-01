// SPDX-License-Identifier: GPL-3.0-or-later
// Real skill entity, player, weapon and weaponbox translation units. Hooks
// observe and callNext; no replacement DropPlayerItem/CreateWeaponBox body.
#include "trap_fixture.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
using namespace trap_test;
struct Packet
{
    int destination{}, id{}, recipient{};
    Vector origin{};
    std::vector<int> numbers;
    std::vector<float> coordinates;
    std::vector<std::string> strings;
    bool ended{};
};
struct Sound { int entity{}, channel{}, flags{}, pitch{}; std::string sample; float volume{}, attenuation{}; };

// Only the virtual capability under test differs; the actual native Drop
// implementation decides whether to reject this real M4A1 instance.
struct NoDropM4 final : CM4A1 { BOOL CanDrop() override { return FALSE; } };

struct DisarmFixture : Fixture
{
    inline static DisarmFixture* current{};
    std::vector<Packet> packets;
    std::vector<Sound> sounds;
    std::vector<std::string> order;
    std::function<void()> onEngineLottery, onMakeVectors, afterDamage;
    std::function<void(CBaseEntity*)> afterDrop;
    float lottery{50};
    int lotteryCalls{}, damageCalls{}, damageReturns{}, dropCalls{}, vectorCalls{};
    std::string dropName;
    EHANDLE dropped;
    bool observing{};

    explicit DisarmFixture(int restriction = 7, float resistance = 0, bool mapOverride = false)
        : Fixture(0, [=](auto& files)
        {
            std::ostringstream json;
            json << std::setprecision(9) << R"({"zbs_add_attribute":{"restrictWeaponDrop":{"default":{"Difficulty":)"
                 << restriction << '}';
            if (mapOverride) json << R"(,"exception0":{"mapId":[1],"Difficulty":1})";
            json << R"(}},"ZBS_ClueItem":{"Type":{"NONE":0,"DROP_WEAPON_RESIST":40},)"
                 << R"("OperatorType":{"RATIO":1,"MAX":2,"ADD":3},"ClueItems":[)"
                 << R"({"itemID":1,"type":"DROP_WEAPON_RESIST","value":)" << resistance
                 << R"(,"resourceName":"fixture","operatorType":"ADD"}]}})";
            files["maps/ZBS.json"] = json.str();
        })
    {
        Require(!current, "one disarm fixture"); current = this;
        Require(session->Difficulty().SelectedRow() == 1, "authored fixture selects physical difficulty row one");
        g_engfuncs.pfnRandomFloat = [](float low, float high)
        {
            auto& f = *current; ++f.lotteryCalls; f.order.emplace_back("engine-random");
            Check(low == 0 && high == 100, "original engine lottery uses 0..100");
            const float result = f.lottery; const auto callback = f.onEngineLottery;
            if (callback) callback(); return result;
        };
        // Other real SDK consumers may request integer RNG (e.g. pain sounds).
        // This controlled engine never consumes the C runtime RNG stream.
        g_engfuncs.pfnRandomLong = [](int32 low, int32) -> int32 { return low; };
        // Dedicated test client does not predict this native weapon animation.
        // Return false so the real SendWeaponAnim writes its complete packet.
        g_engfuncs.pfnCanSkipPlayer = [](const edict_t*) { return 0; };
        g_engfuncs.pfnMakeVectors = [](const float* angles)
        {
            auto& f = *current; ++f.vectorCalls;
            Require(angles[0] == 0 && angles[2] == 0, "fixture has explicit level yaw for engine vector service");
            const float yaw = angles[1] * (3.14159265358979323846f / 180.0f);
            gpGlobals->v_forward = Vector(std::cos(yaw), std::sin(yaw), 0);
            gpGlobals->v_right = Vector(std::sin(yaw), -std::cos(yaw), 0);
            gpGlobals->v_up = Vector(0, 0, 1);
            const auto callback = f.onMakeVectors; if (callback) callback();
        };
        g_engfuncs.pfnMessageBegin = [](int destination, int id, const float* origin, edict_t* recipient)
        {
            auto& f = *current;
            Require(f.packets.empty() || f.packets.back().ended, "engine messages do not nest");
            f.packets.push_back({destination, id, recipient ? Engine::Index(recipient) : 0,
                origin ? Vector(origin) : g_vecZero});
            f.order.emplace_back(id == gmsgShake ? "shake" : "message");
            const auto callback = f.onMessage; if (callback) callback(id);
        };
        g_engfuncs.pfnWriteByte = g_engfuncs.pfnWriteChar = g_engfuncs.pfnWriteShort =
            g_engfuncs.pfnWriteLong = g_engfuncs.pfnWriteEntity = [](int value)
        { Require(!current->packets.empty(), "scalar belongs to a packet"); current->packets.back().numbers.push_back(value); };
        g_engfuncs.pfnWriteCoord = g_engfuncs.pfnWriteAngle = [](float value)
        { Require(!current->packets.empty(), "coordinate belongs to a packet"); current->packets.back().coordinates.push_back(value); };
        g_engfuncs.pfnWriteString = [](const char* value)
        { Require(!current->packets.empty(), "string belongs to a packet"); current->packets.back().strings.emplace_back(value); };
        g_engfuncs.pfnMessageEnd = []
        { Require(!current->packets.empty() && !current->packets.back().ended, "one packet end"); current->packets.back().ended = true; };
        g_engfuncs.pfnEmitSound = [](edict_t* ed, int channel, const char* sample, float volume, float attenuation, int flags, int pitch)
        {
            auto& f = *current;
            f.sounds.push_back({Engine::Index(ed), channel, flags, pitch, sample, volume, attenuation});
            if (!std::strcmp(sample, "zombi/deimos_skill_hit.wav")) f.order.emplace_back("hit-sound");
            const auto callback = f.engine.onSound; if (callback) callback(ed, sample);
        };
        g_ReGameHookchains.m_CBasePlayer_TakeDamage.registerHook(Damage, 193);
        g_ReGameHookchains.m_CBasePlayer_DropPlayerItem.registerHook(Drop, 193);
        observing = true;
        ammodrop.value = 2;
    }
    static BOOL Damage(IReGameHook_CBasePlayer_TakeDamage* chain, CBasePlayer* player,
        entvars_t* inflictor, entvars_t* attacker, float& amount, int bits)
    {
        auto& f = *current; ++f.damageCalls; f.order.emplace_back("zero-damage");
        Check(inflictor == player->pev && attacker == player->pev && amount == 0 && bits == DMG_BULLET,
            "actual TakeDamage receives struck player twice zero and DMG_BULLET");
        const BOOL result = chain->callNext(player, inflictor, attacker, amount, bits);
        ++f.damageReturns;
        const auto callback = f.afterDamage; if (callback) callback(); return result;
    }
    static CBaseEntity* Drop(IReGameHook_CBasePlayer_DropPlayerItem* chain, CBasePlayer* player, const char* name)
    {
        auto& f = *current; ++f.dropCalls; f.dropName = name; f.order.emplace_back("native-drop");
        auto* result = chain->callNext(player, name);
        if (result) f.dropped = result;
        const auto callback = f.afterDrop; if (callback) callback(result); return result;
    }
    CBasePlayer& Victim()
    {
        auto& player = Player(); player.pev->angles = Vector(0, 90, 0);
        player.pev->origin = Vector(100, 200, 300);
        player.pev->takedamage = DAMAGE_AIM;
        csol::scenario::EquipmentLoadout loadout; loadout.classModsComplete = true; loadout.carriedItems = {1};
        Require(session->EquipmentState()->BindPlayer(player, 0, std::move(loadout)), "actual equipment service binds resistance item");
        return player;
    }
    CBasePlayerWeapon& Weapon(CBasePlayer& player, const char* name, int ammo = 37, bool cannotDrop = false)
    {
        edict_t* ed{};
        if (cannotDrop)
        {
            ed = engine.Reserve(engine.nextEntity++, name);
            GetClassPtr<CCSM4A1>(reinterpret_cast<NoDropM4*>(&ed->v));
        }
        else ed = Engine::Create(Engine::String(name));
        Require(engine.api.pfnSpawn(ed) == 0, "weapon uses real Spawn and ItemInfo");
        auto* weapon = dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));
        Require(weapon && !player.m_rgpPlayerItems[weapon->iItemSlot()], "fresh real weapon inventory slot");
        const int ammoIndex = weapon->iItemSlot() == PRIMARY_WEAPON_SLOT ? 4 : 6;
        weapon->m_iPrimaryAmmoType = ammoIndex; weapon->m_iSecondaryAmmoType = -1; weapon->m_iClip = 0;
        weapon->m_pPlayer = &player; weapon->m_pNext = nullptr;
        player.m_rgpPlayerItems[weapon->iItemSlot()] = weapon;
        player.m_rgAmmo[ammoIndex] = ammo;
        // Inventory attachment is fixture setup. Native removal, packing,
        // selection and CanDrop still run, including zero-ammo alternatives.
        return *weapon;
    }
    EHANDLE Projectile()
    {
        auto call = CSOLMonsterSpawnCall(session->Map(), session->Monsters(), deaths, *monster);
        const int begin = engine.nextEntity; bool created{}; ConfigError error;
        Require(CSOL_CreateMonsterDisarm(call, runtime, Vector(11, 22, 33), Vector(0, 1, 0), created, error) && created,
            "actual factory creates a disarm projectile");
        for (int i = begin; i < engine.nextEntity; ++i)
            if (auto* entity = CBaseEntity::Instance(Engine::At(i)); entity && FClassnameIs(entity->pev, "deimostail"))
                return EHANDLE(entity);
        throw std::runtime_error("factory succeeded without real deimostail entity");
    }
    std::vector<const Packet*> Effects() const
    {
        std::vector<const Packet*> result;
        const int id = engine.messages.at(csol::monster_effects::Message);
        for (const auto& packet : packets) if (packet.id == id) result.push_back(&packet);
        return result;
    }
    void Retired(const EHANDLE& handle)
    {
        auto* entity = handle.Get<CBaseEntity>();
        Check(!entity || ((entity->pev->flags & FL_KILLME) && !entity->m_pfnThink && !entity->m_pfnTouch && !entity->pev->nextthink),
            "retired projectile has no remaining engine Think or Touch");
        if (entity)
        {
            const auto before = packets.size(); engine.api.pfnThink(entity->edict());
            Check(packets.size() == before, "subsequent real DispatchThink cannot replay retired impact");
        }
    }
    ~DisarmFixture()
    {
        afterDamage = {}; afterDrop = {}; onEngineLottery = {}; onMakeVectors = {};
        onMessage = {}; engine.onSound = {};
        if (observing)
        {
            g_ReGameHookchains.m_CBasePlayer_TakeDamage.unregisterHook(Damage);
            g_ReGameHookchains.m_CBasePlayer_DropPlayerItem.unregisterHook(Drop);
        }
        // Clear while packet/sound storage and derived callbacks still exist.
        engine.Clear(); current = nullptr;
    }
};

unsigned SeedFor(bool zero)
{
    for (unsigned seed = 0; seed < 1000000; ++seed)
    { std::srand(seed); if ((std::rand() == 0) == zero) return seed; }
    throw std::runtime_error("real CRT did not supply requested deterministic seed");
}
void CheckImpact(DisarmFixture& f)
{
    const auto effects = f.Effects();
    Check(effects.size() == 1, "one terminal impact packet");
    if (!effects.empty())
        Check(effects[0]->destination == MSG_PVS && effects[0]->numbers == std::vector<int>{csol::monster_effects::Impact} &&
            effects[0]->coordinates == std::vector<float>{11, 22, 13} && effects[0]->ended,
            "terminal impact preserves PVS origin and original twenty-unit downward offset");
}
void Flight()
{
    for (int collision = 0; collision < 3; ++collision)
    {
        DisarmFixture f; auto& player = f.Victim(); const auto shot = f.Projectile();
        auto* entity = shot.Get<CBaseEntity>();
        Check(entity->pev->origin == Vector(11,22,33) && entity->pev->velocity == Vector(0,1200,0) &&
            entity->pev->movetype == MOVETYPE_FLY && entity->pev->solid == SOLID_BBOX &&
            entity->pev->mins == g_vecZero && entity->pev->maxs == g_vecZero && entity->pev->nextthink == 10.5f,
            "actual projectile has point collision velocity1200 and half-second deadline");
        const auto trail = std::find_if(f.packets.begin(),f.packets.end(),[](const auto& p){return p.id == SVC_TEMPENTITY;});
        Check(trail != f.packets.end() && trail->destination == MSG_BROADCAST && trail->numbers.size() == 9 &&
            trail->numbers.front() == TE_BEAMFOLLOW && trail->numbers[1] == entity->entindex() &&
            std::vector<int>(trail->numbers.begin()+3,trail->numbers.end()) == std::vector<int>{20,1,250,200,0,255},
            "actual spawn emits original beam-follow trail parameters");
        int reentry{};
        f.onMessage = [&](int id)
        {
            if (id != f.engine.messages.at(csol::monster_effects::Message)) return;
            ++reentry;
            f.engine.api.pfnThink(entity->edict());
            f.Touch(shot, player);
        };
        if (collision == 0) { f.engine.globals.time = 10.5f; f.engine.api.pfnThink(entity->edict()); }
        if (collision == 1)
        {
            auto* world = Engine::At(0); world->v.classname = Engine::String("worldspawn");
            GetClassPtr<CCSEntity>(reinterpret_cast<CWorld*>(&world->v));
            f.engine.api.pfnTouch(entity->edict(), world);
        }
        if (collision == 2) { player.pev->deadflag = DEAD_DEAD; f.Touch(shot, player); }
        Check(f.damageCalls == 0 && f.lotteryCalls == 0 && f.dropCalls == 0,
            "timeout world and dead-player collisions never execute human disarm lottery");
        Check(reentry == 1, "impact callback reentry cannot send another terminal packet or touch a human");
        CheckImpact(f); f.Retired(shot);
    }
}
void LotteryAndNativeDrop()
{
    const unsigned seed = SeedFor(false);
    for (const float choice : {49.999f, 50.0f, 50.001f})
    {
        DisarmFixture f; auto& player = f.Victim(); auto& weapon = f.Weapon(player,"weapon_m4a1");
        const EHANDLE item(&weapon); const auto shot = f.Projectile(); f.lottery = choice;
        std::srand(seed); const int first = std::rand(), second = std::rand(); std::srand(seed);
        f.Touch(shot, player); const bool attempt = choice <= 50;
        Check(f.damageCalls == 1 && f.damageReturns == 1 && player.pev->health == 100,
            "native zero-damage call executes and returns without health loss");
        Check(f.lotteryCalls == 1 && f.dropCalls == int(attempt) && std::rand() == (attempt ? second : first),
            "50 equality attempts disarm and only accepted engine gate consumes one separate CRT draw");
        const auto hit = std::find_if(f.sounds.begin(),f.sounds.end(),[](const auto& s){return s.sample == "zombi/deimos_skill_hit.wav";});
        Check(hit != f.sounds.end() && hit->entity == 1 && hit->channel == CHAN_BODY && hit->volume == 1 &&
            hit->attenuation == .8f && !hit->flags && hit->pitch == 100, "human hit sound uses original recipient and audio parameters");
        const auto shake = std::find_if(f.packets.begin(),f.packets.end(),[](const auto& p){return p.id == gmsgShake;});
        Check(shake != f.packets.end() && shake->destination == MSG_ONE && shake->recipient == 1 &&
            shake->numbers == std::vector<int>{0xA000,0x2000,0x6400}, "original shake packet reaches the struck player");
        const auto where = [&](const char* name){return std::find(f.order.begin(),f.order.end(),name)-f.order.begin();};
        Check(where("hit-sound") < where("zero-damage") && where("zero-damage") < where("shake") &&
            where("shake") < where("engine-random"), "sound zero-damage shake and engine lottery retain original order");
        if (attempt)
        {
            auto* box = dynamic_cast<CWeaponBox*>(f.dropped.Get<CBaseEntity>());
            Require(box != nullptr, "DropPlayerItem returned an actual live native weaponbox");
            Check(f.dropName == "weapon_m4a1" && !player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] &&
                box->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] == item.Get<CBaseEntity>() && !weapon.m_pPlayer &&
                weapon.pev->owner == box->edict(), "real PackWeapon removes exact inventory item and transfers owner");
            const int index = box->GetAmmoIndex("556Nato");
            Check(index > 0 && box->m_rgAmmo[index] == 37 && player.m_rgAmmo[4] == 0,
                "native ammo-drop policy transfers all thirty-seven reserve rounds exactly once");
            Check(std::fabs(box->pev->velocity.x) < .001f && std::fabs(box->pev->velocity.y - 600) < .001f &&
                box->pev->velocity.z == 0 && box->pev->owner == player.edict(), "actual weaponbox gets player-forward speed600 after native drop");
        }
        else Check(player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] == &weapon && player.m_rgAmmo[4] == 37,
            "rejected engine lottery leaves inventory and reserve ammo intact");
        CheckImpact(f); f.Retired(shot);
    }
}
void SelectionAndCanDrop()
{
    for (int kind = 0; kind < 5; ++kind)
    {
        DisarmFixture f; auto& player = f.Victim(); CBasePlayerWeapon* primary{};
        if (kind != 1 && kind != 4) primary = &f.Weapon(player,"weapon_m4a1",37,kind == 2);
        CBasePlayerWeapon* pistol = kind != 4 ? &f.Weapon(player,"weapon_usp",0) : nullptr;
        // An empty pistol cannot auto-deploy; the selected primary is still
        // the target even though it is not the active item.
        if (kind == 0) player.m_pActiveItem = pistol;
        if (kind == 3) player.m_bIsVIP = true;
        const auto shot = f.Projectile(); std::srand(SeedFor(false)); f.Touch(shot,player);
        const bool rejected = kind == 2 || kind == 3;
        Check(f.dropCalls == int(kind != 4), "only one native Drop call is made for selected inventory head");
        if (kind != 4) Check(f.dropName == (kind == 1 ? "weapon_usp" : "weapon_m4a1"),
            "primary precedes pistol independently of current active item");
        if (rejected) Check(!f.dropped.Get() && player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] == primary &&
            player.m_rgpPlayerItems[PISTOL_SLOT] == pistol, "native CanDrop or VIP refusal never falls through to pistol");
        if (kind == 1) Check(f.dropped.Get() && !player.m_rgpPlayerItems[PISTOL_SLOT], "pistol drops when primary is absent");
        CheckImpact(f); f.Retired(shot);
    }
}
void RestrictionAndResistance()
{
    const unsigned nonzero = SeedFor(false), zero = SeedFor(true);
    std::srand(nonzero); const float equality = float(std::rand()) * (1.0f / 32767.0f);
    for (int kind = 0; kind < 7; ++kind)
    {
        const bool restricted = kind == 0 || kind == 1;
        const float resistance = kind == 2 ? 1.0f : kind == 3 ? equality : 0.0f;
        DisarmFixture f(kind == 0 ? 1 : 7, resistance,kind == 1);
        auto& player = f.Victim(); auto& weapon = f.Weapon(player,"weapon_m4a1");
        if (kind == 5 || kind == 6)
        {
            CSOLScenarioStatBuff buff; buff.forbidWeaponDrop = true; buff.expires = kind == 5 ? 11.0f : 10.0f;
            Require(f.players->InstallStatBuff(EHANDLE(&player),buff) != 0,"real player runtime installs weapon-drop protection");
        }
        const auto shot = f.Projectile(); const unsigned seed = kind == 4 ? zero : nonzero;
        std::srand(seed); const int first = std::rand(),second = std::rand(); std::srand(seed);
        f.Touch(shot,player);
        Check(std::rand() == (restricted ? first : second),"difficulty gates precede CRT lottery while resistance and buff checks consume it");
        const bool drops = kind == 6;
        Check(f.dropCalls == int(drops),"difficulty resistance equality rand-zero and active-buff gates preserve original comparisons");
        if (!drops) Check(player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] == &weapon && player.m_rgAmmo[4] == 37,
            "denied disarm preserves exact native inventory and ammo");
        bool resisted{};
        for (const auto& packet : f.packets) for (const auto& text : packet.strings)
            if (text == "#CSO_ZBSVER2_YouResistedWeaponDrop") resisted = true;
        Check(resisted == (kind >= 2 && kind <= 4), "resistance denial alone emits the original resist token");
        CheckImpact(f); f.Retired(shot);
    }
}
void Cancellation()
{
    for (int edge = 0; edge < 4; ++edge) for (int action = 0; action < 5; ++action)
    {
        DisarmFixture f; auto& player = f.Victim(); auto& weapon = f.Weapon(player,"weapon_m4a1");
        const EHANDLE handle(&player), item(&weapon); const auto shot = f.Projectile();
        const auto oldPlayers = f.runtime->players; const auto oldSession = f.runtime->session;
        bool cancelled{};
        const auto cancel = [&]
        {
            if (cancelled) return; cancelled = true;
            if (action == 0) Require(f.players->ResetMonsterSkillState(handle),"actual life reset in external callback");
            if (action == 1) f.runtime->players = std::make_shared<CCSOLScenarioPlayerRuntime>(f.session);
            if (action == 2) f.runtime->session = std::make_shared<CCSOLScenarioSession>();
            if (action == 3) f.Destroy(shot);
            if (action == 4) Engine::Remove(handle.Get());
        };
        if (edge == 0) f.engine.onSound = [&](edict_t*,const char* name){if(!std::strcmp(name,"zombi/deimos_skill_hit.wav"))cancel();};
        if (edge == 1) f.afterDamage = cancel;
        if (edge == 2) f.onMessage = [&](int id){if(id == gmsgShake)cancel();};
        if (edge == 3) f.onEngineLottery = cancel;
        std::srand(SeedFor(false)); f.Touch(shot,player);
        Check(cancelled && f.dropCalls == 0, "callback invalidation cannot reach native Drop");
        if (action == 4) Check(!handle.Get(), "raw player deletion is detected before later player dereference");
        else Check(player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] == item.Get<CBaseEntity>() && player.m_rgAmmo[4] == 37,
            "callback invalidation cannot alter replacement-life inventory or ammo");
        Check(f.damageCalls == int(edge > 0) && f.lotteryCalls == int(edge == 3) && f.Effects().empty(),
            "cancelled callback stops all later old-life disarm stages and impact");
        f.runtime->players = oldPlayers; f.runtime->session = oldSession;
        f.Retired(shot);
    }
}
void DropCallbackCancellation()
{
    for (int action = 0; action < 4; ++action)
    {
        DisarmFixture f; auto& player = f.Victim(); f.Weapon(player,"weapon_m4a1");
        const EHANDLE handle(&player); const auto shot = f.Projectile();
        f.afterDrop = [&](CBaseEntity* result)
        {
            Require(result != nullptr,"actual Drop completed before callback invalidation");
            if (action == 0) Require(f.players->ResetMonsterSkillState(handle),"Drop post hook advances player life");
            if (action == 1) Engine::Remove(result->edict());
            if (action == 2) result->pev->flags |= FL_KILLME;
            if (action == 3) f.onMakeVectors = [&]
            {
                f.onMakeVectors = {};
                Require(f.players->ResetMonsterSkillState(handle),"post-Drop vector callback advances player life");
            };
        };
        std::srand(SeedFor(false)); f.Touch(shot,player);
        Check(f.dropCalls == 1 && !player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT],"completed native Drop is never repeated by cancellation tail");
        if (action == 0 || action == 3)
        {
            auto* box = f.dropped.Get<CBaseEntity>();
            Check(box && std::fabs(box->pev->velocity.y - 400) < .001f && f.Effects().empty(),
                "new life after actual Drop prevents stale speed600 write and terminal effect");
        }
        if (action == 1) Check(!f.dropped.Get(),"freed native Drop return is rejected before RTTI or field access");
        if (action == 2) Check(std::fabs(f.dropped.Get<CBaseEntity>()->pev->velocity.y - 400) < .001f,
            "pending-deletion weaponbox retains native velocity without stale override");
        f.Retired(shot);
    }
}
void ModeLifetime()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        DisarmFixture f; auto& player = f.Victim(); f.Weapon(player,"weapon_m4a1"); const auto shot = f.Projectile();
        const auto access = f.session->Map().EntityAccess();
        // The mode service stages the next map; it is not a close operation.
        // Invoke the actual scenario rules' server-deactivation virtual first.
        g_pGameRules->ServerDeactivate();
        Check(f.runtime->Closing() && !access->Allowed(), "real ServerDeactivate closes runtime and revokes old entity access");
        f.engine.Modes().EndMap(); f.engine.Prepare(mode == 0 ? "0" : "1");
        f.Touch(shot,player);
        Check(f.damageCalls == 0 && f.lotteryCalls == 0 && f.dropCalls == 0 && f.Effects().empty(),
            "new Native or ZP map generation cannot execute old scenario projectile");
    }
}
}

int main(int argc,char** argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "all";
        if(group == "all" || group == "flight")Flight();
        if(group == "all" || group == "drop")LotteryAndNativeDrop();
        if(group == "all" || group == "selection")SelectionAndCanDrop();
        if(group == "all" || group == "resistance")RestrictionAndResistance();
        if(group == "all" || group == "cancel")Cancellation();
        if(group == "all" || group == "drop-cancel")DropCallbackCancellation();
        if(group == "all" || group == "mode")ModeLifetime();
    }
    catch(const std::exception& error){++sdk_test::failures;std::cerr << "EXCEPTION " << error.what() << '\n';}
    std::cout << "monster-disarm-sdk " << sdk_test::checks << " checks " << sdk_test::failures << " failures\n";
    return sdk_test::failures ? 1 : 0;
}
