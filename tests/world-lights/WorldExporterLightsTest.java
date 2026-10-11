package dev.goldcraft.client;

import dev.goldcraft.bridge.Wire;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
import static dev.goldcraft.client.WorldExporter.*;

/** Executes complete production exporter methods against explicit world/transport inputs. */
class WorldExporterLightsTest {
    private static List<SectionLightSnapshot.Lamp> read(byte[] bytes,long epoch) {
        Wire.Reader reader=new Wire.Reader(bytes);assertEquals(epoch,reader.i64());int count=reader.i32();
        assertEquals(12+count*20,bytes.length);var result=new ArrayList<SectionLightSnapshot.Lamp>();
        for(int i=0;i<count;i++)result.add(new SectionLightSnapshot.Lamp(reader.f32(),reader.f32(),reader.f32(),reader.f32(),reader.i32()));
        reader.finish();return result;
    }
    private static ClientWorld full() {
        ClientWorld world=new ClientWorld();world.lookup=(x,y,z)->State.LAMP;return world;
    }
    @Test void productionScanExportsAll4096LightsBeyond24BlocksAndCameraMovementDoesNotReorderThem() {
        WorldExporter exporter=fixture(full());exporter.export(2,4,2);
        byte[] initial=exporter.lights();var lights=read(initial,42);
        assertEquals(4096,lights.size());assertTrue(lights.stream().allMatch(l->l.x()*l.x()+l.z()*l.z()>24*24));
        assertEquals(new SectionLightSnapshot.Lamp(32.5f,64.6f,32.5f,480,0xffffeed0),lights.getFirst());
        assertEquals(new SectionLightSnapshot.Lamp(47.5f,79.6f,47.5f,480,0xffffeed0),lights.getLast());
        MinecraftClient.getInstance().player.position=new BlockPos(15,64,15);
        assertSame(initial,exporter.lights());exporter.export(2,4,2);assertSame(initial,exporter.lights());
    }
    @Test void failedInitialMeshDoesNotPublishLightsAndRetryPublishesTheCompleteSection() {
        WorldExporter exporter=fixture(full());exporter.endpoint().rejectType=Wire.SECTION_MESH;
        exporter.export(0,4,0);assertFalse(exporter.exported(0,4,0));assertTrue(read(exporter.lights(),42).isEmpty());
        exporter.endpoint().rejectType=-1;exporter.export(0,4,0);
        assertTrue(exporter.exported(0,4,0));assertEquals(4096,read(exporter.lights(),42).size());
    }
    @Test void blockRemovalWaitsForSuccessfulMeshAndThenRemovesThatLight() {
        ClientWorld world=full();WorldExporter exporter=fixture(world);exporter.export(0,4,0);
        byte[] before=exporter.lights();world.lookup=(x,y,z)->x==0&&y==64&&z==0?State.AIR:State.LAMP;
        exporter.endpoint().rejectType=Wire.SECTION_MESH;exporter.export(0,4,0);assertSame(before,exporter.lights());
        exporter.endpoint().rejectType=-1;exporter.export(0,4,0);var after=read(exporter.lights(),42);
        assertEquals(4095,after.size());assertEquals(1.5f,after.getFirst().x());
    }
    @Test void boundaryBlockRemovalKeepsTheNeighborSectionLight() {
        ClientWorld world=new ClientWorld();
        world.lookup=(x,y,z)->(x==15||x==16)&&y==64&&z==0?State.TORCH:State.AIR;
        WorldExporter exporter=fixture(world);exporter.export(1,4,0);exporter.export(0,4,0);
        var both=read(exporter.lights(),42);assertEquals(2,both.size());
        assertEquals(15.5f,both.get(0).x());assertEquals(16.5f,both.get(1).x());
        world.lookup=(x,y,z)->x==16&&y==64&&z==0?State.TORCH:State.AIR;
        exporter.export(0,4,0);exporter.export(1,4,0);
        assertEquals(List.of(both.get(1)),read(exporter.lights(),42));
    }
    @Test void sceneWindowUnloadKeepsLightsUntilRemoveSectionIsAccepted() {
        WorldExporter exporter=fixture(full());exporter.export(0,4,0);byte[] before=exporter.lights();
        MinecraftClient.getInstance().player.position=new BlockPos(64,64,0);
        exporter.endpoint().rejectType=Wire.REMOVE_SECTION;exporter.refreshNow();
        assertTrue(exporter.exported(0,4,0));assertSame(before,exporter.lights());
        exporter.endpoint().rejectType=-1;exporter.refreshNow();
        assertFalse(exporter.exported(0,4,0));assertTrue(read(exporter.lights(),42).isEmpty());
    }
    @Test void unloadedChunkDoesNotPublishUnexportedLights() {
        ClientWorld world=full();world.loaded=false;WorldExporter exporter=fixture(world);
        exporter.export(0,4,0);assertFalse(exporter.exported(0,4,0));assertTrue(read(exporter.lights(),42).isEmpty());
    }
    @Test void bindingNewMapEpochClearsPreviousSceneLights() {
        WorldExporter exporter=fixture(full());exporter.export(0,4,0);byte[] before=exporter.lights();
        exporter.bind(42);assertSame(before,exporter.lights());exporter.bind(43);
        assertTrue(read(exporter.lights(),43).isEmpty());assertFalse(exporter.exported(0,4,0));
    }
    @Test void resourceReloadClearsLightsAfterSceneResetIsAccepted() {
        ClientWorld world=full();WorldExporter exporter=fixture(world);exporter.export(0,4,0);byte[] before=exporter.lights();
        world.lookup=(x,y,z)->State.AIR;WorldExporter.atlasUploaded(new SpriteAtlasTexture());
        exporter.endpoint().rejectType=Wire.SCENE_RESET;exporter.renderNow();assertSame(before,exporter.lights());
        exporter.endpoint().rejectType=-1;exporter.renderNow();assertTrue(read(exporter.lights(),42).isEmpty());
    }
    @Test void reconnectResendClearsTheOldScene() {
        ClientWorld world=full();WorldExporter exporter=fixture(world);exporter.export(0,4,0);
        world.lookup=(x,y,z)->State.AIR;exporter.resendScene(42);exporter.renderNow();
        assertTrue(read(exporter.lights(),42).isEmpty());
    }
    @Test void replacingMinecraftWorldClearsTheOldScene() {
        WorldExporter exporter=fixture(full());exporter.export(0,4,0);
        MinecraftClient.getInstance().world=new ClientWorld();exporter.renderNow();
        assertTrue(read(exporter.lights(),42).isEmpty());
    }
}
