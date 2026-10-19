std::uint64_t native_impulse_event=0,native_impulse_generation=0,native_impulse_epoch=0;
void HandleNativeImpulse(std::span<const std::uint8_t> bytes){
    if(!link.native_impulse())throw ProtocolError("Unnegotiated native impulse");
    const auto q=native_impulse::decode(bytes);
    if(q.epoch!=registry.world()||!edicts)return;
    const auto authority=link.generation();
    if(native_impulse_generation!=authority||native_impulse_epoch!=q.epoch){native_impulse_generation=authority;native_impulse_epoch=q.epoch;native_impulse_event=0;}
    if(q.event<=native_impulse_event)return;
    native_impulse_event=q.event; // A rejected event cannot be replayed after a lifecycle change.
    const auto* pair=q.slot<=static_cast<unsigned>(max_clients)?registry.get(q.slot):nullptr;
    if(!pair||pair->serial!=q.serial||player_births[q.slot]!=q.spawn||player_lives[q.slot]!=q.life
        ||minecraft_forms[q.slot]||edicts[q.slot].free)return;
    const auto identity=CaptureNativePlayer(q.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity)
        ||!link.native_impulse()||link.generation()!=authority)return;
    auto* victim=edicts+q.slot;
    if(victim->v.iuser1||victim->v.movetype!=MOVETYPE_WALK||(identity.team!=1&&identity.team!=2))return;
    const auto center=victim->v.origin+(victim->v.mins+victim->v.maxs)*.5f;
    if(!skill_damage_snapshots.matches(q.epoch,q.tick,q.slot,q.serial,q.spawn,q.center,gpGlobals->time,{center.x,center.y,center.z}))return;
    const auto velocity=victim->v.velocity+Vector(q.delta.x,q.delta.y,q.delta.z);
    if(!std::isfinite(velocity.x)||!std::isfinite(velocity.y)||!std::isfinite(velocity.z))return;
    victim->v.velocity=velocity;
    if(q.delta.z>0)victim->v.flags&=~FL_ONGROUND;
    // Normal ReHLDS movement/collision and maxvelocity processing own the result.
}

native_knockback::Grants native_knockback_grants;
void HandleNativeKnockback(std::span<const std::uint8_t> bytes){
    if(!link.native_knockback())throw ProtocolError("Unnegotiated native knockback");
    if(bytes.size()!=72)throw ProtocolError("Invalid native knockback length");
    const auto q=native_impulse::decode(bytes.first(64));
    Reader r(bytes.subspan(64));const auto damage_event=r.u64();r.finish();
    if(!damage_event)throw ProtocolError("Missing native knockback damage event");
    const auto identity=CaptureNativePlayer(q.slot);
    if(!CurrentNativePlayer(identity))return;
    if(!native_knockback_grants.consume(q,damage_event,link.generation(),identity.team,gpGlobals->time))return;
    HandleNativeImpulse(bytes.first(64));
}
