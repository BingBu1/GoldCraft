package dev.goldcraft.test;

import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.HostRaycast;
import dev.goldcraft.world.HostSupportUpdates;
import net.minecraft.block.Blocks;
import net.minecraft.block.WallTorchBlock;
import net.minecraft.entity.FallingBlockEntity;
import net.minecraft.entity.ItemEntity;
import net.minecraft.item.AutomaticItemPlacementContext;
import net.minecraft.item.BlockItem;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import java.util.List;

@GameTestHolder("goldcraft_support")
@PrefixGameTestTemplate(false)
public final class BlockSupportTest {
    @GameTest(templateName="empty",batchId="host_support_falling",tickLimit=110)
    public void placedAndFallingBlocksRetainTheirStateOnActualBsp(TestContext context){
        var f=NavigationTest.fixture(context,"cs_assault");var w=f.world();
        var blocks=List.of(Blocks.SAND,Blocks.ANVIL,Blocks.GRAVEL,Blocks.RED_SAND,Blocks.WHITE_CONCRETE_POWDER);
        for(int i=0;i<blocks.size();i++){
            var p=new BlockPos(10+i,64,-6);
            context.assertTrue(w.getBlockState(p.down()).isAir(),"Fixture must have no Minecraft floor");
            context.assertTrue(HostCollision.shape(w,p).isEmpty()&&!HostCollision.shape(w,p.down()).isEmpty(),"Actual BSP floor moved");
            w.setBlockState(p,blocks.get(i).getDefaultState(),3);
            var high=p.south(2).up(5);w.setBlockState(high,blocks.get(i).getDefaultState(),3);
            FallingBlockEntity.spawnFromBlock(w,high,blocks.get(i).getDefaultState());
        }
        context.runAtTick(80,()->{
            for(int i=0;i<blocks.size();i++){
                var p=new BlockPos(10+i,64,-6);
                context.assertTrue(w.getBlockState(p).isOf(blocks.get(i)),"Placed gravity block destroyed: "+blocks.get(i));
                context.assertTrue(w.getBlockState(p.south(2)).isOf(blocks.get(i)),"Falling block did not settle: "+blocks.get(i));
                context.assertTrue(w.getBlockState(p.down()).isAir(),"Support wrote a fake Minecraft floor");
                w.setBlockState(p,Blocks.AIR.getDefaultState(),3);w.setBlockState(p.south(2),Blocks.AIR.getDefaultState(),3);
            }
            context.assertTrue(w.getEntitiesByClass(ItemEntity.class,new Box(8,60,-8,18,72,0),e->true).isEmpty(),"Gravity blocks dropped as items");
            context.complete();
        });
    }

    @GameTest(templateName="empty",batchId="host_support_torch",tickLimit=55)
    public void actualItemPlacementAcceptsFloorAndWallButNotAir(TestContext context){
        var f=NavigationTest.fixture(context,"nav_fixture");var w=f.world();
        var floor=new BlockPos(4,64,4);var wall=new BlockPos(7,66,4);
        var hit=HostRaycast.trace(f.host(),new Vec3d(4.5,67,4.5),new Vec3d(4.5,62,4.5));
        context.assertTrue(hit!=null&&hit.getBlockPos().equals(floor)&&hit.getSide()==Direction.UP,"Host placement ray chose wrong floor cell");
        var item=(BlockItem)Items.TORCH;
        context.assertTrue(item.place(new AutomaticItemPlacementContext(w,floor,Direction.DOWN,new ItemStack(Items.TORCH),Direction.UP)).isAccepted(),"Torch item rejected host floor");
        context.assertTrue(item.place(new AutomaticItemPlacementContext(w,wall,Direction.EAST,new ItemStack(Items.TORCH),Direction.WEST)).isAccepted(),"Torch item rejected host wall");
        var air=new BlockPos(4,78,4);
        context.assertTrue(!item.place(new AutomaticItemPlacementContext(w,air,Direction.DOWN,new ItemStack(Items.TORCH),Direction.UP)).isAccepted(),"Floating torch was accepted");
        context.assertTrue(w.getBlockState(floor.down()).isAir()&&w.getBlockState(wall.east()).isAir(),"Host support changed air state");
        context.assertTrue(w.getBlockState(wall).isOf(Blocks.WALL_TORCH)&&w.getBlockState(wall).get(WallTorchBlock.FACING)==Direction.WEST,"Wrong wall torch orientation");
        context.assertTrue(!Blocks.AIR.getDefaultState().isSideSolidFullSquare(w,air,Direction.UP),"Cached AIR support leaked to empty space");
        context.assertTrue(!Blocks.AIR.getDefaultState().isSideSolidFullSquare(context.getWorld(),air,Direction.UP),"Host support leaked to ordinary world");
        context.runAtTick(30,()->{
            context.assertTrue(w.getBlockState(floor).isOf(Blocks.TORCH)&&w.getBlockState(wall).isOf(Blocks.WALL_TORCH),"Placed torches did not survive normal ticks");
            w.setBlockState(floor,Blocks.AIR.getDefaultState(),3);w.setBlockState(wall,Blocks.AIR.getDefaultState(),3);context.complete();
        });
    }

