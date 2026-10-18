struct NativeNausea {status_nausea::State state;NativePlayerIdentity identity;std::uint64_t authority=0;};
std::array<NativeNausea,65> native_nausea;
int nausea_message=0;
std::uint64_t nausea_delivery_revision=0;
void ClearStatusNausea(bool new_world=false){native_nausea={};if(new_world)nausea_delivery_revision=0;}
void HandleStatusNausea(std::span<const std::uint8_t> bytes){
    if(!link.status_nausea())throw ProtocolError("Unnegotiated nausea state");
    const auto authority=link.generation();const auto s=status_nausea::decode(bytes);
    if(!edicts)return;
    const auto* pair=s.slot<=static_cast<unsigned>(max_clients)?registry.get(s.slot):nullptr;
    if(!pair||s.epoch!=registry.world()||s.serial!=pair->serial||s.spawn!=player_births[s.slot]
        ||s.life!=player_lives[s.slot]||edicts[s.slot].free)return;
    const auto identity=CaptureNativePlayer(s.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity)
        ||!link.status_nausea()||authority!=link.generation())return;
    auto& current=native_nausea[s.slot];const auto& old=current.state;
    if(current.authority==authority&&current.identity.table==edicts&&current.identity.player==identity.player
        &&current.identity.entity_serial==identity.entity_serial&&old.epoch==s.epoch&&old.serial==s.serial
        &&old.spawn==s.spawn&&old.life==s.life&&s.revision<=old.revision)return;
    current={s,identity,authority};
    auto* e=edicts+s.slot;
    if(nausea_message&&client_ready[s.slot]&&!(e->v.flags&FL_FAKECLIENT)
        &&std::strcmp(g_engfuncs.pfnInfoKeyValue(g_engfuncs.pfnGetInfoKeyBuffer(e),status_nausea::client_capability),"1")==0
        &&CurrentNativePlayer(identity)&&link.status_nausea()&&authority==link.generation()){
        if(nausea_delivery_revision==UINT64_MAX)return;
        auto next=s;next.revision=++nausea_delivery_revision;const auto payload=status_nausea::encode(next);
        MESSAGE_BEGIN(MSG_ONE,nausea_message,nullptr,e);for(auto b:payload)WRITE_BYTE(b);MESSAGE_END();
    }
}
