// Native round lifecycle; included after map-mining state and helpers.
void GoldCraft_MapMiningFrame() {
    if (!policy.epoch || !g_pGameRules || !g_pGameRules->IsMultiplayer())
        return;
    // A round-end announcement starts the victory delay, not the next round.
    // Preserve both mined geometry and entities until the real CleanUpMap.
    // An edict can disappear, be reused, or change inline model independently
    // of mining. Publish all removals together so one stale target cannot block
    // the next unrelated cut. Failures retain the old transaction and retry
    // without skipping the rest of this frame's bridge processing.
    if (map_physics)
        background_pruner.run(map_edits, *map_physics, LiveEditTarget, [](const char *reason) {
            ALERT(at_console, "Map edit cleanup deferred; will retry: %s\n", reason);
        });
}

GoldCraft_MapMiningRoundCleanup::GoldCraft_MapMiningRoundCleanup() {
    if (cleanup_depth++ != 0)
        return;
    GoldCraft_RoundCleanup();
    cleanup_persist = Persist();
    // CleanUpMap runs at RestartRound, before player respawns in ReGameDLL fixes.
    // It also handles direct rg_restart_round / sv_restart without an end frame.
    // Native cleanup performs the reset once when preservation is disabled.
    InvalidateRequests();
    if (!cleanup_persist) {
        mined_entities.clear();
        map_edits.restore();
        ClearMapPhysics();
    }
}
GoldCraft_MapMiningRoundCleanup::~GoldCraft_MapMiningRoundCleanup() {
    if (--cleanup_depth == 0)
        cleanup_persist = false;
}
bool GoldCraft_MapMiningKeepEntity(edict_t *entity) {
    if (!cleanup_depth || !cleanup_persist || !entity)
        return false;
    const auto found = mined_entities.find(ENTINDEX(entity));
    if (found == mined_entities.end())
        return false;
    if (found->second.matches(entity))
        return true;
    mined_entities.erase(found); // A reused slot/model must follow its own lifecycle.
    return false;
}
