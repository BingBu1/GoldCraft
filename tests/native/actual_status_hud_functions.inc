void ClearStatusHud(){
    for(unsigned i=1;i<=static_cast<unsigned>(max_clients);++i){
        if(!native_status_hud[i].state.entries.empty())DrawStatusHud(i,"");
        native_status_hud[i]={};
    }
}
void HandleStatusHud(std::span<const std::uint8_t> bytes){
    if(!link.status_effects())throw ProtocolError("Unnegotiated status HUD");
    auto state=read_status_hud(bytes);
    const auto* binding=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!binding||state.epoch!=registry.world()||binding->serial!=state.serial||player_births[state.slot]!=state.spawn)return;
    auto& old=native_status_hud[state.slot];
    if(state.epoch==old.state.epoch&&state.serial==old.state.serial&&state.spawn==old.state.spawn&&state.revision<=old.state.revision)return;
    auto* player=GET_PRIVATE<CBasePlayer>(edicts+state.slot);
    if(!player||edicts[state.slot].free||!player->IsAlive())return;
    const auto text=status_hud_text(state);
    // Empty mirrors are also sent for paired players and unaffected native
    // players. Clear only our previously visible text; do not repeatedly erase
    // an AMXX message that may have subsequently acquired this HUD channel.
    if(!text.empty()||!old.state.entries.empty())DrawStatusHud(state.slot,text);
    old={std::move(state),text.empty()?0.0f:gpGlobals->time+1.2f};
}
void ExpireStatusHud(){
    for(unsigned i=1;i<=static_cast<unsigned>(max_clients);++i){
        auto& value=native_status_hud[i];if(!value.state.revision||value.expires==0)continue;
        const auto* pair=registry.get(i);auto* player=GET_PRIVATE<CBasePlayer>(edicts+i);
        if(!link.connected()||value.state.epoch!=registry.world()||!pair||pair->serial!=value.state.serial||player_births[i]!=value.state.spawn
            ||!player||!player->IsAlive()||gpGlobals->time>=value.expires){DrawStatusHud(i,"");value.expires=0;value.state.entries.clear();}
    }
}
