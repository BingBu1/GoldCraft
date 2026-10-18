// Export the engine's actual eye point, including custom view offsets. The
// shallow-water buoyancy promotion is deliberately not an eye-water sample.
void SendNativeView(const std::vector<int>& slots){
    if(!edicts||!link.native_view())return;
    const auto world=registry.world(),authority=link.generation();
    Writer records;unsigned count=0;
    for(int slot:slots){
        const auto identity=CaptureNativePlayer(slot);const auto spawn=player_births[slot];
        if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()
            ||!CurrentNativePlayer(identity)||!spawn||!identity.life)continue;
        const auto eye=edicts[slot].v.origin+edicts[slot].v.view_ofs;
        if(!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z))continue;
        int contents=g_engfuncs.pfnPointContents(eye);
        if(contents==fluids::empty)if(const auto* field=ActiveFluids())contents=field->contents(eye);
        if(registry.world()!=world||link.generation()!=authority||!link.native_view())return;
        if(!CurrentNativePlayer(identity)||spawn!=player_births[slot])continue;
        records.u32(slot);records.u32(identity.serial);records.u32(spawn);records.u32(identity.life);
        records.f32(eye.x);records.f32(eye.y);records.f32(eye.z);
        records.u32(contents==CONTENTS_WATER?1u:0u);++count;
    }
    Writer w;w.u64(world);w.u64(tick);w.u32(count);w.bytes(records.data);
    link.send(Type::native_view,w.data);
}
