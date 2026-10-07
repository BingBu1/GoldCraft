package dev.goldcraft.test;

import com.google.gson.JsonParser;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.BspMap;
import dev.goldcraft.world.HostCollision;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.ai.pathing.LandPathNodeMaker;
import net.minecraft.entity.ai.pathing.PathContext;
import net.minecraft.entity.ai.pathing.PathNodeType;
import net.minecraft.entity.passive.PigEntity;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.chunk.ChunkCache;
import java.nio.file.Files;
import java.nio.file.Path;

@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class NavigationTest {
    record Fixture(ServerWorld world,HostWorldState host){}
    static Fixture fixture(TestContext context,String map) {
        try {
            Path root=Path.of(System.getProperty("goldcraft.test.root"));
            var entry=JsonParser.parseString(Files.readString(Path.of(System.getProperty("goldcraft.test.fixtures",root.resolve("sandbox/neoforge-offline-tests/fixtures.json").toString())))).getAsJsonObject().getAsJsonObject(map);
            var key=RegistryKey.of(RegistryKeys.WORLD,Identifier.of(entry.get("dimension").getAsString()));
            ServerWorld world=context.getWorld().getServer().getWorld(key);
            context.assertTrue(world!=null,"Offline host dimension missing");
            byte[] bytes=Files.readAllBytes(Path.of(entry.get("path").getAsString()));
            HostWorldState host=new HostWorldState();
            host.world(new Wire.Writer().i64(1).string(map).toByteArray());
            host.bsp(new Wire.Writer().i64(1).i32((int)BspMap.checksum(bytes)).i32(bytes.length).bytes(bytes).toByteArray());
            HostCollision.attach(world,host);
            for(int x=-1;x<=4;x++)for(int z=-1;z<=2;z++){world.setChunkForced(x,z,true);world.getChunk(x,z);}
            world.resetIdleTimeout();
            for(var e:world.iterateEntities())if(!(e instanceof net.minecraft.server.network.ServerPlayerEntity))e.discard();
            return new Fixture(world,host);
        }catch(Exception e){throw new RuntimeException(e);}
    }
    private static PigEntity pig(ServerWorld world,Vec3d pos){
        PigEntity pig=EntityType.PIG.create(world);
        // Give navigation one fixed destination; random wandering would replace
        // the test route. Physics and vanilla navigation remain enabled.
        pig.clearGoalsAndTasks();
        pig.refreshPositionAndAngles(pos.x,pos.y,pos.z,0,0);pig.setPersistent();
        world.spawnEntity(pig);return pig;
    }
    private static void route(TestContext context,Fixture fixture,Vec3d start,Vec3d goal,boolean detour){
        PigEntity pig=pig(fixture.world,start);double[] travelled={0};Vec3d[] previous={start};boolean[] away={false};
        boolean[] started={false};
        context.runAtEveryTick(()->{
            if(pig.age<10||!pig.isOnGround()||started[0]&&(!pig.getNavigation().isIdle()||pig.squaredDistanceTo(goal)<0.65))return;
            started[0]=true;
            var path=pig.getNavigation().findPathTo(goal.x,goal.y,goal.z,0);
            StringBuilder nodes=new StringBuilder();
            if(path!=null)for(int i=0;i<path.getLength();i++){var n=path.getNode(i);nodes.append(n).append('/').append(n.type).append('/').append(n.pathLength).append(' ');}
            // Vanilla returns partial paths when its follow-range budget expires.
            // Like a pursuit goal, continue from the last node until physically at the target.
            context.assertTrue(path!=null&&path.getLength()>1,"Vanilla A* cannot advance toward supported host target; start="+pig.getPos()+" goal="+goal+" nodes="+nodes);
            context.assertTrue(pig.getNavigation().startMovingAlong(path,1),"Vanilla navigation failed to start");
        });
        context.runAtEveryTick(()->{
            if(pig.isRemoved())return;
            travelled[0]+=pig.getPos().distanceTo(previous[0]);previous[0]=pig.getPos();
            away[0]|=Math.abs(pig.getZ()-start.z)>2.0;
        });
        context.addInstantFinalTask(()->{
            context.assertTrue(started[0],"Mob did not settle/tick on host floor; age="+pig.age+" position="+pig.getPos());
            context.assertTrue(pig.squaredDistanceTo(goal)<0.65,"Mob did not autonomously walk to goal; position="+pig.getPos());
            context.assertTrue(travelled[0]>start.distanceTo(goal)*0.8,"Insufficient actual movement");
            context.assertTrue(!detour||away[0],"Mob did not detour around BSP wall");
            context.assertTrue(fixture.world.getBlockState(BlockPos.ofFloored(start).down()).isAir(),"Fixture must have no MC support blocks");
            pig.discard();
        });
    }
    @GameTest(templateName="empty",batchId="support",tickLimit=80)
    public void fractionalSupportAndChunkCache(TestContext context){
        Fixture f=fixture(context,"nav_fixture");PigEntity pig=pig(f.world,new Vec3d(4.5,64.25,4.5));
        context.runAtTick(10,()->{
            var cache=new ChunkCache(f.world,new BlockPos(0,60,0),new BlockPos(16,72,16));
            var pc=new PathContext(cache,pig);
            context.assertTrue(pc.getNodeType(4,64,4)==PathNodeType.BLOCKED,"Host support appears OPEN");
            context.assertTrue(LandPathNodeMaker.getLandNodeType(pc,new BlockPos.Mutable(4,65,4))==PathNodeType.WALKABLE,"Host floor is not walkable");
            context.assertTrue(Math.abs(LandPathNodeMaker.getFeetY(cache,new BlockPos(4,65,4))-64.25)<0.00001,"Fractional height lost");
            context.assertTrue(!cache.isSpaceEmpty(pig,new Box(8.1,65,3.1,8.9,66,3.9)),"ChunkCache ignores host wall");
            context.assertTrue(pig.getNavigation().isValidPosition(new BlockPos(4,65,4)),"Random navigation rejects host support");
            pig.discard();context.complete();
        });
    }
    @GameTest(templateName="empty",batchId="detour",tickLimit=300)
    public void walksAroundHostWall(TestContext context){route(context,fixture(context,"nav_fixture"),new Vec3d(4.5,64.25,4.5),new Vec3d(13.5,64.25,4.5),true);}

    @GameTest(templateName="empty",batchId="steps",tickLimit=300)
    public void walksFractionalHostSteps(TestContext context){route(context,fixture(context,"nav_fixture"),new Vec3d(21.5,64.25,12.5),new Vec3d(31.5,65,12.5),false);}

    @GameTest(templateName="empty",batchId="assault",tickLimit=240)
    public void walksActualAssaultBsp(TestContext context){route(context,fixture(context,"cs_assault"),new Vec3d(10,64.001,-5.5),new Vec3d(15.5,64,-5.5),false);}

    private static byte[] door(long tick,float shift){
        return new Wire.Writer().i64(1).i64(tick).i32(1).i32(100).i32(1).i32(1).i32(7).i32(1)
            .f32(0).f32(shift).f32(0).f32(0).f32(0).f32(0)
            .f32(48*32).f32(-24*32+shift).f32(8).f32(49*32).f32(-20*32+shift).f32(5*32)
            .f32(0).f32(0).f32(0).toByteArray();
    }
    @GameTest(templateName="empty",batchId="door",tickLimit=300)
    public void movingBrushUpdatesPathQueriesWithoutBlockUpdates(TestContext context){
        Fixture f=fixture(context,"nav_fixture");f.host.brushes(door(1,0));
        PigEntity pig=pig(f.world,new Vec3d(43.5,64.25,22.5));
        context.runAtTick(12,()->{
            var path=pig.getNavigation().findPathTo(53.5,65,22.5,0);
            context.assertTrue(path==null||!path.reachesTarget(),"Closed host door is traversable");
            var pc=new PathContext(new ChunkCache(f.world,new BlockPos(40,60,19),new BlockPos(56,72,26)),pig);
            context.assertTrue(pc.getNodeType(48,65,22)==PathNodeType.BLOCKED,"Door missing from path cache");
            f.host.brushes(door(2,-320));
            context.assertTrue(pc.getNodeType(48,65,22)==PathNodeType.OPEN,"Moving door left stale BLOCKED node");
            pig.getNavigation().stop();
            var openPath=pig.getNavigation().findPathTo(53.5,65,22.5,0);
            context.assertTrue(openPath!=null&&openPath.reachesTarget(),"Opened door did not restore path");
            pig.getNavigation().startMovingAlong(openPath,1);
        });
        context.addInstantFinalTask(()->{
            context.assertTrue(pig.squaredDistanceTo(53.5,64.25,22.5)<0.65,"Mob did not pass opened door: "+pig.getPos());
            pig.discard();context.complete();
        });
    }

    @GameTest(templateName="empty",batchId="door_replan",tickLimit=350)
    public void movingDoorAutomaticallyResumesAnInterruptedRoute(TestContext context){
        Fixture f=fixture(context,"nav_fixture");f.host.brushes(door(1,-320));
        PigEntity pig=pig(f.world,new Vec3d(43.5,64.25,22.5));
        boolean[] stoppedAtDoor={false};
        context.runAtTick(12,()->{
            var path=pig.getNavigation().findPathTo(53.5,65,22.5,0);
            context.assertTrue(path!=null&&path.reachesTarget(),"Initial open corridor must have a complete route");
            context.assertTrue(pig.getNavigation().startMovingAlong(path,1),"Failed to assign initial route");
        });
        context.runAtTick(22,()->f.host.brushes(door(2,0)));
        context.runAtTick(95,()->{
            context.assertTrue(pig.getX()>44&&pig.getX()<47.6,"Closed moving door did not stop route: "+pig.getPos());
            var path=pig.getNavigation().getCurrentPath();
            context.assertTrue(path!=null&&!path.reachesTarget()&&path.isFinished(),"Closed door must leave a completed partial route");
            stoppedAtDoor[0]=true;
            // No navigation calls follow this movement of the native brush.
            // The existing navigation must notice the changed geometry itself.
            f.host.brushes(door(3,-320));
        });
        context.addInstantFinalTask(()->{
            context.assertTrue(stoppedAtDoor[0],"Mob has not yet waited at the closed door");
            context.assertTrue(pig.squaredDistanceTo(53.5,64.25,22.5)<0.65,"Opened host door did not automatically resume route: "+pig.getPos());
            pig.discard();
        });
    }
}
