/*
*
*   This program is free software; you can redistribute it and/or modify it
*   under the terms of the GNU General Public License as published by the
*   Free Software Foundation; either version 2 of the License, or (at
*   your option) any later version.
*
*   This program is distributed in the hope that it will be useful, but
*   WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*   General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software Foundation,
*   Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*   In addition, as a special exception, the author gives permission to
*   link the code of this program with the Half-Life Game Engine ("HL
*   Engine") and Modified Game Libraries ("MODs") developed by Valve,
*   L.L.C ("Valve").  You must obey the GNU General Public License in all
*   respects for all of the code used other than the HL Engine and MODs
*   from Valve.  If you modify this file, you may extend this exception
*   to your version of the file, but you are not obligated to do so.  If
*   you do not wish to do so, delete this exception statement from your
*   version.
*
*/

// Included by player.cpp. Exact post-armor tails extracted from the matched ReGameDLL source.
// Validation, pre hooks, shield and native armor have already run once.
BOOL CBasePlayer::GoldCraft_CommitEquipmentDamage(entvars_t *pevInflictor, entvars_t *pevAttacker,
    float flDamage, int bitsDamageType, CBasePlayer *pAttack, BOOL bTeamAttack, int armorHit, bool explosion)
{
	BOOL bTookDamage;
	if (explosion)
	{
		m_lastDamageAmount = flDamage;

		if (pev->health > flDamage)
		{
			SetAnimation(PLAYER_FLINCH);
			Pain(m_LastHitGroup, false);
		}
		else
		{
			if (bitsDamageType & DMG_BLAST)
				m_bKilledByBomb = true;

			else if (bitsDamageType & DMG_EXPLOSION)
				m_bKilledByGrenade = true;
		}

		LogAttack(pAttack, this, bTeamAttack, int(flDamage), armorHit, pev->health - flDamage, pev->armorvalue, GetKillerWeaponName(pevInflictor, pevAttacker));
		bTookDamage = CBaseMonster::TakeDamage(pevInflictor, pevAttacker, int(flDamage), bitsDamageType);

		if (bTookDamage)
		{
			if (TheBots)
			{
				TheBots->OnEvent(EVENT_PLAYER_TOOK_DAMAGE, this, pAttack);
			}

			if (TheCareerTasks)
			{
				CBasePlayer *pPlayerAttacker = CBasePlayer::Instance(pevAttacker);
				if (pPlayerAttacker && !pPlayerAttacker->IsBot() && pPlayerAttacker->m_iTeam != m_iTeam)
				{
					TheCareerTasks->HandleEnemyInjury(GetKillerWeaponName(pevInflictor, pevAttacker), pPlayerAttacker->HasShield(), pPlayerAttacker);
				}
			}

#ifdef REGAMEDLL_API
			CSPlayer()->RecordDamage(pAttack, flDamage);
#endif
		}

		{
			// reset damage time countdown for each type of time based damage player just sustained
			for (int i = 0; i < ITBD_END; i++)
			{
				if (bitsDamageType & (DMG_PARALYZE << i))
					m_rgbTimeBasedDamage[i] = 0;
			}
		}

		// tell director about it
		MESSAGE_BEGIN(MSG_SPEC, SVC_DIRECTOR);
			WRITE_BYTE(9);								// command length in bytes
			WRITE_BYTE(DRC_CMD_EVENT);					// take damage event
			WRITE_SHORT(ENTINDEX(edict()));				// index number of primary entity
			WRITE_SHORT(ENTINDEX(ENT(pevInflictor)));	// index number of secondary entity
			WRITE_LONG(5);								// eventflags (priority and flags)
		MESSAGE_END();

		MESSAGE_BEGIN(MSG_SPEC, gmsgHLTV);
			WRITE_BYTE(ENTINDEX(edict()));
			WRITE_BYTE(int(Q_max(pev->health, 0.0f)) | DRC_FLAG_FACEPLAYER);
		MESSAGE_END();

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);

			if (!UTIL_IsValidPlayer(pPlayer))
				continue;

			if (pPlayer->m_hObserverTarget != this)
				continue;

			MESSAGE_BEGIN(MSG_ONE, gmsgSpecHealth, nullptr, pPlayer->edict());
				GoldCraft_WriteHealth(pPlayer->edict(), pev->health);
			MESSAGE_END();
		}

		return bTookDamage;
	}
	m_lastDamageAmount = flDamage;

	LogAttack(pAttack, this, bTeamAttack, flDamage, armorHit, pev->health - flDamage, pev->armorvalue, GetKillerWeaponName(pevInflictor, pevAttacker));

	// this cast to INT is critical!!! If a player ends up with 0.5 health, the engine will get that
	// as an int (zero) and think the player is dead! (this will incite a clientside screentilt, etc)
	bTookDamage = CBaseMonster::TakeDamage(pevInflictor, pevAttacker, int(flDamage), bitsDamageType);

	if (bTookDamage)
	{
		if (TheBots)
		{
			TheBots->OnEvent(EVENT_PLAYER_TOOK_DAMAGE, this, pAttack);
		}

		if (TheCareerTasks)
		{
			CBasePlayer *pPlayerAttacker = CBasePlayer::Instance(pevAttacker);
			if (pPlayerAttacker && !pPlayerAttacker->IsBot() && pPlayerAttacker->m_iTeam != m_iTeam)
			{
				TheCareerTasks->HandleEnemyInjury(GetKillerWeaponName(pevInflictor, pevAttacker), pPlayerAttacker->HasShield(), pPlayerAttacker);
			}
		}

#ifdef REGAMEDLL_API
		CSPlayer()->RecordDamage(pAttack, flDamage);
#endif
	}

	{
		// reset damage time countdown for each type of time based damage player just sustained
		for (int i = 0; i < ITBD_END; i++)
		{
			if (bitsDamageType & (DMG_PARALYZE << i))
				m_rgbTimeBasedDamage[i] = 0;
		}
	}

	// tell director about it
	MESSAGE_BEGIN(MSG_SPEC, SVC_DIRECTOR);
		WRITE_BYTE(9);								// command length in bytes
		WRITE_BYTE(DRC_CMD_EVENT);					// take damage event
		WRITE_SHORT(ENTINDEX(edict()));				// index number of primary entity
		WRITE_SHORT(ENTINDEX(ENT(pevInflictor)));	// index number of secondary entity
		WRITE_LONG(5);								// eventflags (priority and flags)
	MESSAGE_END();

	MESSAGE_BEGIN(MSG_SPEC, gmsgHLTV);
		WRITE_BYTE(ENTINDEX(edict()));
		WRITE_BYTE(int(Q_max(pev->health, 0.0f)) | DRC_FLAG_FACEPLAYER);
	MESSAGE_END();

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex(i);

		if (!UTIL_IsValidPlayer(pPlayer))
			continue;

		if (pPlayer->m_hObserverTarget == this)
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgSpecHealth, nullptr, pPlayer->edict());
				GoldCraft_WriteHealth(pPlayer->edict(), pev->health);
			MESSAGE_END();
		}
	}

#ifdef REGAMEDLL_FIXES
	if ((bitsDamageType & DMG_DROWN) && pev->waterlevel == 0) {
		bitsDamageType &= ~DMG_DROWN;
	}
#endif

	// Save this so we can report it to the client
	m_bitsHUDDamage = -1;

	// make sure the damage bits get resent
	m_bitsDamageType |= bitsDamageType;

	return bTookDamage;
}
