// Presentation only: retain entity/model/solid so client prediction and hits agree.
struct NativeVisibility {status_visibility::State state;float updated=0,expires=0;};
std::array<NativeVisibility,65> native_visibility;
void ClearStatusVisibility(){native_visibility={};}
void HandleStatusVisibility(std::span<const std::uint8_t> bytes){
    if(!link.status_visibility())throw ProtocolError("Unnegotiated visibility state");
    const auto state=status_visibility::decode(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto& current=native_visibility[state.slot];const auto& old=current.state;
    if(old.epoch==state.epoch&&old.serial==state.serial&&old.spawn==state.spawn&&state.revision<=old.revision)return;
    current={state,gpGlobals->time,gpGlobals->time+.35f};
}
bool StatusInvisible(edict_t* entity){
    if(!entity||!edicts||!link.status_visibility())return false;
    const int slot=ENTINDEX(entity);
    if(slot<1||slot>max_clients||entity->free)return false;
    const auto identity=CaptureNativePlayer(static_cast<unsigned>(slot));
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return false;
    const auto& current=native_visibility[slot];const auto& s=current.state;const auto* pair=registry.get(slot);
    return pair&&gpGlobals->time<current.expires&&gpGlobals->time>=current.updated
        &&s.epoch==registry.world()&&s.serial==pair->serial&&s.spawn==player_births[slot]&&s.life==player_lives[slot]
        &&(s.flags&status_visibility::invisible)!=0;
}
