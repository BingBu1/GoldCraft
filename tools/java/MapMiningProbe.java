// Isolated production-NeoForge fixture. Never packaged with the GoldCraft mod.
package dev.goldcraft.test.mining;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.mojang.authlib.GameProfile;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.net.MiningPayload;
import dev.goldcraft.world.HostMining;
import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.HostRaycast;
import dev.goldcraft.world.HostSession;
import io.netty.channel.embedded.EmbeddedChannel;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.players.PlayerList;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.entity.projectile.ProjectileUtil;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import net.neoforged.bus.api.EventPriority;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.common.util.FakePlayer;
import net.neoforged.neoforge.event.tick.ServerTickEvent;

@Mod("goldcraft_mining_probe")
public final class MapMiningProbe {
    private final Path command = Path.of(System.getenv("GOLDCRAFT_MINING_COMMAND"));
    private final Path status = Path.of(System.getenv("GOLDCRAFT_MINING_PROBE"));
    private final Path bindings = Path.of(System.getenv("GOLDCRAFT_HEADLESS_BINDINGS"));
    private final UUID uuid = UUID.randomUUID();
    private FakePlayer player;
    private EmbeddedChannel channel;
    private int ticks, slot, consecutiveWriteFailures, statusRetries;
    private long acknowledged, nextPair;
    private boolean held, ready = true, renew = true;
    private String error;
    private BlockPos placedBlock;
    private JsonObject geometryProbe;

