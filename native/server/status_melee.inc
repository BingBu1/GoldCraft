struct NativeMelee {
    status_melee::State state;NativePlayerIdentity identity;
    std::uint64_t authority=0;float updated=0,expires=0;
};
std::array<NativeMelee,65> native_melee;
void ClearStatusMelee(){native_melee={};}
void HandleStatusMelee(std::span<const std::uint8_t> bytes){
    if(!link.status_melee())throw ProtocolError("Unnegotiated melee attributes");
    const auto authority=link.generation();const auto s=status_melee::decode(bytes);
    if(!edicts)return;
    const auto* pair=s.slot<=static_cast<unsigned>(max_clients)?registry.get(s.slot):nullptr;
    if(!pair||s.epoch!=registry.world()||s.serial!=pair->serial||s.spawn!=player_births[s.slot]
        ||s.life!=player_lives[s.slot]||edicts[s.slot].free)return;
    const auto identity=CaptureNativePlayer(s.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity)
        ||!link.status_melee()||authority!=link.generation())return;
    auto& current=native_melee[s.slot];const auto& old=current.state;
    if(current.authority==authority&&current.identity.table==edicts&&current.identity.player==identity.player
        &&current.identity.entity_serial==identity.entity_serial&&old.epoch==s.epoch&&old.serial==s.serial
        &&old.spawn==s.spawn&&old.life==s.life&&s.revision<=old.revision)return;
    current={s,identity,authority,gpGlobals->time,gpGlobals->time+.35f};
}
const status_melee::State* CurrentMelee(edict_t* entity){
    if(!entity||!edicts||!link.status_melee()||entity==final_minecraft_damage||entity==minecraft_attack)return nullptr;
    const int slot=ENTINDEX(entity);
    if(slot<1||slot>max_clients||entity->free||minecraft_forms[slot]||entity->v.iuser1)return nullptr;
    const auto& entry=native_melee[slot];const auto& s=entry.state;const auto identity=entry.identity;
    if(entry.authority!=link.generation()||!identity.player||!CurrentNativePlayer(identity)
        ||!identity.player->IsAlive()||!CurrentNativePlayer(identity)||!link.status_melee()
        ||entry.authority!=link.generation())return nullptr;
    return gpGlobals->time>=entry.updated&&gpGlobals->time<entry.expires
        &&s.spawn==player_births[slot]&&s.life==player_lives[slot]?&s:nullptr;
}
