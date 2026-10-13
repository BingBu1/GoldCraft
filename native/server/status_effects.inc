// Included inside bridge.cpp's private namespace. MC status callbacks are an
// authenticated server authority; these messages cannot arrive from a CS client.
struct NativeStatusHudState {goldcraft::StatusHud state;float expires=0;};
std::array<NativeStatusHudState,65> native_status_hud;
void DrawStatusHud(unsigned slot,const std::string& text){
    auto* player=GET_PRIVATE<CBasePlayer>(edicts+slot);
    if(!player||edicts[slot].free)return;
    hudtextparms_t style{};style.channel=4;style.x=0.72f;style.y=0.2f;
    style.r1=220;style.g1=235;style.b1=255;style.a1=255;style.holdTime=1.2f;
    UTIL_HudMessage(player,style,text.empty()?" ":text.c_str());
}
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
void HandleStatusDamage(const MobDamage& d){
    if(!link.status_effects())throw ProtocolError("Unnegotiated status damage");
    if(d.epoch!=registry.world()||d.event<=mob_damage_event){++mob_damage_rejected;return;}
    mob_damage_event=d.event;
    const auto generation=link.generation(),world=registry.world();
    unsigned status=1;float health=0,armor=0;
    const auto valid=[&](){const auto* pair=registry.get(d.slot);
        return link.connected()&&link.generation()==generation&&registry.world()==world&&pair&&pair->serial==d.serial
            &&player_births[d.slot]==d.life&&!edicts[d.slot].free;};
    if(d.slot<=static_cast<unsigned>(max_clients)&&valid()){
        auto* victim=edicts+d.slot;auto* player=GET_PRIVATE<CBasePlayer>(victim);
        auto* source=(d.status_flags&8)?edicts:d.attacker?nullptr:GoldCraft_MobObject(d.source_key);
        if(d.attacker&&d.attacker<=static_cast<unsigned>(max_clients)){
            const auto* caster=registry.get(d.attacker);
            auto* candidate=GET_PRIVATE<CBasePlayer>(edicts+d.attacker);
            if(caster&&caster->serial==d.attacker_serial&&player_births[d.attacker]==d.attacker_spawn
                &&!edicts[d.attacker].free&&candidate&&candidate->IsAlive())source=edicts+d.attacker;
        }
        if(player&&player->IsAlive()&&source){
            const auto sourceSerial=source->serialnumber;
            auto* sourceEntity=GET_PRIVATE<CBaseEntity>(source);
            const auto sourceCurrent=[&](){
                if(source->free||source->serialnumber!=sourceSerial||GET_PRIVATE<CBaseEntity>(source)!=sourceEntity)return false;
                if(d.status_flags&8)return source==edicts;
                if(d.attacker){const auto* pair=registry.get(d.attacker);auto* caster=GET_PRIVATE<CBasePlayer>(source);
                    return pair&&pair->serial==d.attacker_serial&&player_births[d.attacker]==d.attacker_spawn&&caster&&caster->IsAlive();}
                return GoldCraft_MobObject(d.source_key)==source;
            };
            const Vector center=victim->v.origin+(victim->v.mins+victim->v.maxs)*0.5f;
            if(skill_damage_snapshots.matches(d,gpGlobals->time,{center.x,center.y,center.z})){
                bool contact=true;
                if(d.status_flags&16){
                    // A splash point may already be inside the affected hull.
                    // Ignore that hull so fStartSolid means a real obstruction.
                    TraceResult trace;UTIL_TraceLine(Vector(d.source.x,d.source.y,d.source.z),center,dont_ignore_monsters,victim,&trace);
                    contact=!trace.fStartSolid&&(trace.flFraction>=0.999f||trace.pHit==victim);
                }
                const bool rulesAllow=contact&&(d.amount<0||g_pGameRules->FPlayerCanTakeDamage(player,sourceEntity));
                // Rules and Pawn hooks may disconnect or replace either participant.
                if(!valid()||GET_PRIVATE<CBasePlayer>(victim)!=player)return;
                if(!sourceCurrent())status=2;
                else if(!player->IsAlive())status=2;
                else if(rulesAllow){
                    const auto group=player->m_LastHitGroup;const float before=victim->v.health,beforeArmor=victim->v.armorvalue;
                    player->m_LastHitGroup=HITGROUP_GENERIC;
                    auto* oldAttack=minecraft_attack;auto* oldFinal=final_minecraft_damage;
                    minecraft_attack=victim;if(d.status_flags&2)final_minecraft_damage=victim;
                    bool accepted=false;
                    if(d.amount<0)accepted=player->TakeHealth(-d.amount,DMG_GENERIC)!=FALSE;
                    else {
                        // Poison's vanilla floor is one MC health point, or 5 CS HP.
                        // Enforce it against current native health, even after a rejected heal.
                        const float amount=(d.status_flags&4)?std::min(d.amount,std::max(0.0f,victim->v.health-5.0f)):d.amount;
                        accepted=amount==0||player->TakeDamage(&source->v,&source->v,amount,DMG_GENERIC)!=FALSE;
                    }
                    minecraft_attack=oldAttack;final_minecraft_damage=oldFinal;
                    if(!valid()||GET_PRIVATE<CBasePlayer>(victim)!=player)return;
                    status=accepted||victim->v.health!=before||victim->v.armorvalue!=beforeArmor?0u:5u;
                    player->m_LastHitGroup=group;next_snapshot=0;
                }else status=4;
            }else status=6;
        }else status=2;
        if(valid()&&GET_PRIVATE<CBasePlayer>(victim)==player){health=std::max(0.0f,victim->v.health);armor=victim->v.armorvalue;}
    }
    if(status==0)++mob_damage_accepted;else ++mob_damage_rejected;
    if(!link.connected()||link.generation()!=generation||registry.world()!=world)return;
    Writer reply;reply.u64(world);reply.u64(d.event);reply.u32(d.slot);reply.u32(d.serial);reply.u32(d.life);
    reply.u32(status);reply.f32(health);reply.f32(armor);reply.u64(tick);
    link.send(Type::damage_result,reply.data);
}
