// The native class owns pev.max_health. MC only supplies an expiring effective
// limit, so class changes, reconnects and ordinary servers never inherit it.
struct NativeHealthLimit {
    status_health::State state;
    std::uint64_t authority=0;
    float updated=0,expires=0;
    edict_t* table=nullptr;
    CBasePlayer* player=nullptr;
    int entity_serial=0;
};
std::array<NativeHealthLimit,65> native_health_limits;
void ClearStatusHealth(){native_health_limits={};}
const status_health::State* CurrentHealthLimit(unsigned slot){
    if(!edicts||slot<1||slot>static_cast<unsigned>(max_clients)||!link.status_health())return nullptr;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return nullptr;
    const auto& current=native_health_limits[slot];const auto& s=current.state;const auto* pair=registry.get(slot);
    if(!pair||current.authority!=link.generation()||gpGlobals->time>=current.expires||gpGlobals->time<current.updated
        ||s.epoch!=registry.world()||s.serial!=pair->serial||s.spawn!=player_births[slot]||s.life!=player_lives[slot]
        ||s.base!=edicts[slot].v.max_health||current.table!=edicts||current.player!=identity.player
        ||current.entity_serial!=edicts[slot].serialnumber)return nullptr;
    return &s;
}
void HandleStatusHealth(std::span<const std::uint8_t> bytes){
    if(!link.status_health())throw ProtocolError("Unnegotiated health limit");
    const auto authority=link.generation();
    const auto s=status_health::decode(bytes);
    if(!edicts||s.slot>static_cast<unsigned>(max_clients))return;
    const auto identity=CaptureNativePlayer(s.slot);
    const auto* pair=registry.get(s.slot);
    if(!pair||s.epoch!=registry.world()||s.serial!=pair->serial||s.spawn!=player_births[s.slot]
        ||s.life!=player_lives[s.slot]||s.base!=edicts[s.slot].v.max_health||!identity.player
        ||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity)
        ||s.base!=edicts[s.slot].v.max_health||!link.status_health()||authority!=link.generation())return;
    auto& current=native_health_limits[s.slot];const auto& old=current.state;
    const bool same=current.authority==link.generation()&&old.epoch==s.epoch&&old.serial==s.serial&&old.spawn==s.spawn
        &&current.table==edicts&&current.player==identity.player&&current.entity_serial==edicts[s.slot].serialnumber;
    if(same&&s.revision<=old.revision)return;
    // Same-spawn form transitions retain the comparison history, but cannot
    // consume an old-life lease. A new class base starts a new comparison.
    const float previous=same&&old.base==s.base?old.effective:s.base;
    auto& health=edicts[s.slot].v.health;
    health=status_health::reduce(health,previous,s.effective);
    current={s,link.generation(),gpGlobals->time,gpGlobals->time+.35f,edicts,identity.player,edicts[s.slot].serialnumber};
    next_snapshot=0;
}
void SendHealthLimits(const std::vector<int>& slots){
    if(!link.status_health())return;
    const auto world=registry.world(),authority=link.generation();
    Writer records;unsigned count=0;
    for(int slot:slots){
        const auto identity=CaptureNativePlayer(slot);const auto* pair=registry.get(slot);
        if(!pair||!player_births[slot]||!player_lives[slot]||!identity.player||!CurrentNativePlayer(identity)
            ||!identity.player->IsAlive()||!CurrentNativePlayer(identity))continue;
        const float base=edicts[slot].v.max_health;
        if(!status_health::valid_limit(base))continue;
        const auto* current=CurrentHealthLimit(slot);
        if(registry.world()!=world||link.generation()!=authority||!link.status_health())return;
        if(!CurrentNativePlayer(identity)||base!=edicts[slot].v.max_health)continue;
        records.u32(slot);records.u32(pair->serial);records.u32(player_births[slot]);records.u32(player_lives[slot]);
        records.f32(base);records.f32(current?current->effective:base);records.u64(current?current->revision:0);++count;
    }
    Writer w;w.u64(world);w.u64(tick);w.u32(count);w.bytes(records.data);
    link.send(Type::health_limits,w.data);
}
