package dev.goldcraft.test;

import dev.goldcraft.GoldCraft;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import net.minecraft.block.Blocks;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.util.math.BlockPos;

@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class HostIntegrationTest {
    @GameTest(templateName="empty",tickLimit=400)
    public void realHostServerStream(TestContext context) {
        context.setBlockState(new BlockPos(1,1,1),Blocks.STONE);
        context.addInstantFinalTask(()-> {
            context.assertTrue(GoldCraft.hostConnected(),"NeoForge is not connected to the real CS server");
            context.assertTrue(GoldCraft.HOST_WORLD.epoch()!=0,"No host map epoch received");
            context.assertTrue(GoldCraft.HOST_WORLD.map().equals(System.getenv().getOrDefault("GOLDCRAFT_TEST_MAP","cs_assault")),"Unexpected isolated CS test map");
            context.assertTrue(GoldCraft.HOST_WORLD.tick()>=3,"No advancing authoritative actor snapshots");
            context.expectBlock(Blocks.STONE,new BlockPos(1,1,1));
        });
    }
}
