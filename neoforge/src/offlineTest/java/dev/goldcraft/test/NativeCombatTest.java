package dev.goldcraft.test;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.NativePlayerEntity;
import dev.goldcraft.world.NativePlayers;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageTypes;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.Difficulty;
import java.util.UUID;

/** Real vanilla AI/physics; the bite sink records a request, never pretends to be ReHLDS health. */
@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class NativeCombatTest {
    private static HostWorldState.Actor actor(int serial,int life,int flags,float health){
        return new HostWorldState.Actor(1,serial,7,2,flags,life,health,0,
            new HostWorldState.Vector(14.5f*32,-12.5f*32,44),new HostWorldState.Vector(0,0,0),0,0,
            new HostWorldState.Vector(-16,-16,-36),new HostWorldState.Vector(16,16,36),new UUID(0,0),life,0);
    }
    private static void snapshot(HostWorldState host,long tick,HostWorldState.Actor... actors){
        var w=new Wire.Writer().i64(host.epoch()).i64(tick).f32(tick/20f).i32(0).i32(actors.length);
        for(var a:actors){
            w.i32(a.slot()).i32(a.serial()).i32(a.userid()).i32(a.team()).i32(a.flags()).i32(a.life()).f32(a.health()).f32(a.armor());
            for(var v:new HostWorldState.Vector[]{a.origin(),a.velocity()})w.f32(v.x()).f32(v.y()).f32(v.z());
            w.f32(a.pitch()).f32(a.yaw());
            for(var v:new HostWorldState.Vector[]{a.mins(),a.maxs()})w.f32(v.x()).f32(v.y()).f32(v.z());
            w.bytes(Wire.uuid(a.minecraftPlayer())).i32(a.spawn()).i64(a.vitalsAck());
        }
        host.actors(w.toByteArray());
    }
    @GameTest(templateName="empty",batchId="native_revenge",tickLimit=400)
    public void wolfNaturallyRetaliatesChasesAndBitesNativeBody(TestContext context){
        NativePlayers.clear();var f=NavigationTest.fixture(context,"nav_fixture");var world=f.world();
        world.getServer().setDifficulty(Difficulty.NORMAL,true);
        int[] bites={0};float[] damage={0};
        NativePlayerEntity body=new NativePlayerEntity(world,1,actor(4,8,1,100),(target,source,amount)->{
            bites[0]++;damage[0]+=amount;return true;
        });
        world.spawnEntity(body);
        var wolf=EntityType.WOLF.create(world);Vec3d start=new Vec3d(4.5,64.25,12.5);
        wolf.refreshPositionAndAngles(start.x,start.y,start.z,0,0);wolf.setPersistent();world.spawnEntity(wolf);
        boolean[] shot={false},targeted={false};
        context.runAtEveryTick(()->{
            if(!shot[0]&&wolf.age>=20&&wolf.isOnGround()){
                var source=new DamageSource(world.getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(DamageTypes.ARROW),body);
                context.assertTrue(wolf.damage(source,1),"Wolf rejected native player attack");shot[0]=true;
            }
            targeted[0]|=wolf.getTarget()==body&&wolf.hasAngerTime();
        });
        context.addInstantFinalTask(()->{
            context.assertTrue(targeted[0],"Vanilla RevengeGoal did not retain native attacker");
            context.assertTrue(wolf.getPos().distanceTo(start)>3,"Wolf did not autonomously chase over BSP floor");
            context.assertTrue(bites[0]>0&&damage[0]>0,"Wolf did not reach native damage sink; position="+wolf.getPos());
            context.assertTrue(body.getHealth()==20,"MC must not subtract native authoritative health");
            wolf.discard();body.discard();
        });
    }
    @GameTest(templateName="empty",batchId="native_hostile",tickLimit=350)
    public void hostileNaturallyTargetsUnpairedCsPlayer(TestContext context){
        NativePlayers.clear();var f=NavigationTest.fixture(context,"nav_fixture");var world=f.world();
        world.getServer().setDifficulty(Difficulty.NORMAL,true);world.setTimeOfDay(18000);
        snapshot(f.host(),1,actor(4,8,1,100));NativePlayers.tick(world.getServer(),f.host());
        var body=NativePlayers.attacker(world.getServer(),f.host(),f.host().actors().getFirst());
        context.assertTrue(body instanceof NativePlayerEntity&&!body.isSpectator(),"Unpaired CS player lacks targetable body");
        context.assertTrue(world.getPlayers().isEmpty(),"AI body must not join the network-player list");
        var zombie=EntityType.ZOMBIE.create(world);Vec3d start=new Vec3d(5.5,64.25,12.5);
        zombie.refreshPositionAndAngles(start.x,start.y,start.z,0,0);zombie.setPersistent();world.spawnEntity(zombie);
        context.addInstantFinalTask(()->{
            var predicate=net.minecraft.entity.ai.TargetPredicate.createAttackable().setBaseMaxDistance(35);
            context.assertTrue(zombie.getTarget()==body,"Vanilla hostile AI cannot find CS player; age="+zombie.age+" removed="+zombie.isRemoved()+" bodyRemoved="+body.isRemoved()+" predicate="+predicate.test(zombie,body)+" sees="+zombie.getVisibilityCache().canSee(body)+" lookup="+(world.getClosestPlayer(predicate,zombie,zombie.getX(),zombie.getEyeY(),zombie.getZ())==body));
            context.assertTrue(zombie.getPos().distanceTo(start)>3&&zombie.squaredDistanceTo(body)<9,"Hostile did not chase native target; position="+zombie.getPos());
            zombie.discard();NativePlayers.clear();
        });
    }
    @GameTest(templateName="empty",batchId="native_lifecycle",tickLimit=80)
    public void nativeTargetLifecycleRejectsDeadAndReusedConnections(TestContext context){
        NativePlayers.clear();var f=NavigationTest.fixture(context,"nav_fixture");var server=f.world().getServer();
        snapshot(f.host(),1,actor(4,8,1,100));NativePlayers.tick(server,f.host());
        var first=NativePlayers.attacker(server,f.host(),f.host().actors().getFirst());
        context.assertTrue(f.world().getPlayerByUuid(first.getUuid())==first,"Native UUID lookup missing from targeting");
        snapshot(f.host(),2,actor(5,1,1,100));NativePlayers.tick(server,f.host());
        var replacement=NativePlayers.attacker(server,f.host(),f.host().actors().getFirst());
        context.assertTrue(first.isRemoved()&&!first.getUuid().equals(replacement.getUuid()),"Reused CS slot retained old identity");
        context.assertTrue(f.world().getPlayerByUuid(first.getUuid())==null,"Old native body remains targetable");
        snapshot(f.host(),3,actor(5,1,0,0));NativePlayers.tick(server,f.host());
        context.assertTrue(replacement.isRemoved()&&f.world().getPlayerByUuid(replacement.getUuid())==null,"Native death leaves a target");
        NativePlayers.clear();context.complete();
    }
    @GameTest(templateName="empty",batchId="native_cooldown",tickLimit=80)
    public void nativeDamageCooldownPreservesAuthority(TestContext context){
        NativePlayers.clear();var f=NavigationTest.fixture(context,"nav_fixture");var world=f.world();
        world.getServer().setDifficulty(Difficulty.NORMAL,true);
        float[] requested={0};int[] calls={0};
        var body=new NativePlayerEntity(world,1,actor(4,8,1,100),(target,source,amount)->{requested[0]+=amount;calls[0]++;return true;});
        var wolf=EntityType.WOLF.create(world);var source=world.getDamageSources().mobAttack(wolf);
        context.assertTrue(body.damage(source,3)&&!body.damage(source,3),"Equal same-tick bites bypass cooldown");
        context.assertTrue(body.damage(source,5)&&calls[0]==2&&requested[0]==5,"Stronger bite must request only its damage difference");
        context.assertTrue(body.getHealth()==20,"Unconfirmed bite changed native health locally");
        for(int i=0;i<10;i++)body.tick();
        context.assertTrue(body.damage(source,3)&&requested[0]==8,"Cooldown never expires");
        body.update(actor(4,8,1,70));context.assertTrue(body.getHealth()==14,"Native health acknowledgement not applied");
        body.discard();wolf.discard();context.complete();
    }
}
