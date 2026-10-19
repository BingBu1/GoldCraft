struct NativeMotion {status_motion::State state;float updated=0,expires=0;};
std::array<NativeMotion,65> native_motion;
int motion_message=0;
void ClearStatusMotion(){native_motion={};}
void HandleStatusMotion(std::span<const std::uint8_t> bytes){
    if(!link.status_motion())throw ProtocolError("Unnegotiated motion state");
    const auto state=status_motion::decode(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto& current=native_motion[state.slot];const auto& old=current.state;
    if(old.epoch==state.epoch&&old.serial==state.serial&&old.spawn==state.spawn&&state.revision<=old.revision)return;
    current={state,gpGlobals->time,gpGlobals->time+.35f};
    auto* e=edicts+state.slot;
    if(motion_message&&client_ready[state.slot]&&!(e->v.flags&FL_FAKECLIENT)
        &&HasClientFeature(e,client_features::motion,status_motion::client_capability)){
        MESSAGE_BEGIN(MSG_ONE,motion_message,nullptr,e);for(auto b:bytes)WRITE_BYTE(b);MESSAGE_END();
    }
}
const status_motion::State* CurrentMotion(unsigned slot){
    if(!edicts||slot<1||slot>static_cast<unsigned>(max_clients)||!link.status_motion()||minecraft_forms[slot]||controlled[slot].active)return nullptr;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return nullptr;
    const auto& current=native_motion[slot];const auto& s=current.state;const auto* pair=registry.get(slot);
    if(!pair||gpGlobals->time>=current.expires||gpGlobals->time<current.updated||s.epoch!=registry.world()
        ||s.serial!=pair->serial||s.spawn!=player_births[slot]||s.life!=player_lives[slot])return nullptr;
    return &s;
}
