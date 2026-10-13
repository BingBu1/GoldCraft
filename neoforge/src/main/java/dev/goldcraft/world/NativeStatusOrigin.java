package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import net.minecraft.entity.Entity;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import java.util.UUID;

/** Immutable identity; a removed caster never resolves to a reused native slot. */
public record NativeStatusOrigin(long epoch,UUID uuid,int slot,int serial,int spawn) {
    public static NativeStatusOrigin capture(Entity entity) {
        if(entity==null||!(entity.getWorld() instanceof ServerWorld))return null;
        var actor=entity instanceof NativePlayerEntity p?p.actor()
            :entity instanceof NativePlayerHitboxEntity p&&p.body()!=null?p.body().actor()
            :entity instanceof ServerPlayerEntity p?GoldCraft.HOST_WORLD.actor(p.getUuid()):null;
        return new NativeStatusOrigin(GoldCraft.HOST_WORLD.epoch(),entity.getUuid(),
            actor==null?0:actor.slot(),actor==null?0:actor.serial(),actor==null?0:actor.spawn());
    }
    public Entity resolve(ServerWorld world) {
        if(epoch!=GoldCraft.HOST_WORLD.epoch())return null;
        if(slot!=0){
            var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==slot).findFirst().orElse(null);
            if(actor==null||actor.serial()!=serial||actor.spawn()!=spawn||(actor.flags()&5)!=1)return null;
            return NativePlayers.attacker(world.getServer(),GoldCraft.HOST_WORLD,actor);
        }
        var entity=world.getEntity(uuid);
        return entity!=null&&!entity.isRemoved()&&entity.isAlive()?entity:null;
    }
}
