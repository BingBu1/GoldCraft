// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
inline constexpr char CSOLOberonBombModel[] = "models/zbs_bossL_big02_bomb.mdl";
inline constexpr char CSOLOberonExplosionSprite[] = "sprites/eexplo.spr";
inline constexpr char CSOLOberonDebrisModel[] = "models/concrete_gibs.mdl";
inline constexpr char CSOLOberonBounceSound1[] = "zombi/Zombi_Bomb_Bounce_1.wav";
inline constexpr char CSOLOberonBounceSound2[] = "zombi/Zombi_Bomb_Bounce_2.wav";
inline constexpr std::array CSOLOberonBombSounds{
    CSOLOberonBounceSound1, CSOLOberonBounceSound2};
inline constexpr char CSOLOberonStunSound[] = "zombi/TD_Stun.wav";
// HUD_StudioEvent 5008 selects these samples from its numeric option. They
// are not literal WAV paths in the model's animation-event table.
inline constexpr char CSOLOberonFootstepSound1[] = "zombi/boss_footstep_1.wav";
inline constexpr char CSOLOberonFootstepSound2[] = "zombi/boss_footstep_2.wav";
inline constexpr char CSOLOberonKnifeWalkSound1[] = "zombi/oberon/knife_walk1.wav";
inline constexpr char CSOLOberonKnifeWalkSound2[] = "zombi/oberon/knife_walk2.wav";

// SK3/SK5 debris and SK7 projectiles are not appearance models. Register
// their resources during map loading, before either attack can call
// MODEL_INDEX/SET_MODEL. The client-only stun sound must be downloadable too.
inline constexpr std::array CSOLOberonAttackModels{
    CSOLOberonBombModel, CSOLOberonExplosionSprite, CSOLOberonDebrisModel};
inline constexpr std::array CSOLOberonAttackSounds{
    CSOLOberonFootstepSound1, CSOLOberonFootstepSound2,
    CSOLOberonKnifeWalkSound1, CSOLOberonKnifeWalkSound2,
    CSOLOberonBounceSound1, CSOLOberonBounceSound2, CSOLOberonStunSound};
