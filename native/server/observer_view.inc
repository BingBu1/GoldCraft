// A dead actor's transform is a presentation anchor. ReGameDLL validates and
// selects m_hObserverTarget; do not accept a client-supplied tracking position.
const entvars_t* ActorPresentationView(CBasePlayer* player) {
    CBaseEntity* target=player->m_hObserverTarget;
    if(!player->IsAlive()&&player->pev->iuser1&&player->pev->iuser1!=OBS_ROAMING
        &&target&&target->IsPlayer()&&target!=player)
        return target->pev;
    return player->pev;
}
