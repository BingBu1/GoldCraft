std::array<status_vision::State,65> native_vision;
int vision_message=0;
void ClearStatusVision(){native_vision={};}
void HandleStatusVision(std::span<const std::uint8_t> bytes){
    if(!link.status_vision())throw ProtocolError("Unnegotiated vision state");
    const auto state=status_vision::decode(bytes);
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||state.epoch!=registry.world()||state.serial!=pair->serial||state.spawn!=player_births[state.slot]
        ||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    const auto identity=CaptureNativePlayer(state.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto& old=native_vision[state.slot];
    if(old.epoch==state.epoch&&old.serial==state.serial&&old.spawn==state.spawn&&state.revision<=old.revision)return;
    old=state;
    auto* e=edicts+state.slot;
    if(vision_message&&client_ready[state.slot]&&!(e->v.flags&FL_FAKECLIENT)
        &&HasClientFeature(e,client_features::vision,status_vision::client_capability)){
        MESSAGE_BEGIN(MSG_ONE,vision_message,nullptr,e);for(auto b:bytes)WRITE_BYTE(b);MESSAGE_END();
    }
}
