// Included in bridge.cpp. Only the authenticated Minecraft server can send
// negotiated TNT hurt calls; ordinary mob packets retain their source rules.
void HandleTntDamage(const MobDamage& d){
    if(!link.tnt_damage())throw ProtocolError("Unnegotiated TNT damage");
    if(d.epoch!=registry.world()||d.event<=mob_damage_event){++mob_damage_rejected;return;}
    mob_damage_event=d.event;
    const auto generation=link.generation(),world=registry.world();
    auto* const table=edicts;
    unsigned status=1;float health=0,armor=0;
    const auto session=[&](){return edicts==table&&link.connected()&&link.generation()==generation&&registry.world()==world;};
    const auto current=[&](){const auto* binding=registry.get(d.slot);
        return session()&&binding&&binding->serial==d.serial&&player_births[d.slot]==d.life&&!table[d.slot].free;};
    if(d.slot<=static_cast<unsigned>(max_clients)&&current()){
        auto* victim=table+d.slot;auto* player=GET_PRIVATE<CBasePlayer>(victim);const int victimSerial=victim->serialnumber;
        const auto targetCurrent=[&](){return current()&&victim->serialnumber==victimSerial&&GET_PRIVATE<CBasePlayer>(victim)==player;};
        auto* source=d.attacker?nullptr:d.source_key?GoldCraft_MobObject(d.source_key):table;
        if(d.attacker&&d.attacker<=static_cast<unsigned>(max_clients)){
            const auto* binding=registry.get(d.attacker);
            if(binding&&binding->serial==d.attacker_serial&&player_births[d.attacker]==d.attacker_spawn
                &&!table[d.attacker].free&&GET_PRIVATE<CBasePlayer>(table+d.attacker))source=table+d.attacker;
        }
        if(player&&player->IsAlive()&&source){
            const int sourceSerial=source->serialnumber;auto* sourceEntity=GET_PRIVATE<CBaseEntity>(source);
            const auto sourceCurrent=[&](){
                if(!session()||source->free||source->serialnumber!=sourceSerial||GET_PRIVATE<CBaseEntity>(source)!=sourceEntity)return false;
                if(d.attacker){const auto* binding=registry.get(d.attacker);
                    return binding&&binding->serial==d.attacker_serial&&player_births[d.attacker]==d.attacker_spawn;}
                return d.source_key?GoldCraft_MobObject(d.source_key)==source:source==table;
            };
            const Vector point(d.source.x,d.source.y,d.source.z);
            const Vector center=victim->v.origin+(victim->v.mins+victim->v.maxs)*0.5f;
            if(!skill_damage_snapshots.matches(d,gpGlobals->time,{center.x,center.y,center.z}))status=6;
            else if((point-center).LengthSquared()>384.0f*384.0f)status=3;
            else {
                // Ignore only this target hull. A TNT center inside the target
                // may hurt it, while host geometry/other entities still occlude.
                TraceResult trace;UTIL_TraceLine(point,center,dont_ignore_monsters,victim,&trace);
                const bool permitted=!trace.fStartSolid&&trace.flFraction>=0.999f
                    &&g_pGameRules->FPlayerCanTakeDamage(player,sourceEntity);
                if(!targetCurrent())return;
                if(!sourceCurrent())status=2;
                else if(!permitted)status=4;
                else if(NUMBER_OF_ENTITIES()>=gpGlobals->maxEntities-64)status=5;
                else {
                    // Give native damage hooks and death notices a real TNT
                    // inflictor at the explosion center, separate from its owner.
                    auto* inflictor=GetClassPtr<CCSEntity>(static_cast<CBaseEntity*>(nullptr));
                    if(!inflictor)status=5;
                    else {
                        auto* inflictorEdict=inflictor->edict();const int inflictorSerial=inflictorEdict->serialnumber;
                        inflictor->pev->classname=MAKE_STRING("goldcraft_tnt");
                        inflictor->pev->movetype=MOVETYPE_NONE;inflictor->pev->solid=SOLID_NOT;
                        inflictor->pev->takedamage=DAMAGE_NO;inflictor->pev->owner=source;
                        UTIL_SetOrigin(inflictor->pev,point);
                        const int group=player->m_LastHitGroup;
                        const float before=victim->v.health,beforeArmor=victim->v.armorvalue;
                        player->m_LastHitGroup=HITGROUP_GENERIC;
                        auto* previous=minecraft_attack;minecraft_attack=victim;
                        const bool accepted=player->TakeDamage(inflictor->pev,&source->v,d.amount,DMG_BLAST)!=FALSE;
                        minecraft_attack=session()?previous:nullptr;
                        if(session()&&!inflictorEdict->free&&inflictorEdict->serialnumber==inflictorSerial
                            &&GET_PRIVATE<CBaseEntity>(inflictorEdict)==inflictor)UTIL_Remove(inflictor);
                        if(!targetCurrent())return;
                        status=accepted||victim->v.health<before||victim->v.armorvalue<beforeArmor?0u:5u;
                        player->m_LastHitGroup=group;next_snapshot=0;
                    }
                }
            }
        }else status=2;
        if(targetCurrent()){health=std::max(0.0f,victim->v.health);armor=victim->v.armorvalue;}
    }
    if(status==0)++mob_damage_accepted;else ++mob_damage_rejected;
    if(!session())return;
    Writer reply;reply.u64(world);reply.u64(d.event);reply.u32(d.slot);reply.u32(d.serial);reply.u32(d.life);
    reply.u32(status);reply.f32(health);reply.f32(armor);link.send(Type::damage_result,reply.data);
}
