package dev.goldcraft.test;

import dev.goldcraft.world.HostWorldReset;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.entity.ItemEntity;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import java.util.stream.Stream;

@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class HostWorldResetTest {
    private static Entity pig(ServerWorld world){
        var entity=EntityType.PIG.create(world);entity.refreshPositionAndAngles(4.5,65,4.5,0,0);world.spawnEntity(entity);return entity;
    }
    private static NbtCompound save(Entity entity){
        var data=entity.writeNbt(new NbtCompound());data.putString("id",EntityType.getId(entity.getType()).toString());return data;
    }
    private static Entity load(ServerWorld world,NbtCompound data){
        var entity=EntityType.loadEntityWithPassengers(data,world,e->e);
        world.loadEntities(Stream.of(entity));return entity;
    }
    @GameTest(templateName="empty",batchId="map_entity_reset",tickLimit=60)
    public void resetLoadedAndDiskEntitiesButKeepSameSessionReload(TestContext context){
        var first=NavigationTest.fixture(context,"nav_fixture").world();
        var second=NavigationTest.fixture(context,"cs_assault").world();
        HostWorldReset.activate(first,701);
        var live=pig(first);var snapshot=save(live);
        var item=new ItemEntity(first,4.5,65,4.5,new ItemStack(Items.DIAMOND));first.spawnEntity(item);
        var projectile=EntityType.ARROW.create(first);projectile.refreshPositionAndAngles(4.5,65,4.5,0,0);first.spawnEntity(projectile);
        HostWorldReset.activate(first,701);
        context.assertTrue(!live.isRemoved()&&!item.isRemoved()&&!projectile.isRemoved(),"Same-session reconnect erased entities");
        live.remove(Entity.RemovalReason.UNLOADED_TO_CHUNK);
        var reloaded=load(first,snapshot.copy());
        context.assertTrue(first.getEntity(reloaded.getUuid())==reloaded,"Same-session serialized entity was rejected on actual load path");
        var outside=pig(context.getWorld());
        HostWorldReset.activate(second,702);
        context.assertTrue(reloaded.isRemoved()&&item.isRemoved()&&projectile.isRemoved(),"Departing map retained mobs/items/projectiles");
        context.assertTrue(!outside.isRemoved(),"Reset touched an unmanaged Minecraft dimension");
        var stale=load(first,snapshot.copy());
        context.assertTrue(first.getEntity(stale.getUuid())==null,"Old disk entity reappeared after changing maps");
        var fresh=pig(second);var freshData=save(fresh);
        HostWorldReset.activate(second,702);
        context.assertTrue(!fresh.isRemoved(),"Ordinary reconnect erased the new map mob");
        HostWorldReset.activate(second,703);
        context.assertTrue(fresh.isRemoved(),"Same-map server restart preserved its previous mob");
        var old=load(second,freshData);
        context.assertTrue(second.getEntity(old.getUuid())==null,"Old disk entity survived the restart epoch");
        outside.discard();context.complete();
    }
}
