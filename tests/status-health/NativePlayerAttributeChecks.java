package dev.goldcraft.healthprobe;

import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import static dev.goldcraft.healthprobe.NativeHealthProbe.*;
import dev.goldcraft.world.*;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.GameType;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.living.LivingDropsEvent;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/** Actual vanilla entity-death loot uses the native attacker's Player#getLuck. */
final class NativePlayerAttributeChecks {
    static void loot(NativePlayerEntity attacker,Item expected)throws Exception {
        var cow=EntityType.COW.create(world);check(cow!=null,"real loot victim created");
        cow.setPos(0,70,0);world.addFreshEntity(cow);
        var drops=new ArrayList<Item>();var seen=new int[1];
        java.util.function.Consumer<LivingDropsEvent> listener=e->{
            if(e.getEntity()!=cow)return;
            check(e.getSource().getEntity()==attacker&&e.isRecentlyHit(),"actual death retains logical Player attacker and player-hit flag");
            for(ItemEntity item:e.getDrops())drops.add(item.getItem().getItem());
            seen[0]++;
        };
        NeoForge.EVENT_BUS.addListener(listener);
        try{check(cow.hurt(world.damageSources().playerAttack(attacker),100),"actual native Player attack kills loot victim");}
        finally{NeoForge.EVENT_BUS.unregister(listener);}
        check(seen[0]==1,"actual vanilla death emits one loot event");
        check(drops.equals(expected==null?List.of():List.of(expected)),"actual loot table consumes luck: expected="+expected+" actual="+drops);
        for(var e:world.getAllEntities())if(e instanceof ItemEntity)e.discard();
    }
    static void run()throws Exception {
        fresh();snapshot(actor(NONE,3,7,11,1,100,0),100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(host.actors().getFirst());var body=box.body();
        check(box.getAttribute(Attributes.LUCK)!=null,"unique effect owner has Player luck attribute");
        check(box.getAttribute(Attributes.ATTACK_DAMAGE)!=null&&box.getAttribute(Attributes.ATTACK_SPEED)!=null,"unique effect owner has Player combat attributes");
        near(body.getLuck(),0,"native Player starts with vanilla luck");loot(body,null);
        box.addEffect(new MobEffectInstance(MobEffects.LUCK,3,1));near(body.getLuck(),2,"logical Player reads effective luck");loot(body,Items.GOLD_INGOT);
        step(box,3);near(body.getLuck(),0,"luck expiry restores vanilla context");loot(body,null);
        box.addEffect(new MobEffectInstance(MobEffects.UNLUCK,100,2));near(body.getLuck(),-3,"logical Player reads negative luck");loot(body,Items.DIAMOND);
        box.removeEffect(MobEffects.UNLUCK);box.addEffect(new MobEffectInstance(MobEffects.DAMAGE_BOOST,100,1));
        near(body.getAttributeValue(Attributes.ATTACK_DAMAGE),7,"strength modifier is retained on the effective Player attribute");
        box.addEffect(new MobEffectInstance(MobEffects.WEAKNESS,100,0));near(body.getAttributeValue(Attributes.ATTACK_DAMAGE),3,"weakness combines once with strength");
        near(body.getAttributeBaseValue(Attributes.ATTACK_DAMAGE),1,"effective modifier does not rewrite base attribute");
        box.removeAllEffects();near(body.getAttributeValue(Attributes.ATTACK_DAMAGE),1,"effect clear restores combat base");
        fresh();var p=player(new UUID(0,9080));snapshot(actor(p.getUUID(),3,7,11,113,100,0),100);
        SharedVitals.prepare(p,host,host.actors().getFirst());NativePlayers.tick(server,host);
        var luck=new MobEffectInstance(MobEffects.LUCK,200,1);p.addEffect(luck);near(p.getLuck(),2,"paired MC form starts with luck");
        snapshot(actor(p.getUUID(),3,7,12,17,100,0),100);HostSession.hold(p);NativePlayers.tick(server,host);
        box=NativePlayers.hitbox(host.actors().getFirst());body=box.body();
        check(box.getEffect(MobEffects.LUCK)==luck&&p.getActiveEffects().isEmpty(),"MC to CS transfers one luck effect instance");
        near(body.getLuck(),2,"CS loot actor uses transferred luck");loot(body,Items.GOLD_INGOT);
        step(box,1);check(luck.getDuration()==199,"transferred luck has one ticker");
        snapshot(actor(p.getUUID(),3,7,13,113,100,0),100);p.setGameMode(GameType.SURVIVAL);NativePlayers.tick(server,host);
        check(p.getEffect(MobEffects.LUCK)==luck&&box.isRemoved(),"CS to MC returns the same luck effect");near(p.getLuck(),2,"MC attribute restores after handoff");
        near(body.getLuck(),0,"retired logical Player no longer reads removed hitbox effects");
    }
}
