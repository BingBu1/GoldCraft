// Authenticated MC authority only. The 350ms lease never crosses a native life.
struct NativeEnvironment {status_environment::State state;float updated=0,expires=0;};
std::array<NativeEnvironment,65> native_environment;
void ClearStatusEnvironment(){native_environment={};}
void HandleStatusEnvironment(std::span<const std::uint8_t> bytes){
    if(!link.status_environment())throw ProtocolError("Unnegotiated environment state");
    const auto state=status_environment::decode(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto& current=native_environment[state.slot];const auto& old=current.state;
    if(old.epoch==state.epoch&&old.serial==state.serial&&old.spawn==state.spawn&&state.revision<=old.revision)return;
    current={state,gpGlobals->time,gpGlobals->time+0.35f};
}
const status_environment::State* CurrentEnvironment(edict_t* entity){
    if(!entity||!edicts||!link.status_environment())return nullptr;
    const int slot=ENTINDEX(entity);
    if(slot<1||slot>max_clients||entity->free)return nullptr;
    const auto identity=CaptureNativePlayer(static_cast<unsigned>(slot));
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return nullptr;
    const auto& current=native_environment[slot];const auto& s=current.state;
    const auto* pair=registry.get(slot);
    if(!pair||gpGlobals->time>=current.expires||gpGlobals->time<current.updated
        ||s.epoch!=registry.world()||s.serial!=pair->serial||s.spawn!=player_births[slot]||s.life!=player_lives[slot])return nullptr;
    return &s;
}
