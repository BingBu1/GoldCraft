package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.NativeHit;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageTypes;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.math.Vec3d;

/** Apply original hurt effects only after a native-origin hit actually settled. */
public final class NativeHurt {
    private static long epoch,generation,last;
    private NativeHurt(){}
    public static void clear(){epoch=generation=last=0;}
    public static void handle(MinecraftServer server,byte[] bytes){
        if(!GoldCraft.nativeHitAvailable())throw new IllegalArgumentException("Unnegotiated native hurt");
        var hit=NativeHit.read(bytes);var host=GoldCraft.HOST_WORLD;
        if(hit.epoch()!=host.epoch())return;
        if(epoch!=hit.epoch()||generation!=GoldCraft.authorityGeneration()){
            clear();epoch=hit.epoch();generation=GoldCraft.authorityGeneration();
        }
        if(Long.compareUnsigned(hit.event(),last)<=0)return;
        last=hit.event();
        final long eventGeneration=generation;
        var actor=host.actors().stream().filter(a->a.slot()==hit.slot()&&a.serial()==hit.serial()&&a.spawn()==hit.spawn()).findFirst().orElse(null);
        if(actor==null)return;
        // Effect instances have a unique owner; form handoff may be pending until
        // the end of this incoming batch. Prefer the actual existing effect body.
        var sourceActor=host.actors().stream().filter(a->a.slot()==hit.attacker()&&a.serial()==hit.attackerSerial()&&a.spawn()==hit.attackerSpawn()).findFirst().orElse(null);
        var attacker=sourceActor==null?null:NativePlayers.attacker(server,host,sourceActor);
        var box=NativePlayers.hitbox(actor);
        LivingEntity owner=box!=null?box:server.getPlayerManager().getPlayer(actor.minecraftPlayer());
        if(owner==null||owner.isRemoved())return;
        var kind=switch(hit.kind()){
            case 1->DamageTypes.PLAYER_ATTACK;case 2->DamageTypes.ARROW;case 3->DamageTypes.EXPLOSION;
            case 4->DamageTypes.IN_FIRE;case 5->DamageTypes.FALL;case 6->DamageTypes.DROWN;case 7->DamageTypes.MAGIC;
            default->DamageTypes.GENERIC;
        };
        var entry=owner.getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(kind);
        var position=new Vec3d(hit.x()/Wire.UNITS_PER_BLOCK,hit.z()/Wire.UNITS_PER_BLOCK+Wire.Y_OFFSET,-hit.y()/Wire.UNITS_PER_BLOCK);
        var source=new DamageSource(entry,attacker,attacker,position){@Override public ItemStack getWeaponStack(){return null;}};
        for(var effect:owner.getStatusEffects().stream().map(net.minecraft.entity.effect.StatusEffectInstance::new).toList()){
            if(host.epoch()!=hit.epoch()||GoldCraft.authorityGeneration()!=eventGeneration||owner.isRemoved()
                ||host.actors().stream().noneMatch(a->a.slot()==hit.slot()&&a.serial()==hit.serial()&&a.spawn()==hit.spawn()))return;
            try(var scope=NativeStatusExecution.effect(owner,effect,false)){
                effect.onEntityDamage(owner,source,hit.amount()/MinecraftObjects.CS_HEALTH_PER_MC);
            }
        }
    }
}
