#pragma once
#include "goldcraft/world_objects.hpp"
struct edict_s;
void GoldCraft_ObjectsPrecache();
void GoldCraft_ObjectsReset(bool remove);
void GoldCraft_ObjectsUpdate(const goldcraft::WorldObjects& snapshot);
void GoldCraft_ObjectsExpire();
std::size_t GoldCraft_ObjectCount();
std::vector<std::pair<int,goldcraft::WorldObject>> GoldCraft_ObjectColliders();
std::uint64_t GoldCraft_ObjectKey(edict_s* entity);
bool GoldCraft_ObjectBlocksPenetration(edict_s* entity);
edict_s* GoldCraft_MobObject(std::uint64_t key);
bool GoldCraft_ObjectAction(std::uint64_t key,unsigned action,edict_s* attacker,float amount,unsigned bits,const float* source);
