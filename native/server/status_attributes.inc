// Included in bridge.cpp's namespace. All hooks run on the native game thread.
struct StatusSource {
    edict_t* entity=nullptr;int engine_serial=0;unsigned serial=0,spawn=0;
};
StatusSource CaptureStatusSource(edict_t* entity){
    if(!entity||entity->free)return {};
    const auto index=ENTINDEX(entity);
    const auto* pair=index>0&&index<=max_clients?registry.get(index):nullptr;
    return {entity,entity->serialnumber,pair?pair->serial:0,pair?player_births[index]:0};
}
edict_t* CurrentStatusSource(const StatusSource& source){
    if(!source.entity||source.entity->free||source.entity->serialnumber!=source.engine_serial)return edicts;
    if(source.serial){const auto index=ENTINDEX(source.entity);const auto* pair=registry.get(index);
        if(!pair||pair->serial!=source.serial||player_births[index]!=source.spawn)return edicts;}
    return source.entity;
}
struct StatusDamageOrigin {
    StatusSource inflictor,attacker;int bits=0;float damage=0,immediate=0;
    std::shared_ptr<GoldCraft_NativeHitScope::State> hit;
};
struct NativeAttributes {
    StatusBudget budget;float expires=0;std::uint64_t settled=0;
    std::map<std::uint64_t,StatusDamageOrigin> origins;
};
std::array<NativeAttributes,65> native_attributes;
void ClearStatusAttributes(){native_attributes={};}
void HandleStatusAttributes(std::span<const std::uint8_t> bytes){
    if(!link.status_attributes())throw ProtocolError("Unnegotiated status attributes");
    const auto state=read_status_attributes(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto& current=native_attributes[state.slot];const auto& old=current.budget.state;
    if(old.epoch!=state.epoch||old.serial!=state.serial||old.spawn!=state.spawn)current={};
    if(state.ack>current.settled)return;
    if(current.budget.update(state))current.expires=gpGlobals->time+0.35f;
}
NativeAttributes* CurrentAttributes(edict_t* entity){
    if(!entity||!edicts||!link.status_attributes())return nullptr;
    const auto slot=ENTINDEX(entity);
    if(slot<1||slot>max_clients||entity->free)return nullptr;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return nullptr;
    auto& current=native_attributes[slot];const auto& state=current.budget.state;
    const auto* pair=registry.get(slot);
    if(!pair||current.expires<=gpGlobals->time||state.epoch!=registry.world()
        ||state.serial!=pair->serial||state.spawn!=player_births[slot]||state.life!=player_lives[slot])return nullptr;
    return &current;
}
bool SpendStatus(NativeAttributes& value,float absorption,float durability,float shieldDamage,StatusDamageOrigin origin={}){
    auto& budget=value.budget;if(!budget.available())return false;
    const auto& s=budget.state;Writer w;
    w.u64(s.epoch);w.u64(budget.sequence+1);w.u64(s.revision);w.u64(s.shield);
    w.u32(s.slot);w.u32(s.serial);w.u32(s.spawn);w.u32(s.life);
    w.f32(absorption);w.f32(durability);w.f32(shieldDamage);
    if(!link.send(Type::status_spend,w.data))return false;
    budget.commit(absorption,durability);
    if(absorption>0)value.origins.emplace(budget.sequence,origin);
    return true;
}

void HandleStatusSpendResult(std::span<const std::uint8_t> bytes){
    if(!link.status_attributes())throw ProtocolError("Unnegotiated status spend result");
    Reader r(bytes);const auto world=r.u64(),event=r.u64();
    const auto slot=r.u32(),serial=r.u32(),spawn=r.u32();const auto paid=r.f32();r.finish();
    if(!world||!event||!slot||slot>64||!serial||!spawn||paid<0||paid>100000)throw ProtocolError("Invalid status spend result");
    const auto* pair=slot<=static_cast<unsigned>(max_clients)?registry.get(slot):nullptr;
    if(world!=registry.world()||!pair||pair->serial!=serial||player_births[slot]!=spawn)return;
    auto& value=native_attributes[slot];auto& budget=value.budget;
    if(budget.state.epoch!=world||budget.state.serial!=serial||budget.state.spawn!=spawn||event<=value.settled)return;
    const auto pending=std::find_if(budget.pending.begin(),budget.pending.end(),[&](const auto& item){return item.event==event;});
    if(event!=value.settled+1||pending==budget.pending.end()||paid>pending->absorption)throw ProtocolError("Status spend result order/amount");
    const auto old=value.origins.find(event);
    const StatusDamageOrigin origin=old==value.origins.end()?StatusDamageOrigin{}:old->second;
    if(old!=value.origins.end())value.origins.erase(old);
    value.settled=event;
    if(paid>0)EmitNativeHit(origin.hit);
    // Match the single integer truncation in CBasePlayer::TakeDamage. Rounding
    // the fractional deficit independently can lose one native HP per hit.
    const float deficit=std::trunc(std::max(0.0f,origin.damage-paid*5.0f))-std::trunc(origin.immediate);
    if(deficit<=0)return;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto* player=identity.player;
    auto* inflictor=CurrentStatusSource(origin.inflictor);
    auto* attacker=CurrentStatusSource(origin.attacker);
    // CS armor, resistance and ReAPI pre callbacks already ran for this hit.
    // Settle only the health part MC could not pay, through native death/credit.
    // Replaying TakeDamage here could rerun an infection or spend armor twice.
    GoldCraft_NativeHitScope hit_scope(origin.hit);
    player->CBaseMonster::TakeDamage(&inflictor->v,&attacker->v,deficit,origin.bits);
    next_snapshot=0;
}