    @GameTest(templateName="empty",batchId="host_support_lifecycle",tickLimit=110)
    public void carvedFloorAndMovingDoorInvalidateSupportWithoutFakeBlocks(TestContext context){
        var f=NavigationTest.fixture(context,"cs_assault");var w=f.world();
        var torch=new BlockPos(12,64,-6);var sand=torch.east(2);
        context.assertTrue(Blocks.TORCH.getDefaultState().canPlaceAt(w,torch),"Torch cannot use actual BSP support");
        w.setBlockState(torch,Blocks.TORCH.getDefaultState(),3);w.setBlockState(sand,Blocks.SAND.getDefaultState(),3);
        var doorFixture=NavigationTest.fixture(context,"nav_fixture");var dw=doorFixture.world();var wall=new BlockPos(47,66,22);
        doorFixture.host().brushes(door(1,0));
        var wallState=Blocks.WALL_TORCH.getDefaultState().with(WallTorchBlock.FACING,Direction.WEST);
        context.assertTrue(wallState.canPlaceAt(dw,wall),"Closed host door cannot support torch");dw.setBlockState(wall,wallState,3);
        context.runAtEveryTick(()->{HostSupportUpdates.tick(w);HostSupportUpdates.tick(dw);});
        context.runAtTick(20,()->{
            context.assertTrue(w.getBlockState(sand).isOf(Blocks.SAND),"Sand did not remain before carving");
            var bounds=HostCollision.goldsrc(new Box(11,61,-7,16,64,-3));
            f.host().edits().accept(new MapEdits.Snapshot(1,1,List.of(new MapEdits.Cut(1,new MapEdits.Target(0,0,0),
                new MapEdits.Box(bounds.min().x(),bounds.min().y(),bounds.min().z(),bounds.max().x(),bounds.max().y(),bounds.max().z())))));
            doorFixture.host().brushes(door(2,-320));
        });
        context.runAtTick(80,()->{
            context.assertTrue(w.getBlockState(torch).isAir(),"Torch retained carved-away floor");
            context.assertTrue(w.getBlockState(sand).isAir()&&w.getBlockState(sand.down(3)).isOf(Blocks.SAND),"Sand did not naturally fall after carving");
            context.assertTrue(dw.getBlockState(wall).isAir(),"Torch remained on opened door");
            context.assertTrue(w.getEntitiesByClass(ItemEntity.class,new Box(10,59,-9,17,68,0),e->e.getStack().isOf(Items.TORCH)).stream().mapToInt(e->e.getStack().getCount()).sum()==1,"Carved support must drop exactly one torch");
            context.assertTrue(dw.getEntitiesByClass(ItemEntity.class,new Box(44,62,19,51,70,26),e->e.getStack().isOf(Items.TORCH)).stream().mapToInt(e->e.getStack().getCount()).sum()==1,"Moving door must drop exactly one torch");
            w.setBlockState(sand.down(3),Blocks.AIR.getDefaultState(),3);
            f.host().edits().accept(new MapEdits.Snapshot(1,2,List.of()));
            context.assertTrue(Blocks.TORCH.getDefaultState().canPlaceAt(w,torch),"Restored BSP retained stale support cache");
            context.complete();
        });
    }

    private static byte[] door(long tick,float shift){
        return new Wire.Writer().i64(1).i64(tick).i32(1).i32(100).i32(1).i32(1).i32(7).i32(1)
            .f32(0).f32(shift).f32(0).f32(0).f32(0).f32(0)
            .f32(48*32).f32(-24*32+shift).f32(8).f32(49*32).f32(-20*32+shift).f32(5*32)
            .f32(0).f32(0).f32(0).toByteArray();
    }
}
