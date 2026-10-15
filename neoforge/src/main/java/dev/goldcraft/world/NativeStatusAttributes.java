package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.mixin.NativeAbsorptionAccessor;
import net.minecraft.entity.EntityStatuses;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.data.DataTracker;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.item.ItemStack;
import net.minecraft.network.packet.s2c.play.EntityTrackerUpdateS2CPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.stat.Stats;
import net.minecraft.util.Hand;
import net.neoforged.neoforge.event.EventHooks;
import java.util.*;

/** Native movement/defense uses a short server lease. MC owns effects and items. */
public final class NativeStatusAttributes {
    private record Shield(ItemStack stack,Hand hand,ServerPlayerEntity player){}
    private static final class State {
        long epoch,revision,ack,processed,shieldSequence;
        int serial,spawn;
        ItemStack active=ItemStack.EMPTY;
        long activeToken;
        final ArrayDeque<byte[]> receipts=new ArrayDeque<>();
        final LinkedHashMap<Long,Shield> shields=new LinkedHashMap<>();
    }
    private static final Map<Integer,State> states=new HashMap<>();
    private static long revision;
    private NativeStatusAttributes(){}
    public static void clear(){states.clear();revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner,ServerPlayerEntity paired){
        if(!GoldCraft.statusAttributesAvailable())return;
        long epoch=GoldCraft.HOST_WORLD.epoch();State state=states.get(actor.slot());
        if(state==null||state.epoch!=epoch||state.serial!=actor.serial()||state.spawn!=actor.spawn()){
            state=new State();state.epoch=epoch;state.serial=actor.serial();state.spawn=actor.spawn();states.put(actor.slot(),state);
        }
        while(!state.receipts.isEmpty()){
            if(!GoldCraft.sendAttributesToHost(Wire.STATUS_SPEND,state.receipts.peek()))return;
            state.receipts.remove();state.ack++;
        }
        var attribute=owner.getAttributeInstance(EntityAttributes.GENERIC_MOVEMENT_SPEED);
        double speed=attribute==null||attribute.getBaseValue()<=0?1:attribute.getValue()/attribute.getBaseValue();
        float multiplier=actor.minecraftForm()?1:(float)Math.clamp(speed,0,16);
        var resistance=owner.getStatusEffect(StatusEffects.RESISTANCE);
        float reduction=resistance==null?1:Math.max(0,1-(resistance.getAmplifier()+1)*0.2f);
        boolean blocking=actor.minecraftForm()&&(actor.flags()&32)!=0&&owner==paired&&owner.isBlocking();
        float durability=0;int flags=0;
        if(blocking){
            ItemStack item=owner.getActiveItem();
            if(state.active!=item){
                state.active=item;state.activeToken=0;
                // A lower/raise cycle of the same stack must keep its token:
                // native unacknowledged durability still belongs to that item.
                for(var entry:state.shields.entrySet())if(entry.getValue().stack()==item){state.activeToken=entry.getKey();break;}
                if(state.activeToken==0)state.activeToken=++state.shieldSequence;
                state.shields.put(state.activeToken,new Shield(item,owner.getActiveHand(),paired));
                while(state.shields.size()>128)state.shields.remove(state.shields.keySet().iterator().next());
            }
            flags=1|(paired.isCreative()?2:0);
            durability=paired.isCreative()||!item.isDamageable()?10000000:Math.max(0,item.getMaxDamage()-item.getDamage());
        }else state.active=ItemStack.EMPTY;
        long next=++revision;
        byte[] packet=new Wire.Writer().i64(epoch).i64(next).i64(state.ack).i64(blocking?state.activeToken:0)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life()).i32(flags)
            .f32(multiplier).f32(reduction).f32(owner.getAbsorptionAmount()).f32(durability).toByteArray();
        if(GoldCraft.sendAttributesToHost(Wire.STATUS_ATTRIBUTES,packet))state.revision=next;
        if(paired!=null&&paired!=owner){
            // Send only the tracker entry; never create a second server absorption pool.
            paired.networkHandler.send(new EntityTrackerUpdateS2CPacket(paired.getId(),List.of(
                DataTracker.SerializedEntry.of(NativeAbsorptionAccessor.goldcraft$absorption(),owner.getAbsorptionAmount()))));
        }
    }
    public static void spend(MinecraftServer server,byte[] bytes){
        if(!GoldCraft.statusAttributesAvailable())throw new IllegalArgumentException("Unnegotiated status spend");
        var r=new Wire.Reader(bytes);long epoch=r.i64(),event=r.i64(),snapshot=r.i64(),token=r.i64();
        int slot=r.i32(),serial=r.i32(),spawn=r.i32(),life=r.i32();
        float absorbed=r.f32(),durability=r.f32(),damage=r.f32();r.finish();
        if(slot<1||slot>64||event==0||snapshot==0||life==0||absorbed<0||absorbed>100000
            ||durability<0||durability>10000000||damage<0||damage>10000000)
            throw new IllegalArgumentException("Invalid status spend");
        State state=states.get(slot);
        if(state==null||state.epoch!=epoch||epoch!=GoldCraft.HOST_WORLD.epoch()||state.serial!=serial||state.spawn!=spawn)return;
        if(Long.compareUnsigned(event,state.processed)<=0)return;
        if(event!=state.processed+1||Long.compareUnsigned(snapshot,state.revision)>0)
            throw new IllegalArgumentException("Out-of-order status spend");
        var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==slot&&a.serial()==serial&&a.spawn()==spawn).findFirst().orElse(null);
        if(actor==null)return;
        // Native life may have advanced on a form switch. The spawn pool is still
        // the same; consume from its current owner, never the removed old hitbox.
        var box=NativePlayers.hitbox(actor);
        LivingEntity owner=box!=null?box:server.getPlayerManager().getPlayer(actor.minecraftPlayer());
        if(owner==null||!owner.isAlive())return;
        // MC damage may have spent the same last hearts before this native
        // reservation arrived. Report actual payment; native settles the deficit.
        float paid=Math.min(absorbed,owner.getAbsorptionAmount());
        if(paid>0)owner.setAbsorptionAmount(owner.getAbsorptionAmount()-paid);
        if(damage>0){
            Shield shield=state.shields.get(token);
            if(shield!=null&&!shield.stack().isEmpty()&&shield.player().isAlive()){
                var player=shield.player();var stack=shield.stack();
                player.incrementStat(Stats.USED.getOrCreateStat(stack.getItem()));
                player.getWorld().sendEntityStatus(player,EntityStatuses.BLOCK_WITH_SHIELD);
                if(durability>0&&!player.isCreative()){
                    // Retain the hit's actual stack even if use was released while
                    // this message crossed processes; never damage a replacement.
                    var original=stack.copy();
                    stack.hurtAndBreak((int)durability,player.getServerWorld(),player,item->{
                        player.sendEquipmentBreakStatus(item,shield.hand()==Hand.OFF_HAND?EquipmentSlot.OFFHAND:EquipmentSlot.MAINHAND);
                        EventHooks.onPlayerDestroyItem(player,original,shield.hand());
                        if(player.getActiveItem()==stack)player.clearActiveItem();
                    });
                }
            }
        }
        state.processed=event;
        state.receipts.add(new Wire.Writer().i64(epoch).i64(event).i32(slot).i32(serial).i32(spawn).f32(paid).toByteArray());
    }
}
