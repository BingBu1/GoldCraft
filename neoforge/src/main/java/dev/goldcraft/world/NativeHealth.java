package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.NativeHealthLimits;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.network.packet.s2c.play.EntityAttributesS2CPacket;
import net.minecraft.server.network.ServerPlayerEntity;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.function.Predicate;

/** CS owns current HP and class base; MC supplies only the effective attribute. */
public final class NativeHealth {
    private record Binding(long epoch,int slot,int serial,int spawn){}
    private static final IdentityHashMap<LivingEntity,Binding> owners=new IdentityHashMap<>();
    private static Predicate<LivingEntity> clientOwnership=entity->false;
    private static LivingEntity synchronizing;
    private static float synchronizedHealth;
    private static long revision;
    private NativeHealth(){}
    public static void clientOwnership(Predicate<LivingEntity> predicate){clientOwnership=predicate;}
    public static void observeClientAttribute(LivingEntity entity){
        if(!entity.getWorld().isClient)return;
        var attribute=entity.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
        if(attribute!=null)((NativeHealthAttribute)(Object)attribute).goldcraft$healthOwnership(clientOwnership.test(entity),false);
    }
    public static boolean owned(LivingEntity entity){
        if(entity.getWorld().isClient)return clientOwnership.test(entity);
        var instance=entity.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
        if(instance instanceof NativeHealthAttribute nativeAttribute&&nativeAttribute.goldcraft$healthOwned())return true;
        var binding=owners.get(entity);
        if(binding==null||!GoldCraft.healthLimitsAvailable()||binding.epoch()!=GoldCraft.HOST_WORLD.epoch())return false;
        return GoldCraft.HOST_WORLD.actors().stream().anyMatch(a->a.slot()==binding.slot()&&a.serial()==binding.serial()&&a.spawn()==binding.spawn());
    }
    public static boolean prepare(LivingEntity entity,HostWorldState.Actor actor){
        if(entity.getWorld().isClient)return false;
        if(!GoldCraft.healthLimitsAvailable()){detach(entity);return false;}
        var host=GoldCraft.HOST_WORLD;var limit=host.healthLimits().get(host,actor);
        var binding=new Binding(host.epoch(),actor.slot(),actor.serial(),actor.spawn());
        var previous=owners.get(entity);
        if(previous!=null&&!previous.equals(binding))detach(entity);
        if(limit==null)return binding.equals(owners.get(entity))&&owned(entity);
        var attribute=entity.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
        if(attribute==null)return false;
        owners.put(entity,binding);
        ((NativeHealthAttribute)(Object)attribute).goldcraft$healthOwnership(true,false);
        double nativeBase=limit.base()/MinecraftObjects.CS_HEALTH_PER_MC;
        if(attribute.getBaseValue()!=nativeBase)attribute.setBaseValue(nativeBase);
        return true;
    }
    public static void detach(LivingEntity entity){
        if(owners.remove(entity)==null)return;
        var attribute=entity.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
        if(attribute!=null)((NativeHealthAttribute)(Object)attribute).goldcraft$healthOwnership(false,true);
    }
    /** Respawn copies attribute bases before initializing HP and firing Clone. */
    public static void restoreCopiedBase(ServerPlayerEntity oldPlayer,ServerPlayerEntity replacement){
        var oldAttribute=oldPlayer.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
        if(!(oldAttribute instanceof NativeHealthAttribute original)||!original.goldcraft$healthOwned())return;
        var attribute=replacement.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
        if(attribute!=null)attribute.setBaseValue(original.goldcraft$originalHealthBase());
    }
    public static void clear(){
        for(var entity:List.copyOf(owners.keySet()))detach(entity);
        synchronizing=null;synchronizedHealth=0;revision=0;
    }
    /** Apply host HP, including rewards above the class/effect healing limit. */
    public static void synchronize(LivingEntity entity,float health){
        var before=synchronizing;float previous=synchronizedHealth;
        synchronizing=entity;synchronizedHealth=health;
        try{entity.setHealth(health);}finally{synchronizing=before;synchronizedHealth=previous;}
    }
    public static float storageMaximum(LivingEntity entity,float maximum){
        if(!owned(entity))return maximum;
        return Math.max(maximum,synchronizing==entity?Math.max(entity.getHealth(),synchronizedHealth):entity.getHealth());
    }
    public static double logicalMaximum(LivingEntity entity,double maximum){
        if(entity instanceof NativePlayerEntity body&&body.hitbox()!=null&&!body.hitbox().isRemoved()&&owned(body))
            return body.hitbox().getMaxHealth();
        return maximum;
    }
    public static void update(HostWorldState.Actor actor,LivingEntity owner,ServerPlayerEntity paired){
        if(!prepare(owner,actor)||!owner.isAlive())return;
        var host=GoldCraft.HOST_WORLD;var limit=host.healthLimits().get(host,actor);
        if(limit==null)return;
        float maximum=owner.getMaxHealth()*MinecraftObjects.CS_HEALTH_PER_MC;
        if(!NativeHealthLimits.valid(maximum))return;
        GoldCraft.sendHealthLimit(NativeHealthLimits.encode(host.epoch(),++revision,limit,maximum));
        if(paired!=null&&owner!=paired){
            // Only the client mirrors these modifiers; it does not create a
            // second effect owner or mutate the spectator's server attributes.
            paired.networkHandler.send(new EntityAttributesS2CPacket(paired.getId(),List.of(owner.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH))));
        }
    }
    /** Once per host tick, including ticks with no living native actors. */
    public static void prune(HostWorldState host){
        var iterator=owners.entrySet().iterator();
        while(iterator.hasNext()){
            var entry=iterator.next();var entity=entry.getKey();var binding=entry.getValue();
            boolean stale=entity.isRemoved()||binding.epoch()!=host.epoch()||!GoldCraft.healthLimitsAvailable()
                ||host.actors().stream().noneMatch(a->a.slot()==binding.slot()&&a.serial()==binding.serial()&&a.spawn()==binding.spawn());
            if(stale){var attribute=entity.getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH);
                if(attribute!=null)((NativeHealthAttribute)(Object)attribute).goldcraft$healthOwnership(false,true);}
            if(stale)iterator.remove();
        }
    }
}
