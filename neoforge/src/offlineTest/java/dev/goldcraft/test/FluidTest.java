package dev.goldcraft.test;

import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.MinecraftFluids;
import net.minecraft.block.Blocks;
import net.minecraft.fluid.Fluids;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.util.math.BlockPos;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;

@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class FluidTest {
    @GameTest(templateName="empty",batchId="host_fluid",tickLimit=130)
    public void scheduledWaterAndLavaStopAtBsp(TestContext context){
        var f=NavigationTest.fixture(context,"nav_fixture");var w=f.world();
        var water=new BlockPos(4,65,4);var lava=new BlockPos(22,65,4);
        w.setBlockState(water,Blocks.WATER.getDefaultState(),3);w.setBlockState(lava,Blocks.LAVA.getDefaultState(),3);
        w.scheduleFluidTick(water,Fluids.WATER,1);w.scheduleFluidTick(lava,Fluids.LAVA,1);
        context.runAtTick(110,()->{
            context.assertTrue(!w.getFluidState(water.east()).isEmpty(),"Water did not actually spread on BSP floor");
            context.assertTrue(!w.getFluidState(lava.east()).isEmpty(),"Lava did not actually spread on BSP floor");
            for(int x=0;x<32;x++)for(int z=0;z<12;z++){
                context.assertTrue(w.getFluidState(new BlockPos(x,63,z)).isEmpty(),"Fluid crossed BSP floor at "+x+","+z);
                for(int y=64;y<69;y++){var p=new BlockPos(x,y,z);
                    if(!HostCollision.shape(w,p).isEmpty())context.assertTrue(w.getFluidState(p).isEmpty(),"Fluid entered BSP cell "+p);
                }
            }
            var volumes=MinecraftFluids.scan(w.getChunk(water.getX()>>4,water.getZ()>>4));
            context.assertTrue(volumes.stream().anyMatch(v->v.contents()==MinecraftFluids.WATER),"Water missing from native export");
            var lavaVolumes=MinecraftFluids.scan(w.getChunk(lava.getX()>>4,lava.getZ()>>4));
            context.assertTrue(lavaVolumes.stream().anyMatch(v->v.contents()==MinecraftFluids.LAVA),"Lava missing from native export");
            context.assertTrue(volumes.stream().allMatch(v->v.box().minY>=65),"Exported liquid below fractional BSP floor");
            for(int x=0;x<32;x++)for(int z=0;z<16;z++)for(int y=63;y<69;y++){
                var p=new BlockPos(x,y,z);if(!w.getFluidState(p).isEmpty())w.setBlockState(p,Blocks.AIR.getDefaultState(),3);
            }
            context.complete();
        });
    }
    @GameTest(templateName="empty",batchId="host_fluid_assault",tickLimit=160)
    public void actualAssaultFloorAndEmbeddedCleanup(TestContext context){
        var f=NavigationTest.fixture(context,"cs_assault");var w=f.world();
        var p=new BlockPos(12,64,-6);var under=p.down();
        context.assertTrue(HostCollision.shape(w,p).isEmpty()&&!HostCollision.shape(w,under).isEmpty(),"Actual assault fixture floor changed");
        w.setBlockState(under,Blocks.LAVA.getDefaultState(),3);
        var queue=(net.minecraft.world.tick.ChunkTickScheduler<?>)w.getChunk(under).getFluidTickScheduler();
        long before=queue.getQueueAsStream().filter(t->t.pos().equals(under)&&t.type()==Fluids.LAVA)
            .mapToLong(t->t.triggerTick()).min().orElse(-1);
        w.scheduleFluidTick(under,Fluids.LAVA,1);
        long deadline=queue.getQueueAsStream().filter(t->t.pos().equals(under)&&t.type()==Fluids.LAVA)
            .mapToLong(t->t.triggerTick()).min().orElse(-1);
        context.assertTrue(before>w.getTime()&&deadline==before,"Vanilla lava queue/de-duplication changed");
        long[] firstAir={-1},waterPlaced={-1};
        context.runAtEveryTick(()->{
            long now=w.getTime();
            if(firstAir[0]<0&&w.getBlockState(under).isAir())firstAir[0]=now;
            if(waterPlaced[0]<0){
                if(now<deadline+2)return;
                // Assert AIR and the consumed real queue before adding water;
                // obsidian conversion must not masquerade as embedded cleanup.
                context.assertTrue(w.getBlockState(under).isAir()&&firstAir[0]>=deadline,
                    "Embedded lava was not removed after its actual scheduled tick");
                context.assertTrue(queue.getQueueAsStream().noneMatch(t->t.pos().equals(under)&&t.type()==Fluids.LAVA),
                    "Embedded lava tick was not consumed");
                w.setBlockState(p,Blocks.WATER.getDefaultState(),3);w.scheduleFluidTick(p,Fluids.WATER,1);
                waterPlaced[0]=now;return;
            }
            if(now-waterPlaced[0]<60)return;
            context.assertTrue(w.getFluidState(under).isEmpty(),"Old embedded lava source survived");
            context.assertTrue(w.getFluidState(p.down(2)).isEmpty(),"Water flowed through actual cs_assault BSP");
            context.assertTrue(!w.getFluidState(p.east()).isEmpty(),"Water failed to spread on actual assault floor");
            for(int x=3;x<22;x++)for(int z=-15;z<4;z++)for(int y=62;y<66;y++){
                var q=new BlockPos(x,y,z);if(!w.getFluidState(q).isEmpty())w.setBlockState(q,Blocks.AIR.getDefaultState(),3);
            }
            context.complete();
        });
    }


    private static byte[] door(long tick,float shift){
        return new dev.goldcraft.bridge.Wire.Writer().i64(1).i64(tick).i32(1).i32(100).i32(1).i32(1).i32(7).i32(1)
            .f32(0).f32(shift).f32(0).f32(0).f32(0).f32(0)
            .f32(48*32).f32(-24*32+shift).f32(8).f32(49*32).f32(-20*32+shift).f32(5*32)
            .f32(0).f32(0).f32(0).toByteArray();
    }

    @GameTest(templateName="empty",batchId="host_fluid_door",tickLimit=110)
    public void movingDoorInvalidatesVanillaAirStateFlowCache(TestContext context){
        var f=NavigationTest.fixture(context,"nav_fixture");var w=f.world();
        var source=new BlockPos(47,65,22);var gate=source.east();var past=gate.east();
        f.host().brushes(door(1,0));
        w.setBlockState(source,Blocks.WATER.getDefaultState(),3);w.scheduleFluidTick(source,Fluids.WATER,1);
        context.runAtTick(25,()->{
            context.assertTrue(!HostCollision.shape(w,gate).isEmpty(),"Closed host door absent");
            context.assertTrue(w.getBlockState(gate).isAir()&&w.getFluidState(past).isEmpty(),"Water crossed closed BSP door");
            f.host().brushes(door(2,-320));
            context.assertTrue(HostCollision.shape(w,gate).isEmpty(),"Door did not move away");
            // The door changes no Minecraft block states. Revisit the same
            // vanilla air/air face after receiving a new host geometry revision.
            w.scheduleFluidTick(source,Fluids.WATER,1);
        });
        context.runAtTick(65,()->{
            context.assertTrue(!w.getFluidState(gate).isEmpty()&&!w.getFluidState(past).isEmpty(),"Opened BSP door left stale blocked flow");
            f.host().brushes(door(3,0));w.scheduleFluidTick(gate,Fluids.WATER,1);
        });
        context.runAtTick(90,()->{
            context.assertTrue(w.getBlockState(gate).isAir(),"Reclosed BSP door retained embedded liquid");
            context.assertTrue(MinecraftFluids.scan(w.getChunk(source.getX()>>4,source.getZ()>>4)).stream().noneMatch(v->v.box().intersects(48,65,22,49,66,23)),"Export includes reclosed door cell");
            context.complete();
        });
    }

    @GameTest(templateName="empty",batchId="host_fluid_waterlogged",tickLimit=15)
    public void waterloggedCollisionIsSubtractedWithoutDeletingBlock(TestContext context){
        var f=NavigationTest.fixture(context,"nav_fixture");var w=f.world();var p=new BlockPos(4,65,4);
        var slab=Blocks.OAK_SLAB.getDefaultState().with(net.minecraft.state.property.Properties.WATERLOGGED,true);
        w.setBlockState(p,slab,3);w.scheduleFluidTick(p,Fluids.WATER,1);
        context.runAtTick(8,()->{
            context.assertTrue(w.getBlockState(p).isOf(Blocks.OAK_SLAB),"Waterlogged building block was deleted");
            var cell=new net.minecraft.util.math.Box(p);
            var volumes=MinecraftFluids.scan(w.getChunk(p.getX()>>4,p.getZ()>>4));
            var wet=volumes.stream().filter(v->v.box().intersects(cell)).toList();
            context.assertTrue(!wet.isEmpty(),"Waterlogged fluid missing from export");
            context.assertTrue(wet.stream().allMatch(v->v.contents()==MinecraftFluids.WATER&&v.box().minY>=65.5),"Waterlogged solid half exported as liquid");
            w.setBlockState(p,Blocks.AIR.getDefaultState(),3);context.complete();
        });
    }
}
