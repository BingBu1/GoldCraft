#pragma once
#include <cstdint>
struct edict_s;
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
void GoldCraft_EntityTouched(edict_s* entity,edict_s* other);
bool GoldCraft_ClientCommand(edict_s* player, const char* command);
void GoldCraft_StartFrame();
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
