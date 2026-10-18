// Authoritative leases and reliable presentation delivery are independent of
// invisible bodies and of the MC mesh channel. Never change entity entvars.
struct NativeGlow {status_glow::State state;NativePlayerIdentity identity;std::uint64_t authority=0;float updated=0,expires=0;};
std::array<NativeGlow,65> native_glow;
struct GlowDelivery {status_glow::Presentation presentation;float next=0,updated=0;};
std::array<GlowDelivery,65> glow_deliveries;
std::uint64_t glow_delivery_revision=0;
int glow_message=0;
void ClearStatusGlow(bool new_world=false){
    native_glow={};
    if(new_world){glow_deliveries={};glow_delivery_revision=0;}
}
void HandleStatusGlow(std::span<const std::uint8_t> bytes){
    if(!link.status_glow())throw ProtocolError("Unnegotiated native outline state");
    const auto authority=link.generation();
    const auto s=status_glow::decode(bytes);
    if(!edicts)return;
    const auto* pair=s.slot<=static_cast<unsigned>(max_clients)?registry.get(s.slot):nullptr;
    if(!pair||s.epoch!=registry.world()||s.serial!=pair->serial||s.spawn!=player_births[s.slot]
        ||s.life!=player_lives[s.slot]||edicts[s.slot].free)return;
    const auto identity=CaptureNativePlayer(s.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity)
        ||!link.status_glow()||authority!=link.generation())return;
    auto& current=native_glow[s.slot];const auto& old=current.state;
    if(current.authority==authority&&current.identity.table==edicts&&current.identity.player==identity.player
        &&current.identity.entity_serial==identity.entity_serial&&old.epoch==s.epoch
        &&old.serial==s.serial&&old.spawn==s.spawn&&s.revision<=old.revision)return;
    current={s,identity,authority,gpGlobals->time,gpGlobals->time+.35f};
}
const status_glow::State* CurrentGlow(unsigned slot){
    if(!edicts||!slot||slot>static_cast<unsigned>(max_clients)||!link.status_glow())return nullptr;
    const auto& entry=native_glow[slot];const auto& s=entry.state;const auto* pair=registry.get(slot);
    const auto identity=entry.identity;
    if(entry.authority!=link.generation()||!identity.player||!CurrentNativePlayer(identity)
        ||!identity.player->IsAlive()||!CurrentNativePlayer(identity)||!link.status_glow()
        ||entry.authority!=link.generation())return nullptr;
    return pair&&gpGlobals->time>=entry.updated&&gpGlobals->time<entry.expires
        &&s.epoch==registry.world()&&s.serial==pair->serial&&s.spawn==player_births[slot]&&s.life==player_lives[slot]
        &&s.flags==status_glow::enabled?&s:nullptr;
}
bool GlowViewer(edict_t* viewer){
    if(!viewer||!edicts)return false;
    const int slot=ENTINDEX(viewer);
    return slot>=1&&slot<=max_clients&&client_ready[slot]&&registry.get(slot)&&!viewer->free
        &&viewer->pvPrivateData&&!(viewer->v.flags&FL_FAKECLIENT)
        &&std::strcmp(g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(viewer),status_glow::client_capability),"1")==0;
}
bool StatusGlowVisible(edict_t* entity,edict_t* viewer){
    return GlowViewer(viewer)&&entity&&CurrentGlow(static_cast<unsigned>(ENTINDEX(entity)));
}
void SendStatusGlow(){
    if(!edicts||!glow_message)return;
    for(int slot=1;slot<=max_clients;++slot){
        auto& delivery=glow_deliveries[slot];auto next=delivery.presentation;
        if(const auto* state=CurrentGlow(static_cast<unsigned>(slot))){
            const auto* pair=registry.get(slot);next={*state,pair->userid,pair->uuid};
        }else{next.state.flags=0;next.state.color=0;}
        // No initial disabled roster is needed. A joining viewer receives an
        // active slot on its next bounded heartbeat; an old viewer gets a clear.
        if(!next.state.epoch||!next.userid)continue;
        const auto& old=delivery.presentation;
        const bool changed=next.identity()!=old.identity()||next.state.epoch!=old.state.epoch
            ||next.state.spawn!=old.state.spawn||next.state.life!=old.state.life
            ||next.state.flags!=old.state.flags||next.state.color!=old.state.color;
        if(!changed&&(!next.state.flags||(gpGlobals->time>=delivery.updated&&gpGlobals->time<delivery.next)))continue;
        if(glow_delivery_revision==UINT64_MAX)return;
        next.state.revision=++glow_delivery_revision;
        delivery={next,gpGlobals->time+.15f,gpGlobals->time};
        const auto bytes=status_glow::encode(next);
        for(int viewer=1;viewer<=max_clients;++viewer){
            auto* receiver=edicts+viewer;if(!GlowViewer(receiver))continue;
            MESSAGE_BEGIN(MSG_ONE,glow_message,nullptr,receiver);for(auto b:bytes)WRITE_BYTE(b);MESSAGE_END();
        }
    }
}
