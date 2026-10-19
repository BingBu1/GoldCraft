// A movement sample shares the actor tick and life, but remains optional for
// older peers. Read engine state; never infer grounding from the MC proxy.
void SendNativeMovement(const std::vector<int>& slots){
    if(!edicts||!link.native_movement())return;
    const auto world=registry.world(),authority=link.generation();
    Writer records;unsigned count=0;
    for(int slot:slots){
        const auto identity=CaptureNativePlayer(slot);const auto spawn=player_births[slot];
        if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()
            ||!CurrentNativePlayer(identity)||!spawn||spawn!=player_births[slot]||!identity.life)continue;
        const auto& v=edicts[slot].v;
        const auto velocity=v.velocity;
        if(!std::isfinite(velocity.x)||!std::isfinite(velocity.y)||!std::isfinite(velocity.z)
            ||std::abs(velocity.x)>65536||std::abs(velocity.y)>65536||std::abs(velocity.z)>65536)continue;
        records.u32(slot);records.u32(identity.serial);records.u32(spawn);records.u32(identity.life);
        records.f32(velocity.x);records.f32(velocity.y);records.f32(velocity.z);
        records.u32(((v.flags&FL_ONGROUND)?1u:0u)|(v.movetype==MOVETYPE_WALK?2u:0u));++count;
    }
    if(registry.world()!=world||link.generation()!=authority||!link.native_movement())return;
    Writer w;w.u64(world);w.u64(tick);w.u32(count);w.bytes(records.data);
    link.send(Type::native_movement,w.data);
}
