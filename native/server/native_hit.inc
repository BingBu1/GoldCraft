// Included at global scope in bridge.cpp after the native identity helpers.
struct GoldCraft_NativeHitScope::State {
    edict_t* table=nullptr;
    edict_t* target=nullptr;
    CBasePlayer* player=nullptr;
    std::uint64_t epoch=0,generation=0;
    unsigned slot=0,serial=0,spawn=0,life=0,attacker=0,attacker_serial=0,attacker_spawn=0,kind=0;
    int engine_serial=0;
    goldcraft::Vec3 position{};
    float amount=0;
    bool notified=false;
};
namespace {
unsigned NativeArmorKind(int bits,edict_t* inflictor,edict_t* attacker);
std::shared_ptr<GoldCraft_NativeHitScope::State> current_native_hit;
std::uint64_t native_hit_sequence=0;
bool CurrentNativeHit(const GoldCraft_NativeHitScope::State& hit){
    if(edicts!=hit.table||registry.world()!=hit.epoch||link.generation()!=hit.generation||!link.native_hit())return false;
    const auto* pair=registry.get(hit.slot);
    return pair&&pair->serial==hit.serial&&player_births[hit.slot]==hit.spawn
        &&!hit.target->free&&hit.target->serialnumber==hit.engine_serial
        &&GET_PRIVATE<CBasePlayer>(hit.target)==hit.player;
}
void EmitNativeHit(const std::shared_ptr<GoldCraft_NativeHitScope::State>& hit){
    if(!hit||hit->notified||!CurrentNativeHit(*hit)||!std::isfinite(hit->amount)||hit->amount<=0)return;
    // Queue before lethal callbacks can publish a dead actor or retire effects.
    // Endpoint disconnects on notification overflow; never replay into a new generation.
    hit->notified=true;
    if(native_hit_sequence==UINT64_MAX)return;
    Writer w;w.u64(hit->epoch);w.u64(++native_hit_sequence);w.u64(tick);
    w.u32(hit->slot);w.u32(hit->serial);w.u32(hit->spawn);w.u32(hit->life);
    w.u32(hit->attacker);w.u32(hit->attacker_serial);w.u32(hit->attacker_spawn);w.u32(hit->kind);
    w.f32(hit->amount);w.f32(hit->position.x);w.f32(hit->position.y);w.f32(hit->position.z);
    link.send(Type::native_hit,w.data);
}
std::shared_ptr<GoldCraft_NativeHitScope::State> NativeHitPayment(edict_t* target,float amount){
    auto hit=current_native_hit;
    if(!hit||hit->target!=target||!CurrentNativeHit(*hit))return {};
    hit->amount=amount;return hit;
}
void NativeHitImpact(unsigned kind,const std::array<float,3>& position){
    if(current_native_hit){current_native_hit->kind=kind;current_native_hit->position={position[0],position[1],position[2]};}
}
}
GoldCraft_NativeHitScope::GoldCraft_NativeHitScope():previous_(std::move(current_native_hit)){}
GoldCraft_NativeHitScope::GoldCraft_NativeHitScope(std::shared_ptr<State> state):GoldCraft_NativeHitScope(){
    current_native_hit=std::move(state);
}
GoldCraft_NativeHitScope::GoldCraft_NativeHitScope(edict_t* target,edict_t* inflictor,edict_t* attacker,int bits)
    :GoldCraft_NativeHitScope(){
    if(!edicts||!target||target==final_minecraft_damage||target==minecraft_attack||!link.native_hit()||!tick)return;
    const int slot=ENTINDEX(target);
    if(slot<1||slot>max_clients||target->free||target->v.takedamage==DAMAGE_NO
        ||target->v.deadflag!=DEAD_NO||target->v.health<=0)return;
    const auto* pair=registry.get(slot);auto* player=GET_PRIVATE<CBasePlayer>(target);
    if(!pair||!player||!player_births[slot]||!player_lives[slot])return;
    auto hit=std::make_shared<State>();
    hit->table=edicts;hit->target=target;hit->player=player;hit->engine_serial=target->serialnumber;
    hit->epoch=registry.world();hit->generation=link.generation();
    hit->slot=slot;hit->serial=pair->serial;hit->spawn=player_births[slot];hit->life=player_lives[slot];
    hit->kind=NativeArmorKind(bits,inflictor,attacker);
    if(attacker&&!attacker->free){
        const int source=ENTINDEX(attacker);
        if(source>0&&source<=max_clients)if(const auto* owner=registry.get(source);owner&&player_births[source]){
            hit->attacker=source;hit->attacker_serial=owner->serial;hit->attacker_spawn=player_births[source];
        }
    }
    const auto position=inflictor&&!inflictor->free?inflictor->v.origin:target->v.origin;
    hit->position={position.x,position.y,position.z};current_native_hit=std::move(hit);
}
GoldCraft_NativeHitScope::~GoldCraft_NativeHitScope(){current_native_hit=std::move(previous_);}
void GoldCraft_NativeDamageCommitted(edict_t* target,float amount){
    const auto hit=current_native_hit;
    if(!hit||hit->target!=target||!std::isfinite(amount)||amount<=0)return;
    EmitNativeHit(hit);
}
