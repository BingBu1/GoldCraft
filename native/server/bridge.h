#pragma once
#include <cstdint>
#include <memory>
struct edict_s;
// One accepted native hit, shared with an outstanding absorption payment.
// A default scope masks an outer hit while reentrant player damage validates.
class GoldCraft_NativeHitScope {
public:
    struct State;
    GoldCraft_NativeHitScope();
    GoldCraft_NativeHitScope(edict_s* target,edict_s* inflictor,edict_s* attacker,int bits);
    explicit GoldCraft_NativeHitScope(std::shared_ptr<State> state);
    ~GoldCraft_NativeHitScope();
    GoldCraft_NativeHitScope(const GoldCraft_NativeHitScope&)=delete;
    GoldCraft_NativeHitScope& operator=(const GoldCraft_NativeHitScope&)=delete;
private:
    std::shared_ptr<State> previous_;
};
void GoldCraft_NativeDamageCommitted(edict_s* target,float amount);
void GoldCraft_InitCvars();
void GoldCraft_ObjectsPrecache();
bool GoldCraft_ObjectBlocksPenetration(edict_s* entity);
void GoldCraft_ServerActivate(edict_s* edicts, int max_clients);
void GoldCraft_ServerDeactivate();
void GoldCraft_ClientConnect(edict_s* player);
void GoldCraft_ClientPutInServer(edict_s* player);
void GoldCraft_ClientDisconnect(edict_s* player);
// Gates effects for the whole actual native lifecycle implementation,
// including reentrant Pawn callbacks. ReAPI pre/post timing is preserved.
class GoldCraft_PlayerEffectTransition {
public:
    GoldCraft_PlayerEffectTransition(edict_s* player, bool terminal) noexcept;
    ~GoldCraft_PlayerEffectTransition() noexcept;
    GoldCraft_PlayerEffectTransition(const GoldCraft_PlayerEffectTransition&) = delete;
    GoldCraft_PlayerEffectTransition& operator=(const GoldCraft_PlayerEffectTransition&) = delete;
private:
    std::uint64_t world_=0,generation_=0,slot_epoch_=0;
    std::uint32_t serial_=0;
    unsigned slot_=0;
    bool terminal_=false;
    int exceptions_=0;
};
void GoldCraft_PlayerSpawn(edict_s* player);
void GoldCraft_PlayerKilled(edict_s* player);
// True only during a final, already vanilla-armored MC health commit.
bool GoldCraft_FinalMinecraftDamage(edict_s* player);
bool GoldCraft_MinecraftAttack(edict_s* player);
// Falling belongs to vanilla only while its authenticated server owns movement.
bool GoldCraft_MinecraftFallAuthority(edict_s* player);
// Read-only effective healing cap; native class maximum remains authoritative.
float GoldCraft_StatusMaxHealth(edict_s* player,float native_maximum);
struct GoldCraft_FallAdjustment {float velocity, multiplier;};
GoldCraft_FallAdjustment GoldCraft_StatusFall(edict_s* player,float velocity,float gravity);
void GoldCraft_EntityTouched(edict_s* entity,edict_s* other);
bool GoldCraft_ClientCommand(edict_s* player, const char* command);
void GoldCraft_StartFrame();
// Read-only speed transform: never overwrites weapon/ZP base speed.
float GoldCraft_StatusMaxSpeed(edict_s* player,float native_speed);
// Native melee only, before TraceAttack/armor/plugin consumers; MC damage already owns attributes.
float GoldCraft_StatusMeleeDamage(edict_s* player,float native_damage);
// Native environment consumes server status, never HUD/client claims.
bool GoldCraft_StatusImmunity(edict_s* player,int bits);
bool GoldCraft_StatusBreathing(edict_s* player);
// Shield guard runs before ReAPI pre hooks, including contact infection.
bool GoldCraft_StatusShield(edict_s* player,edict_s* inflictor,float damage,int bits);
// Native armor has already run. MC-originated final damage is not reduced again.
float GoldCraft_StatusDefense(edict_s* player,float damage,edict_s* inflictor,edict_s* attacker,int bits);
struct entity_state_s;
// Packet rendering only; never changes native collision, traces or plugin entvars.
void GoldCraft_StatusVisibility(entity_state_s* state,edict_s* player);
// Only the authenticated glow lease can bypass PVS; other visibility rules stay.
bool GoldCraft_StatusGlowVisible(edict_s* player,edict_s* viewer);
struct playermove_s;
void GoldCraft_StatusCategorize(void (*original)());
void GoldCraft_StatusJump(void (*original)());
void GoldCraft_StatusWater(void (*original)());
void GoldCraft_FluidMove(playermove_s* move,int server,void (*original)(int));
// Queue after native armor, before the native injury/death tail.
bool GoldCraft_QueueNativeArmor(edict_s* player,float damage,edict_s* inflictor,edict_s* attacker,int bits,edict_s* logged_attacker,bool team_attack,int armor_hit);
const char* GoldCraft_EquipmentWeapon(edict_s* player);
// A nested native attack must not inherit a deferred outer hit context.
class GoldCraft_EquipmentDamageScope {
public:
    GoldCraft_EquipmentDamageScope() noexcept;
    ~GoldCraft_EquipmentDamageScope() noexcept;
    GoldCraft_EquipmentDamageScope(const GoldCraft_EquipmentDamageScope&)=delete;
    GoldCraft_EquipmentDamageScope& operator=(const GoldCraft_EquipmentDamageScope&)=delete;
private:
    edict_s* player_;float factor_;const char* weapon_;
};
// Scope native round cleanup; explicit plugin entity restarts retain their semantics.
struct GoldCraft_MapMiningRoundCleanup {
    GoldCraft_MapMiningRoundCleanup();
    ~GoldCraft_MapMiningRoundCleanup();
    GoldCraft_MapMiningRoundCleanup(const GoldCraft_MapMiningRoundCleanup&) = delete;
    GoldCraft_MapMiningRoundCleanup& operator=(const GoldCraft_MapMiningRoundCleanup&) = delete;
};
bool GoldCraft_MapMiningKeepEntity(edict_s* entity);
void GoldCraft_RoundCleanup();
// Invalidate ReGameDLL's one-second recent-PVS cache on an edit revision change.
bool GoldCraft_MapVisibilityChanged(int clientnum);