    public MapMiningProbe() {
        NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST, this::tick);
    }

    // These two private field names were checked against the pinned 1.21.1
    // sources and Mojang mappings. This simulates the player-list registration
    // only; no login/authentication or network-client acceptance is claimed.
    @SuppressWarnings("unchecked")
    private void register(MinecraftServer server, boolean add) throws ReflectiveOperationException {
        var listField = PlayerList.class.getDeclaredField("players");
        var mapField = PlayerList.class.getDeclaredField("playersByUUID");
        listField.setAccessible(true); mapField.setAccessible(true);
        var list = (List<ServerPlayer>)listField.get(server.getPlayerList());
        var map = (Map<UUID, ServerPlayer>)mapField.get(server.getPlayerList());
        if (add) { list.add(player); map.put(uuid, player); }
        else { list.remove(player); map.remove(uuid, player); }
    }

    private void pair() throws Exception {
        if (ticks < nextPair || !Files.exists(bindings)) return;
        var bytes = ByteBuffer.wrap(Files.readAllBytes(bindings)).order(ByteOrder.LITTLE_ENDIAN);
        if (bytes.remaining() < 12 || bytes.getLong() != GoldCraft.HOST_WORLD.epoch()) return;
        int count = bytes.getInt();
        if (count < 1 || count > 4 || bytes.remaining() != count * 24) return;
        slot = bytes.getInt(); int serial = bytes.getInt(); byte[] token = new byte[16]; bytes.get(token);
        GoldCraft.sendToHost(Wire.PAIR_PLAYER, new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch())
            .i32(slot).i32(serial).bytes(token).bytes(Wire.uuid(uuid)).toByteArray());
        nextPair = ticks + 20;
    }

    private void equip(String item) {
        player.getMainHandItem().forEachModifier(EquipmentSlot.MAINHAND, (type, modifier) -> {
            var attribute = player.getAttribute(type); if (attribute != null) attribute.removeModifier(modifier.id());
        });
        var stack = new ItemStack(BuiltInRegistries.ITEM.get(ResourceLocation.parse(item)));
        player.setItemInHand(InteractionHand.MAIN_HAND, stack);
        // FakePlayer.tick is intentionally empty. Apply the real item's declared
        // equipment modifiers explicitly, without hard-coded damage/tool values.
        stack.forEachModifier(EquipmentSlot.MAINHAND, (type, modifier) -> {
            var attribute = player.getAttribute(type); if (attribute != null) attribute.addTransientModifier(modifier);
        });
    }

    private void readCommand(MinecraftServer server) throws Exception {
        if (!Files.exists(command)) return;
        var data = JsonParser.parseString(Files.readString(command)).getAsJsonObject();
        long id = data.get("id").getAsLong(); if (id <= acknowledged) return;
        String op = data.get("op").getAsString();
        if (op.equals("stop")) {
            held = false; if (player != null) register(server, false);
            if (channel != null) channel.finishAndReleaseAll();
            acknowledged = id; server.halt(false); return;
        }
        if (player == null) return;
        switch (op) {
            case "forget_edits" -> GoldCraft.HOST_WORLD.edits().reset(GoldCraft.HOST_WORLD.epoch());
            case "geometry_probe" -> probeGeometry(data);
            case "aim" -> {
                var eye = data.getAsJsonArray("eye"); var point = data.getAsJsonArray("point");
                double x=eye.get(0).getAsDouble(), y=eye.get(1).getAsDouble(), z=eye.get(2).getAsDouble();
                double dx=point.get(0).getAsDouble()-x, dy=point.get(1).getAsDouble()-y, dz=point.get(2).getAsDouble()-z;
                float yaw=(float)(-Math.toDegrees(Math.atan2(dy,dx))-90);
                float pitch=(float)-Math.toDegrees(Math.atan2(dz,Math.hypot(dx,dy)));
                player.moveTo(x/32, z/32+64-player.getEyeHeight(), -y/32, yaw, pitch);
                // LivingEntity.getViewVector(1) uses head yaw. FakePlayer.tick
                // does not copy the simulated view/body rotation to the head.
                player.setYHeadRot(yaw);
                player.setDeltaMovement(0,0,0);
            }
            case "held" -> held=data.get("value").getAsBoolean();
            case "renew" -> renew=data.get("value").getAsBoolean();
            case "mode" -> player.setGameMode(GameType.byName(data.get("value").getAsString()));
            case "item" -> equip(data.get("value").getAsString());
            case "use" -> {
                if (data.get("value").getAsBoolean()) {
                    player.setItemInHand(InteractionHand.OFF_HAND, new ItemStack(Items.SHIELD));
                    player.startUsingItem(InteractionHand.OFF_HAND);
                } else player.stopUsingItem();
            }
            case "block" -> {
                if (placedBlock != null) player.serverLevel().setBlockAndUpdate(placedBlock, Blocks.AIR.defaultBlockState());
                placedBlock = data.get("value").getAsBoolean()
                    ? BlockPos.containing(player.getEyePosition().add(player.getLookAngle().scale(.9))) : null;
                if (placedBlock != null) player.serverLevel().setBlockAndUpdate(placedBlock, Blocks.STONE.defaultBlockState());
            }
            case "ready" -> ready=data.get("value").getAsBoolean();
            default -> throw new IllegalArgumentException("Unknown fixture operation: "+op);
        }
        acknowledged=id;
    }

    private void tick(ServerTickEvent.Post event) {
        var server=event.getServer(); ++ticks;
        try {
            readCommand(server);
            if (error == null && GoldCraft.hostConnected()) {
                var host=GoldCraft.HOST_WORLD; var world=HostSession.hostWorld(server,host);
                if (world != null && player == null) {
                    player=new FakePlayer(world,new GameProfile(uuid,"MiningProbe"));
                    // FakePlayer's dummy Connection has no Netty channel. A local
                    // empty channel lets the real hasChannel check return false;
                    // it does not negotiate or emulate an actual game client.
                    channel=new EmbeddedChannel(player.connection.getConnection());
                    player.setGameMode(GameType.SURVIVAL); register(server,true); equip("minecraft:iron_pickaxe");
                }
                if (player != null) {
                    var actor=host.actor(uuid);
                    if (actor == null) pair();
                    else {
                        if ((actor.flags()&32)==0) {
                            var feet=actor.minecraftFeet();
                            player.moveTo(feet.x(),feet.y(),feet.z(),-actor.yaw()-90,actor.pitch());
                        }
                        HostSession.ready(uuid,host.epoch(),actor.serial(),actor.life(),ready);
                        readCommand(server);
                        if (renew) HostMining.intent(player,new MiningPayload(host.epoch(),host.mining().revision(),actor.serial(),actor.life(),held));
                    }
                }
            }
            if (error != null) readCommand(server); // Preserve owned-JVM shutdown on failure.
        } catch (Exception failure) {
            held=false; error=failure.toString(); failure.printStackTrace();
        }
        writeStatus();
    }

    private static Vec3 nativePoint(com.google.gson.JsonArray values) {
        return new Vec3(values.get(0).getAsDouble()/32,values.get(2).getAsDouble()/32+64,-values.get(1).getAsDouble()/32);
    }

    private void probeGeometry(JsonObject data) {
        var start=nativePoint(data.getAsJsonArray("start"));
        var end=nativePoint(data.getAsJsonArray("end"));
        boolean crouch=data.get("crouch").getAsBoolean();
        player.setGameMode(GameType.CREATIVE);
        player.noPhysics=false;
        player.setPose(crouch?Pose.CROUCHING:Pose.STANDING);
        player.refreshDimensions();
        player.setOnGround(false);
        player.moveTo(start.x,start.y-(crouch?18:36)/32.0,start.z,0,0);
        player.setDeltaMovement(Vec3.ZERO);
        var before=player.position();
        // Execute the actual Entity.move -> voxel collision -> injected player
        // hull path. This is controlled motion, not graphical keyboard input.
        player.move(MoverType.SELF,end.subtract(start));
        var moved=player.position().subtract(before);
        var ray=player.level().clip(new ClipContext(start,end,ClipContext.Block.OUTLINE,ClipContext.Fluid.NONE,player));
        var cell=BlockPos.containing(nativePoint(data.getAsJsonArray("cell")));
        geometryProbe=new JsonObject();
        geometryProbe.addProperty("moved",moved.length()*32);
        geometryProbe.addProperty("height",player.getBbHeight()*32);
        geometryProbe.addProperty("rayBlocked",ray.getType()!=HitResult.Type.MISS);
        geometryProbe.addProperty("cellSolid",!HostCollision.shape(player.level(),cell).isEmpty());
        geometryProbe.addProperty("collisionRevision",HostCollision.revision(player.level()));
        geometryProbe.addProperty("editRevision",GoldCraft.HOST_WORLD.edits().revision());
    }

    private void writeStatus() {
        try {
            var out=new JsonObject(); var host=GoldCraft.HOST_WORLD; var actor=host.actor(uuid);
            out.addProperty("tick",ticks); out.addProperty("command",acknowledged); out.addProperty("connected",GoldCraft.hostConnected());
            out.addProperty("map",host.map()); out.addProperty("mode",host.mining().mode());
            out.addProperty("policyRevision",host.mining().revision()); out.addProperty("freeze",host.freeze());
            out.addProperty("editsReady",host.edits().ready());out.addProperty("editRevision",host.edits().revision());
            out.addProperty("editCount",host.edits().cuts().size());
            out.addProperty("slot",slot); out.addProperty("actorFlags",actor==null?0:actor.flags());
            out.addProperty("held",held); out.addProperty("renew",renew); out.addProperty("error",error);
            out.addProperty("statusRetries",statusRetries);
            if(geometryProbe!=null)out.add("geometry",geometryProbe);
            if (player!=null) {
                out.addProperty("gameMode",player.gameMode.getGameModeForPlayer().getName());
                out.addProperty("toolDamage",player.getMainHandItem().getDamageValue());
                out.addProperty("attackDamage",player.getAttributeValue(Attributes.ATTACK_DAMAGE));
                out.addProperty("reach",player.blockInteractionRange());
                out.addProperty("cooldownTicks",player.getCurrentItemAttackStrengthDelay());
                out.addProperty("alive",player.isAlive());
                out.addProperty("dimension",player.level().dimension().location().toString());
                out.addProperty("nanoTime",System.nanoTime());
                out.addProperty("capabilities",host.mining().capabilities());
                for(String name:new String[]{"tick","policyRevision"}){
                    var field=HostMining.class.getDeclaredField(name);field.setAccessible(true);
                    out.addProperty("mining"+name,((Number)field.get(null)).longValue());
                }
                // Read-only fixture diagnostics; never alter the production ledger.
                var intents=HostMining.class.getDeclaredField("INTENTS"); intents.setAccessible(true);
                var intent=((Map<?,?>)intents.get(null)).get(uuid);
                if(intent!=null){
                    var detail=new JsonObject();
                    for(String name:new String[]{"held","epoch","revision","serial","life","lastIntent","nextTick","pending"}){
                        var field=intent.getClass().getDeclaredField(name);field.setAccessible(true);
                        detail.addProperty(name,String.valueOf(field.get(intent)));
                    }
                    out.add("intent",detail);
                }
                out.addProperty("usingItem",player.isUsingItem()); out.addProperty("life",actor==null?0:actor.life());
                out.addProperty("x",player.getX());out.addProperty("y",player.getY());out.addProperty("z",player.getZ());
                var start=player.getEyePosition();var end=start.add(player.getViewVector(1).scale(player.blockInteractionRange()));
                var hit=player.level().clip(new ClipContext(start,end,ClipContext.Block.OUTLINE,ClipContext.Fluid.NONE,player));
                out.addProperty("rayType",hit.getType().toString());
                if(hit instanceof HostRaycast.Hit h){out.addProperty("target",h.slot);out.addProperty("model",h.model);}
                else out.addProperty("target",-1);
                var obstruction=ProjectileUtil.getEntityHitResult(player,start,hit.getLocation(),
                    new AABB(start,hit.getLocation()).inflate(1),e->!e.isSpectator()&&e.isPickable(),start.distanceToSqr(hit.getLocation()));
                out.addProperty("entityObstruction",obstruction==null?null:obstruction.getEntity().getType().toString());
            }
            var temporary=status.resolveSibling(status.getFileName()+".tmp");
            Files.writeString(temporary,out.toString());Files.move(temporary,status,StandardCopyOption.REPLACE_EXISTING);
            consecutiveWriteFailures=0;
        }catch(java.nio.file.FileSystemException race){
            // Windows readers can momentarily deny replacement. Keep the last
            // complete sample and retry on the next tick, with a bounded limit.
            ++statusRetries;
            if(++consecutiveWriteFailures>40)throw new IllegalStateException("Mining fixture status remains unavailable",race);
        }catch(Exception failure){throw new IllegalStateException("Mining fixture status failed",failure);}
    }
}
