// Included after status_attributes.inc in bridge.cpp's private namespace.
// Only native health is deferred; native validation/pre-hooks/CS armor ran once.
struct PendingNativeArmor {
    ArmorRequest request;
    StatusSource target,inflictor,attacker,logged_attacker;
    Key owner{};
    float expires=0;
    int bits=0,hitgroup=0,armor_hit=0;
    bool team_attack=false;
    std::string weapon;
    Vector blast_vector;
};
std::map<std::uint64_t,PendingNativeArmor> pending_native_armor;
std::uint64_t native_armor_sequence=0;
edict_t* settling_native_armor=nullptr;
float settling_native_enchantment=1;
const char* settling_native_weapon=nullptr;
void ClearNativeArmor(){pending_native_armor.clear();native_armor_sequence=0;}

unsigned NativeArmorKind(int bits,edict_t* inflictor,edict_t* attacker){
    if(bits&(DMG_BLAST|DMG_EXPLOSION))return 3;
    if(bits&(DMG_BURN|DMG_SLOWBURN))return 4;
    if(bits&DMG_FALL)return 5;
    if(bits&DMG_DROWN)return 6;
    if(bits&(DMG_ACID|DMG_POISON|DMG_NERVEGAS))return 7;
    if(bits&(DMG_CLUB|DMG_SLASH))return 1;
    // Both FireBullets3 and CKnife use BULLET|NEVERGIB. The damage mask alone
    // cannot distinguish a knife from a gun; inspect the actual attacking item.
    if((bits&DMG_BULLET)&&attacker&&attacker==inflictor&&!attacker->free){
        const auto slot=ENTINDEX(attacker);
        if(slot>=1&&slot<=max_clients){
            const auto* player=GET_PRIVATE<CBasePlayer>(attacker);
            if(player&&player->m_pActiveItem&&player->m_pActiveItem->m_iId==WEAPON_KNIFE)return 1;
        }
    }
    if(bits&DMG_BULLET)return 2;
    return 0;
}
bool QueueNativeArmor(edict_t* victim,float damage,edict_t* inflictor,edict_t* attacker,int bits,
    edict_t* logged_attacker,bool team_attack,int armor_hit){
    if(!link.native_armor()||!link.status_attributes()
        ||!std::isfinite(damage)||damage<=0||damage>500000
        ||pending_native_armor.size()>=128||native_armor_sequence==UINT64_MAX)return false;
    auto* attributes=CurrentAttributes(victim);
    if(!attributes)return false;
    const auto slot=ENTINDEX(victim);
    const auto* pair=registry.get(slot);
    if(!pair||!pair->paired||!minecraft_forms[slot]||!controlled[slot].active)return false;
    const auto kind=NativeArmorKind(bits,inflictor,attacker);if(!kind)return false;
    const auto identity=CaptureNativePlayer(slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return false;
    PendingNativeArmor pending;
    auto& q=pending.request;
    q.epoch=registry.world();q.event=native_armor_sequence+1;
    q.slot=slot;q.serial=pair->serial;q.spawn=player_births[slot];q.life=player_lives[slot];
    q.damage=damage;q.kind=static_cast<ArmorDamageKind>(kind);
    pending.target=CaptureStatusSource(victim);pending.inflictor=CaptureStatusSource(inflictor);
    pending.attacker=CaptureStatusSource(attacker);pending.owner=pair->uuid;
    pending.logged_attacker=CaptureStatusSource(logged_attacker);
    pending.team_attack=team_attack;pending.armor_hit=armor_hit;
    pending.blast_vector=identity.player->m_vBlastVector;
    pending.weapon=identity.player->GetKillerWeaponName(inflictor?&inflictor->v:nullptr,attacker?&attacker->v:&edicts->v);
    if(pending.attacker.serial){q.attacker=ENTINDEX(attacker);q.attacker_serial=pending.attacker.serial;q.attacker_spawn=pending.attacker.spawn;}
    const auto position=inflictor&&!inflictor->free?inflictor->v.origin:victim->v.origin;
    q.position={position.x,position.y,position.z};
    pending.bits=bits;pending.hitgroup=identity.player->m_LastHitGroup;pending.expires=gpGlobals->time+0.75f;
    if(!link.send(Type::native_armor_request,write_armor_request(q)))return false;
    pending_native_armor.emplace(q.event,pending);native_armor_sequence=q.event;
    return true;
}
void SettleNativeArmor(const PendingNativeArmor& pending,float afterArmor,float enchantment){
    const auto& q=pending.request;
    if(q.epoch!=registry.world())return;
    auto* victim=CurrentStatusSource(pending.target);
    if(!victim||victim==edicts)return;
    const auto identity=CaptureNativePlayer(q.slot);
    if(!identity.player||!CurrentNativePlayer(identity)||!identity.player->IsAlive()||!CurrentNativePlayer(identity))return;
    auto* inflictor=CurrentStatusSource(pending.inflictor);
    auto* attacker=CurrentStatusSource(pending.attacker);
    // A form switch retains the native spawn and its accepted hit, but cannot
    // accept an equipment result for the prior controller/life.
    const auto* pair=registry.get(q.slot);
    if(player_lives[q.slot]!=q.life||!pair||pair->uuid!=pending.owner){afterArmor=q.damage;enchantment=1;}
    struct Scope {
        edict_t* target=settling_native_armor;float factor=settling_native_enchantment;const char* weapon=settling_native_weapon;
        ~Scope(){settling_native_armor=target;settling_native_enchantment=factor;settling_native_weapon=weapon;}
    };
    GoldCraft_NativeHitScope hit_scope(victim,inflictor,attacker,pending.bits);
    NativeHitImpact(static_cast<unsigned>(q.kind),q.position);
    float finalDamage;
    {
        Scope scope;
        settling_native_armor=victim;settling_native_enchantment=enchantment;
        finalDamage=GoldCraft_StatusDefense(victim,afterArmor,inflictor,attacker,pending.bits);
    }
    if(!CurrentNativePlayer(identity))return;
    Scope weaponScope;
    settling_native_armor=victim;settling_native_enchantment=1;settling_native_weapon=pending.weapon.c_str();
    const int previous=identity.player->m_LastHitGroup;
    const Vector previous_blast=identity.player->m_vBlastVector;
    identity.player->m_LastHitGroup=pending.hitgroup;
    identity.player->m_vBlastVector=pending.blast_vector;
    auto* logged=CurrentStatusSource(pending.logged_attacker);
    auto* logged_player=logged==edicts?nullptr:GET_PRIVATE<CBasePlayer>(logged);
    // Resume the exact original native tail: health/death, attack log, bot
    // injury, RecordDamage and observer health run once with the final amount.
    identity.player->GoldCraft_CommitEquipmentDamage(&inflictor->v,&attacker->v,finalDamage,pending.bits,
        logged_player,pending.team_attack?TRUE:FALSE,pending.armor_hit,(pending.bits&(DMG_BLAST|DMG_EXPLOSION))!=0);
    if(CurrentNativePlayer(identity)){identity.player->m_LastHitGroup=previous;identity.player->m_vBlastVector=previous_blast;}
    next_snapshot=0;
}
void HandleNativeArmorResult(std::span<const std::uint8_t> bytes){
    if(!link.native_armor())throw ProtocolError("Unnegotiated native armor result");
    const auto reply=read_armor_result(bytes);
    const auto it=pending_native_armor.find(reply.event);
    if(it==pending_native_armor.end())return;
    const auto pending=it->second;const auto& q=pending.request;
    if(reply.epoch!=q.epoch||reply.slot!=q.slot||reply.serial!=q.serial||reply.spawn!=q.spawn||reply.life!=q.life)return;
    if(reply.after_armor>q.damage)throw ProtocolError("Native armor increased validated damage");
    pending_native_armor.erase(it); // Remove before callbacks: duplicates cannot wear or damage again.
    SettleNativeArmor(pending,reply.status?q.damage:reply.after_armor,reply.status?1:reply.enchantment_multiplier);
}
void ExpireNativeArmor(){
    auto* const table=edicts;const auto world=registry.world(),generation=link.generation();
    const bool connected=link.connected();
    for(auto it=pending_native_armor.begin();it!=pending_native_armor.end();){
        if(it->second.expires>gpGlobals->time){++it;continue;}
        const auto pending=it->second;pending_native_armor.erase(it);
        SettleNativeArmor(pending,pending.request.damage,1);
        if(edicts!=table||registry.world()!=world||link.generation()!=generation||link.connected()!=connected)return;
        // Damage callbacks can replace the world or clear this queue.
        it=pending_native_armor.begin();
    }
}
void RetireNativeArmor(){
    auto pending=std::move(pending_native_armor);pending_native_armor.clear();native_armor_sequence=0;
    auto* const table=edicts;const auto world=registry.world(),generation=link.generation();
    const bool connected=link.connected();
    for(const auto& [event,hit]:pending){
        SettleNativeArmor(hit,hit.request.damage,1);
        if(edicts!=table||registry.world()!=world||link.generation()!=generation||link.connected()!=connected)return;
    }
}
