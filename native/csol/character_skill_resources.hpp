// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <span>

namespace csol::skillfx
{
inline constexpr std::array Models{
    "models/v_emotion0.mdl", "models/v_emotion1.mdl",
    "models/ef_buffclassskill_heal.mdl", "models/ef_buffclassskill_damage.mdl",
    "sprites/ef_buffclass20s2.spr", "sprites/ef_zbs73_explosion.spr",
    "models/ef_buffclass26-3_skill.mdl",
    "sprites/ef_buffclass24s3tr_skill.spr", "sprites/ef_buffclass24s3ct_skill.spr",
    "models/ef_meatwall_wave.mdl", "sprites/ef_buffcalss24s4_skill.spr",
    "sprites/ef_buffclass24s1tr_skill.spr", "models/ef_buffskill.mdl",
    "models/ef_scorpion_hole.mdl",
    "sprites/ef_buffclass23s3tr_skill01.spr", "sprites/ef_buffclass23s3ct_skill01.spr",
    "models/ef_y21s3buffclassct.mdl", "sprites/ef_sbmine_debuff.spr",
    "models/ef_buffclass22s4tr.mdl", "models/ef_buffclass22s4ct.mdl",
    "sprites/zombiarmorheal_head.spr", "sprites/zb_skill_hpbuff.spr",
    "sprites/zbt_invincibility.spr", "sprites/DoubleSideA.spr", "sprites/DoubleSideB.spr"};
// CL_LoadModel needs model indices for these world effects. Register the
// appended entries after the existing models so their indices stay stable.
inline constexpr auto ClientWorldSprites = std::span{Models}.last<5>();
// Recursive dependencies of Models, verified by csol_resources. These must
// reach clients before the corresponding model is first used.
inline constexpr std::array Dependencies{
    "gfx/tattoo/FemaleLong.tga", "gfx/tattoo/MaleLong.tga", "gfx/tattoo/tattoo.cso",
    "models/texture/#512512CSO_Girl_Hand_long.bmp",
    "models/texture/#512512CSO_HighQ_Hand__Long_512.bmp",
    "models/v_emotion11.seq", "models/v_emotion12.seq", "models/v_emotion13.seq",
    "sprites/muzzleflash317.spr", "sprites/muzzleflash344.spr", "sprites/muzzleflash345.spr",
    "sprites/muzzleflash352.spr", "sprites/muzzleflash383.spr", "sprites/muzzleflash468.spr",
    "sprites/muzzleflash469.spr", "sprites/muzzleflash539.spr", "sprites/muzzleflash563.spr",
    "models/$0a_buffclass26-3_flow3.tga", "models/ef_buffclass26-3_skill.json",
    "models/$0a_bufffiveseven_ef02.tga"};
inline constexpr std::array Sounds{
    "sound/player/buffclass19s1ct/buffclass19s1ct_useskill.wav",
    "sound/player/buffclass19s1tr/buffclass19s1tr_useskill.wav",
    "sound/zs_skill/zs_skill1.wav", "sound/zs_skill/buffclass22s2ct_zbs_skill.wav",
    "sound/weapons/gunkata_skill_last_exp.wav"};
inline constexpr std::array ClientResources{
    "sprites/csol/640hud12.spr",
    "resource/ZBSVER2/buffclassskill_heal.tga", "resource/ZBSVER2/buffclassskill_damage.tga",
    "resource/ZBSVER2/buffclassskill_buffup.tga", "resource/ZBSVER2/buffclassskill_shield.tga",
    "resource/ZBSVER2/buffclassskill_maxhealth.tga", "resource/ZBSVER2/buffclassskill_invincible.tga",
    "resource/zombi/directitem.tga", "resource/ZBSVER2/buffclassskill_damage_shield.tga",
    "resource/ZBSVER2/buffclassskill_damage_heal.tga", "resource/ZBSVER2/reflect.tga",
    "resource/ZBSVER2/reflect_damage.tga",
    "resource/Hud/ZBS/bossreinforce_bg_left.tga", "resource/Hud/ZBS/bossreinforce_bg_center.tga",
    "resource/Hud/ZBS/bossreinforce_bg_right.tga", "resource/helperHud/BossHP_bg_left_jun00.tga"};
inline constexpr std::array ServerTables{
    "classes2/emotion.csv", "resource/ModeEvent/modeevent.csv", "sound/sentences.json"};
}
