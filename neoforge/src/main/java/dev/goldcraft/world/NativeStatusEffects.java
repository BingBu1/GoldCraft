package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.mixin.NativeStatusLifecycleAccessor;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffect;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.network.packet.s2c.play.EntityStatusEffectS2CPacket;
import net.minecraft.network.packet.s2c.play.RemoveEntityStatusEffectS2CPacket;
import net.minecraft.registry.Registries;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.server.network.ServerPlayerEntity;
import java.util.*;

/** One effect map/ticker per native spawn; client HUD copies never enter a server map. */
public final class NativeStatusEffects {
    private record Mirror(ServerPlayerEntity player,Set<RegistryEntry<StatusEffect>> types){}
    private static final Map<Integer,Mirror> mirrors=new HashMap<>();
    private record Handoff(long epoch,int serial,int spawn,dev.goldcraft.bridge.NativeHealthLedger ledger){}
    private static final Map<UUID,Handoff> handoffs=new HashMap<>();
    private static long revision;
    private NativeStatusEffects(){}
    public static LivingEntity routed(LivingEntity entity){
        if(entity.getWorld().isClient)return entity;
        if(entity instanceof NativePlayerEntity body&&body.hitbox()!=null)return body.hitbox();
        if(entity instanceof ServerPlayerEntity player){
            var actor=GoldCraft.HOST_WORLD.actor(player.getUuid());
            if(actor!=null){var proxy=NativePlayers.hitbox(actor);if(proxy!=null)return proxy;}
        }
        return entity;
    }
    public static void transfer(LivingEntity from,LivingEntity to){
        if(from==null||from==to)return;
        if(from instanceof NativePlayerHitboxEntity box&&to instanceof ServerPlayerEntity player){
            var actor=box.body().actor();
            if(!box.body().healthLedger().settled(GoldCraft.HOST_WORLD.tick()))
                handoffs.put(player.getUuid(),new Handoff(box.body().epoch(),actor.serial(),actor.spawn(),box.body().healthLedger()));
        }
        float absorption=from.getAbsorptionAmount();
        boolean moveAbsorption=absorption>0||from.getAttributeValue(net.minecraft.entity.attribute.EntityAttributes.GENERIC_MAX_ABSORPTION)>0;
        // Move the instances themselves: the copy constructor loses hidden effects.
        // Lifecycle hooks migrate attributes/packets, never replay combat onApplied.
        for(var effect:List.copyOf(from.getActiveStatusEffects().values())){
            from.getActiveStatusEffects().remove(effect.getEffectType());
            ((NativeStatusLifecycleAccessor)from).goldcraft$statusRemoved(effect);
            if(to!=null){
                var displaced=to.getActiveStatusEffects().put(effect.getEffectType(),effect);
                if(displaced!=null)((NativeStatusLifecycleAccessor)to).goldcraft$statusRemoved(displaced);
                ((NativeStatusLifecycleAccessor)to).goldcraft$statusAdded(effect,null);
            }
        }
        if(moveAbsorption){
            from.setAbsorptionAmount(0);
            if(to!=null)to.setAbsorptionAmount(absorption);
        }
    }
    public static void hud(HostWorldState.Actor actor,LivingEntity owner,ServerPlayerEntity paired,long tick){
        NativeStatusAttributes.update(actor,owner,paired);
        if(tick%10!=0)return;
        var previous=mirrors.get(actor.slot());
        if(previous!=null&&previous.player()!=paired)clearMirror(actor.slot());
        if(paired!=null&&owner!=paired){
            Set<RegistryEntry<StatusEffect>> next=new HashSet<>();
            for(var effect:owner.getStatusEffects()){
                next.add(effect.getEffectType());
                paired.networkHandler.send(new EntityStatusEffectS2CPacket(paired.getId(),effect,false));
            }
            previous=mirrors.put(actor.slot(),new Mirror(paired,next));
            if(previous!=null)for(var type:previous.types())if(!next.contains(type))
                paired.networkHandler.send(new RemoveEntityStatusEffectS2CPacket(paired.getId(),type));
        }else if(previous!=null){
            // The real player now owns these effects; do not erase its live HUD.
            for(var type:previous.types())if(!owner.hasStatusEffect(type))
                previous.player().networkHandler.send(new RemoveEntityStatusEffectS2CPacket(previous.player().getId(),type));
            mirrors.remove(actor.slot());
        }
        if(!GoldCraft.statusEffectsAvailable())return;
        var entries=new ArrayList<dev.goldcraft.bridge.NativeStatusHud.Entry>();
        if(paired==null)for(var effect:owner.getStatusEffects()){
            if(!effect.shouldShowIcon())continue;
            String id=Registries.STATUS_EFFECT.getId(effect.getEffectType().value()).toString();
            entries.add(new dev.goldcraft.bridge.NativeStatusHud.Entry(id,effect.getAmplifier(),effect.getDuration()));
            if(entries.size()==32)break;
        }
        // Empty packets actively clear the native fallback when MC HUD takes over.
        GoldCraft.sendToHost(76,dev.goldcraft.bridge.NativeStatusHud.encode(GoldCraft.HOST_WORLD.epoch(),++revision,
            actor.slot(),actor.serial(),actor.spawn(),entries));
    }
    public static void clearMirror(int slot){
        var previous=mirrors.remove(slot);if(previous==null)return;
        for(var type:previous.types())previous.player().networkHandler.send(new RemoveEntityStatusEffectS2CPacket(previous.player().getId(),type));
    }
    public static boolean waitingForHealth(LivingEntity entity){
        if(!(entity instanceof ServerPlayerEntity player))return false;
        var wait=handoffs.get(player.getUuid());if(wait==null)return false;
        var actor=GoldCraft.HOST_WORLD.actor(player.getUuid());
        if(actor==null||wait.epoch()!=GoldCraft.HOST_WORLD.epoch()||wait.serial()!=actor.serial()||wait.spawn()!=actor.spawn()
            ||wait.ledger().settled(GoldCraft.HOST_WORLD.tick())){handoffs.remove(player.getUuid());return false;}
        return true;
    }
    public static void clear(){NativeStatusAttributes.clear();for(int slot:List.copyOf(mirrors.keySet()))clearMirror(slot);handoffs.clear();revision=0;}
}
