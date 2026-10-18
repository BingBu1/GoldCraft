struct NativeWater {native_water::State state;float updated=0,expires=0;std::uint64_t authority=0;};
std::array<NativeWater,65> native_water_states;
int water_message=0;
void ClearNativeWater(){native_water_states={};}
void HandleNativeWater(std::span<const std::uint8_t> bytes){
    if(!link.native_water())throw ProtocolError("Unnegotiated water state");
    const auto authority=link.generation();
    const auto state=native_water::decode(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity)
        ||!link.native_water()||link.generation()!=authority)return;
    auto& current=native_water_states[state.slot];const auto& old=current.state;
    if(old.epoch==state.epoch&&old.serial==state.serial&&old.spawn==state.spawn&&state.revision<=old.revision)return;
    current={state,gpGlobals->time,gpGlobals->time+.35f,authority};
    auto* e=edicts+state.slot;
    if(water_message&&client_ready[state.slot]&&!(e->v.flags&FL_FAKECLIENT)
        &&std::strcmp(g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(e),native_water::client_capability),"1")==0){
        MESSAGE_BEGIN(MSG_ONE,water_message,nullptr,e);for(auto b:bytes)WRITE_BYTE(b);MESSAGE_END();
    }
}
const native_water::State* CurrentWater(unsigned slot){
    if(!edicts||slot<1||slot>static_cast<unsigned>(max_clients)||!link.native_water()||minecraft_forms[slot]||controlled[slot].active)return nullptr;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return nullptr;
    const auto& current=native_water_states[slot];const auto& s=current.state;const auto* pair=registry.get(slot);
    if(!pair||!link.native_water()||current.authority!=link.generation()||gpGlobals->time>=current.expires||gpGlobals->time<current.updated||s.epoch!=registry.world()
        ||s.serial!=pair->serial||s.spawn!=player_births[slot]||s.life!=player_lives[slot])return nullptr;
    return &s;
}
