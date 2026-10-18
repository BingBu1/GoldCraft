package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.mixin.NativeRaidManagerAccessor;
import net.minecraft.advancement.criterion.Criteria;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.registry.tag.PointOfInterestTypeTags;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.stat.Stats;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.village.raid.Raid;
import net.minecraft.world.Difficulty;
import net.minecraft.world.GameRules;
import net.minecraft.world.poi.PointOfInterestStorage;

/** Native entry to the actual village raid manager; wave AI/rewards stay vanilla. */
public final class NativeRaids {
    public record Handoff(BlockPos position){}
    private NativeRaids(){}
    private static NativePlayerEntity body(LivingEntity entity){
        return entity instanceof NativePlayerEntity player?player:
            entity instanceof NativePlayerHitboxEntity box?box.body():null;
    }
    public static NativePlayerEntity activePlayer(LivingEntity entity){
        var player=body(entity);
        if(player==null||player.isRemoved()||!player.isAlive()||player.epoch()!=GoldCraft.HOST_WORLD.epoch())return null;
        var previous=player.actor();
        for(var actor:GoldCraft.HOST_WORLD.actors())if(actor.slot()==previous.slot())
            return actor.serial()==previous.serial()&&actor.spawn()==previous.spawn()&&actor.life()==previous.life()
                &&actor.life()!=0&&(actor.flags()&5)==1&&actor.health()>0&&(actor.team()==1||actor.team()==2)
                &&!actor.minecraftForm()&&player.hitbox()!=null&&NativePlayers.hitbox(actor)==player.hitbox()?player:null;
        return null;
    }
    public static boolean badOmen(NativePlayerEntity player,int amplifier){
        var world=(ServerWorld)player.getWorld();var pos=player.getBlockPos();
        if(world.getDifficulty()==Difficulty.PEACEFUL||!world.isNearOccupiedPointOfInterest(pos))return true;
        var raid=world.getRaidAt(pos);
        if(raid!=null&&raid.getBadOmenLevel()>=raid.getMaxAcceptableBadOmenLevel())return true;
        player.addStatusEffect(new StatusEffectInstance(StatusEffects.RAID_OMEN,600,amplifier));
        player.raidOrigin=pos.toImmutable();
        return false;
    }
    public static boolean raidOmen(NativePlayerEntity player){
        var pos=player.raidOrigin;
        if(pos==null)return true;
        // Clear before starting, so reentrant callbacks cannot start twice.
        player.raidOrigin=null;start(player,pos);return false;
    }
    private static Raid start(NativePlayerEntity player,BlockPos origin){
        var world=(ServerWorld)player.getWorld();
        if(world.getGameRules().getBoolean(GameRules.DISABLE_RAIDS)||!world.getDimension().hasRaids())return null;
        var occupied=world.getPointOfInterestStorage().getInCircle(type->type.isIn(PointOfInterestTypeTags.VILLAGE),
            origin,64,PointOfInterestStorage.OccupationStatus.IS_OCCUPIED).toList();
        Vec3d sum=Vec3d.ZERO;
        for(var poi:occupied){var p=poi.getPos();sum=sum.add(p.getX(),p.getY(),p.getZ());}
        var center=occupied.isEmpty()?origin:BlockPos.ofFloored(sum.multiply(1.0/occupied.size()));
        var manager=world.getRaidManager();var access=(NativeRaidManagerAccessor)manager;
        var raid=access.goldcraft$raid(world,center);
        if(!raid.hasStarted())access.goldcraft$raids().putIfAbsent(raid.getRaidId(),raid);
        var omen=player.getStatusEffect(StatusEffects.RAID_OMEN);
        if(omen!=null&&(!raid.hasStarted()||raid.getBadOmenLevel()<raid.getMaxAcceptableBadOmenLevel())){
            raid.setBadOmenLevel((int)Math.clamp((long)raid.getBadOmenLevel()+omen.getAmplifier()+1,0,raid.getMaxAcceptableBadOmenLevel()));
            // A connected paired player owns vanilla statistics/advancements;
            // an ordinary CS connection has no persistent Minecraft account.
            var paired=world.getServer().getPlayerManager().getPlayer(player.actor().minecraftPlayer());
            if(!raid.hasSpawned()&&paired!=null&&paired.getWorld()==world){
                paired.incrementStat(Stats.RAID_TRIGGER);Criteria.VOLUNTARY_EXILE.trigger(paired);
            }
        }
        manager.markDirty();return raid;
    }
    public static Handoff capture(LivingEntity from){
        if(!from.getActiveStatusEffects().containsKey(StatusEffects.RAID_OMEN))return null;
        var player=body(from);
        return new Handoff(player!=null?player.raidOrigin:from instanceof ServerPlayerEntity p?p.getStartRaidPos():null);
    }
    public static void handoff(LivingEntity from,LivingEntity to,Handoff state){
        if(state==null)return;
        clearPosition(from);
        if(to==null||!to.getActiveStatusEffects().containsKey(StatusEffects.RAID_OMEN))return;
        var player=body(to);
        if(player!=null)player.raidOrigin=state.position();
        else if(to instanceof ServerPlayerEntity paired){
            if(state.position()==null)paired.clearStartRaidPos();else paired.setStartRaidPos(state.position());
        }
    }
    public static void removed(LivingEntity entity,StatusEffectInstance effect){
        if(effect.getEffectType().equals(StatusEffects.RAID_OMEN)&&body(entity)!=null)clearPosition(entity);
    }
    public static void victory(LivingEntity hero){
        var player=activePlayer(hero);if(player==null)return;
        var paired=hero.getServer().getPlayerManager().getPlayer(player.actor().minecraftPlayer());
        if(paired!=null&&paired.getWorld()==hero.getWorld()){
            paired.incrementStat(Stats.RAID_WIN);Criteria.HERO_OF_THE_VILLAGE.trigger(paired);
        }
    }
    private static void clearPosition(LivingEntity entity){
        var player=body(entity);
        if(player!=null)player.raidOrigin=null;
        else if(entity instanceof ServerPlayerEntity paired)paired.clearStartRaidPos();
    }
    public static void resetWorld(ServerWorld world){
        var manager=world.getRaidManager();var raids=((NativeRaidManagerAccessor)manager).goldcraft$raids();
        for(var raid:raids.values())raid.invalidate();
        raids.clear();manager.markDirty();
        for(var player:world.getPlayers())player.clearStartRaidPos();
        for(var player:NativePlayers.targetingPlayers(world,java.util.List.of()))clearPosition(player);
    }
}
