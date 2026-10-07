#pragma once
struct edict_s;
void GoldCraft_ObjectsPrecache();
bool GoldCraft_ObjectBlocksPenetration(edict_s* entity);
void GoldCraft_ServerActivate(edict_s* edicts, int max_clients);
void GoldCraft_ServerDeactivate();
void GoldCraft_ClientConnect(edict_s* player);
void GoldCraft_ClientPutInServer(edict_s* player);
void GoldCraft_ClientDisconnect(edict_s* player);
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
