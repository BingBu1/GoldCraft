// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated server fixture; excluded from normal builds. All weapon behavior
// still runs through CTKnifeEx2 and the ordinary inventory/physics paths.
#include "precompiled.h"
#include "weapons.h"
#include "wpn_beamgun.h"
#include "../beamgun_prediction.hpp"

namespace
{
constexpr char PlayerModel[] = "models/player/csol_test/csol_test.mdl";

void BeamFixture()
{
    const char* action=CMD_ARGV(1);
    for(int index=1;index<=gpGlobals->maxClients;++index)
    {
        auto* player=static_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
        if(!player||player->IsDormant()||(player->pev->flags&FL_FAKECLIENT))continue;
        if(!Q_strcmp(action,"give"))
        {
            if(!CSOL_WeaponsReady())continue;
            if(!player->IsAlive())player->RoundRespawn();
            player->DropPrimary();
            const char* name=CMD_ARGC()>2&&!Q_strcmp(CMD_ARGV(2),"beamgun")?"weapon_beamgun":"weapon_beamgunle";
            auto* item=player->GiveNamedItemEx(name);
            if(item)player->SelectItem(name);
            SERVER_PRINT(UTIL_VarArgs("[Beam] give player=%d entity=%d weapon=%s\n",index,item?item->entindex():0,name));
        }
        auto* active=player->m_pActiveItem;
        auto* gun=active?static_cast<CBasePlayerWeapon*>(active->GetWeaponPtr()):nullptr;
        if(!CSOL_IsWeapon(gun)||!csol::BeamWeaponId(gun->m_iId))continue;
        if(!Q_strcmp(action,"ammo")&&CMD_ARGC()==4)
        {
            const int clip=Q_atoi(CMD_ARGV(2)),reserve=Q_atoi(CMD_ARGV(3));
            if(clip<0||clip>80||reserve<0||reserve>200)continue;
            gun->m_iClip=clip;player->m_rgAmmo[gun->m_iPrimaryAmmoType]=reserve;gun->m_fInReload=FALSE;
        }
        else if(!Q_strcmp(action,"energy")&&CMD_ARGC()==3)
        {
            const int energy=Q_atoi(CMD_ARGV(2));if(energy<0||energy>100)continue;
            player->m_rgAmmo[gun->m_iSecondaryAmmoType]=energy;
        }
        if(!Q_strcmp(action,"status"))
        {
            weapon_data_t data{};CSOL_PackBeamWeapon(gun,&data);const auto state=csol::ReadBeamPrediction(data);
            SERVER_PRINT(UTIL_VarArgs("[Beam] player=%d alive=%d id=%d clip=%d reserve=%d energy=%d charge=%d time=%.3f dive=%d pending=%d reload=%d\n",
                index,player->IsAlive(),gun->m_iId,state.clip,player->m_rgAmmo[gun->m_iPrimaryAmmoType],state.energy,
                int(state.charge),state.chargeTime,int(state.dive),int(state.pending),state.reloading));
        }
    }
}

void Fixture()
{
    const char* action = CMD_ARGV(1);
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* player = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
        if (!player || player->IsDormant() || (player->pev->flags & FL_FAKECLIENT)) continue;
        if (!Q_strcmp(action, "give"))
        {
            if (!CSOL_WeaponsReady())
            {
                SERVER_PRINT("[TKnife] unavailable: enable csol_weapons and reload the map before giving weapons.\n");
                continue;
            }
            if (!player->IsAlive()) player->RoundRespawn();
            player->DropSecondary();
            auto* item = player->GiveNamedItemEx("weapon_tknifeex2");
            if (item)
            {
                player->SelectItem("weapon_tknifeex2");
            }
            SERVER_PRINT(UTIL_VarArgs("[TKnife] give player=%d entity=%d\n", index, item ? item->entindex() : 0));
        }
        else if (!Q_strcmp(action, "model"))
        {
            player->CSPlayer()->SetPlayerModelEx("csol_test");
            player->SetClientUserInfoModel(GET_INFO_BUFFER(player->edict()), "csol_test");
            player->SetNewPlayerModel(PlayerModel);
        }
        else if (!Q_strcmp(action, "ammo") && CMD_ARGC() == 3)
        {
            auto* weapon = static_cast<CBasePlayerWeapon*>(player->m_rgpPlayerItems[PISTOL_SLOT]);
            const int total = Q_atoi(CMD_ARGV(2));
            if (!CSOL_IsWeapon(weapon) || total < 0 || total > weapon->iMaxAmmo1() + weapon->iMaxClip()) continue;
            weapon->m_iClip = Q_min(total, weapon->iMaxClip());
            player->m_rgAmmo[weapon->m_iPrimaryAmmoType] = total - weapon->m_iClip;
            weapon->m_fInReload = FALSE;
        }
        else if (!Q_strcmp(action, "pickup"))
        {
            CBaseEntity* entity = nullptr;
            while ((entity = UTIL_FindEntityByClassname(entity, "weaponbox")))
            {
                auto* box = static_cast<CWeaponBox*>(entity);
                auto* item = box->m_rgpPlayerItems[PISTOL_SLOT];
                if (!CSOL_WorldModel(item)) continue;
                // Move the player, then let the real server Touch handle pickup.
                UTIL_SetOrigin(player->pev, box->pev->origin + Vector(0, 0, 24));
                player->pev->velocity = g_vecZero;
                break;
            }
        }
        else if (!Q_strcmp(action, "status"))
        {
            auto* active = player->m_pActiveItem;
            auto* weapon = active ? static_cast<CBasePlayerWeapon*>(active->GetWeaponPtr()) : nullptr;
            const auto* header = static_cast<studiohdr_t*>(GET_MODEL_PTR(player->edict()));
            weapon_data_t data[MAX_WEAPONS]{};
            GetWeaponData(player->edict(), data);
            SERVER_PRINT(UTIL_VarArgs("[TKnife] player=%d alive=%d weapon=%s slot=%d clip=%d reserve=%d wireClip=%d sequence=%d gait=%d sequences=%d owned=%d\n",
                index, player->IsAlive(), active ? STRING(active->pev->classname) : "none",
                active ? active->iItemSlot() : -1, weapon ? weapon->m_iClip : -1,
                weapon && weapon->m_iPrimaryAmmoType >= 0 ? player->m_rgAmmo[weapon->m_iPrimaryAmmoType] : 0,
                data[0].m_iClip, player->pev->sequence, player->pev->gaitsequence, header ? header->numseq : 0,
                player->GetItemById(static_cast<WeaponIdType>(CSOL_WEAPON_TKNIFEEX2)) != nullptr));
        }
    }
    if (!Q_strcmp(action, "status"))
    {
        CBaseEntity* entity = nullptr;
        while ((entity = UTIL_FindEntityByClassname(entity, "weaponbox")))
        {
            auto* box = static_cast<CWeaponBox*>(entity);
            auto* item = box->m_rgpPlayerItems[PISTOL_SLOT];
            if (!CSOL_WorldModel(item)) continue;
            SERVER_PRINT(UTIL_VarArgs("[TKnife] box=%d model=%s slot=%d clip=%d onground=%d\n", box->entindex(),
                STRING(box->pev->model), item->iItemSlot(), static_cast<CBasePlayerWeapon*>(item)->m_iClip,
                (box->pev->flags & FL_ONGROUND) != 0));
        }
    }
}
}

void CSOL_InitWeaponTests() { ADD_SERVER_COMMAND("tknife_fixture", Fixture); ADD_SERVER_COMMAND("beam_fixture", BeamFixture); CSOL_InitBotTests(); }
void CSOL_PrecacheWeaponTests() { PRECACHE_MODEL(const_cast<char*>(PlayerModel)); CSOL_ResetBotTests(); }
