package dev.goldcraft.test;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.world.*;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.*;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import java.util.UUID;

/** These tests execute real woven vanilla callbacks; the sink is not a native server. */
@GameTestHolder("goldcraft_status_tests")
@PrefixGameTestTemplate(false)
public final class NativeStatusTest {
    private static final class Fixture {
        final NativePlayerEntity body;
        final NativePlayerHitboxEntity box;
        long sequence;int applied;float damage;
        Fixture(TestContext context){
            var actor=new HostWorldState.Actor(1,4,7,2,1,8,100,0,new HostWorldState.Vector(0,0,36),
                new HostWorldState.Vector(0,0,0),0,0,new HostWorldState.Vector(-16,-16,-36),
                new HostWorldState.Vector(16,16,36),new UUID(0,0),5,0);
            body=new NativePlayerEntity(context.getWorld(),1,actor,(target,source,amount)->{
                context.assertTrue(NativeStatusExecution.current(target.hitbox())!=null,"Actual effect callback did not open status scope");
                damage+=amount;applied++;long id=++sequence;target.healthLedger().sent(id,-amount);
                // Only this callback fixture acknowledges locally. Production does not.
                target.healthLedger().receipt(id,target.predictedHealth(),id);return true;
            });
            box=new NativePlayerHitboxEntity(NativePlayerHitboxEntity.TYPE.get(),context.getWorld());box.follow(body);
        }
        void ticks(int n){for(int i=0;i<n;++i)box.tick();}
        void close(){NativeStatusEffects.transfer(box,null);box.discard();body.discard();}
    }
    @GameTest(templateName="goldcraft_tests:empty",batchId="native_status_poison",tickLimit=20)
    public void poisonUsesRealTimerAndKeepsNonlethalFloor(TestContext context){
        var f=new Fixture(context);
        try {
            f.box.addStatusEffect(new StatusEffectInstance(StatusEffects.POISON,500,4));
            f.ticks(500);
            context.assertTrue(f.applied>0&&f.damage==19&&f.box.getHealth()==1,"Vanilla poison did not reach exactly one HP through pending health");
            context.assertTrue(f.box.getStatusEffects().isEmpty(),"Poison duration failed to expire");
            context.assertTrue(f.box.isInvisible()&&NativeStatusExecution.current(f.box)==null,"Effect tick leaked visibility or callback scope");
        }finally{f.close();}
        context.complete();
    }
    private static final class CounterEffect extends StatusEffect {
        int starts;
        CounterEffect(){super(StatusEffectCategory.HARMFUL,0xff0000);}
        @Override public void onApplied(LivingEntity target,int amplifier){starts++;target.damage(target.getDamageSources().generic(),2);}
    }
    @GameTest(templateName="goldcraft_tests:empty",batchId="native_status_transfer",tickLimit=20)
    public void transferKeepsHiddenOriginAndDoesNotReplayOnApplied(TestContext context){
        var first=new Fixture(context);var second=new Fixture(context);
        var caster=EntityType.ZOMBIE.create(context.getWorld());
        try {
            var callback=new CounterEffect();RegistryEntry<StatusEffect> type=RegistryEntry.of(callback);
            first.box.addStatusEffect(new StatusEffectInstance(type,200),caster);
            context.assertTrue(callback.starts==1&&first.applied==1,"Application callback did not execute");
            NativeStatusEffects.transfer(first.box,second.box);
            context.assertTrue(callback.starts==1&&second.applied==0&&first.box.getStatusEffects().isEmpty(),"Transfer replayed callback or kept two owners");
            first.box.addStatusEffect(new StatusEffectInstance(StatusEffects.WITHER,100,0),caster);
            var origin=((NativeStatusInstance)(Object)first.box.getStatusEffect(StatusEffects.WITHER)).goldcraft$statusOrigin();
            first.box.addStatusEffect(new StatusEffectInstance(StatusEffects.WITHER,2,1),null);
            NativeStatusEffects.transfer(first.box,second.box);second.ticks(2);
            var restored=second.box.getStatusEffect(StatusEffects.WITHER);
            context.assertTrue(restored!=null&&restored.getAmplifier()==0&&restored.getDuration()==98,"Hidden effect/duration was lost in transfer");
            context.assertTrue(origin.equals(((NativeStatusInstance)(Object)restored).goldcraft$statusOrigin()),"Hidden effect adopted the stronger effect's caster");
            context.assertTrue(NativeStatusExecution.current(second.box)==null,"Transfer/tick leaked effect scope");
        }finally{first.close();second.close();caster.discard();}
        context.complete();
    }
    @GameTest(templateName="goldcraft_tests:empty",batchId="native_status_attributes",tickLimit=20)
    public void attributesAndAbsorptionTransferUseActualEffects(TestContext context){
        var first=new Fixture(context);var second=new Fixture(context);
        var movement=net.minecraft.entity.attribute.EntityAttributes.GENERIC_MOVEMENT_SPEED;
        try {
            double base=first.box.getAttributeValue(movement);
            first.box.addStatusEffect(new StatusEffectInstance(StatusEffects.SLOWNESS,40,0));
            context.assertTrue(Math.abs(first.box.getAttributeValue(movement)/base-.85)<.0001,"Slowness attribute missing");
            first.box.clearStatusEffects();
            first.box.addStatusEffect(new StatusEffectInstance(StatusEffects.SPEED,40,1));
            context.assertTrue(Math.abs(first.box.getAttributeValue(movement)/base-1.4)<.0001,"Speed II attribute missing");
            first.ticks(40);
            context.assertTrue(Math.abs(first.box.getAttributeValue(movement)-base)<.0001,"Expired speed did not restore base");
            first.box.addStatusEffect(new StatusEffectInstance(StatusEffects.ABSORPTION,200,1));
            context.assertTrue(first.box.getAbsorptionAmount()==8,"Actual absorption application missing");
            context.assertTrue(first.box.damage(first.box.getDamageSources().generic(),2),"Fully absorbed hit lost its normal hit feedback");
            context.assertTrue(first.box.getAbsorptionAmount()==6&&first.applied==0,"Absorbed hit debited native health or wrong pool");
            first.box.setAbsorptionAmount(3);
            NativeStatusEffects.transfer(first.box,second.box);
            context.assertTrue(first.box.getAbsorptionAmount()==0&&second.box.getAbsorptionAmount()==3,"Transfer refilled or duplicated absorption");
            NativeStatusEffects.transfer(second.box,first.box);
            context.assertTrue(second.box.getAbsorptionAmount()==0&&first.box.getAbsorptionAmount()==3,"Return transfer changed spent pool");
            first.box.clearStatusEffects();first.ticks(1);
            context.assertTrue(first.box.getAbsorptionAmount()==0,"Removing absorption left extra health");
        }finally{first.close();second.close();}
        context.complete();
    }

}
