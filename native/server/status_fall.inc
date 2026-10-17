// One server-authored snapshot at landing; no client HUD or movement claim.
struct NativeFall {status_fall::State state;float updated=0,expires=0;};
std::array<NativeFall,65> native_fall;
void ClearStatusFall(){native_fall={};}
void HandleStatusFall(std::span<const std::uint8_t> bytes){
    if(!link.status_fall())throw ProtocolError("Unnegotiated fall state");
    const auto state=status_fall::decode(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto& current=native_fall[state.slot];const auto& old=current.state;
    if(old.epoch==state.epoch&&old.serial==state.serial&&old.spawn==state.spawn&&state.revision<=old.revision)return;
    current={state,gpGlobals->time,gpGlobals->time+.35f};
}
const status_fall::State* CurrentFall(unsigned slot){
    if(!edicts||slot<1||slot>static_cast<unsigned>(max_clients)||!link.status_fall()
        ||minecraft_forms[slot]||controlled[slot].active)return nullptr;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return nullptr;
    const auto& current=native_fall[slot];const auto& s=current.state;const auto* pair=registry.get(slot);
    if(!pair||gpGlobals->time>=current.expires||gpGlobals->time<current.updated||s.epoch!=registry.world()
        ||s.serial!=pair->serial||s.spawn!=player_births[slot]||s.life!=player_lives[slot])return nullptr;
    return &s;
}
