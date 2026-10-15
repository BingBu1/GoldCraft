package dev.goldcraft.test;

import com.mojang.authlib.GameProfile;
import io.netty.channel.embedded.EmbeddedChannel;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.players.PlayerList;
import net.minecraft.world.level.GameType;
import net.neoforged.neoforge.common.util.FakePlayer;

/** Test-owned player-list simulation, never a network login or client-input test. */
final class PairedTntPlayer {
    private FakePlayer player;
    private EmbeddedChannel channel;
    private int slot, ticks, nextPair;
    private String error;

    boolean accepts(MinecraftServer server) {
        return server.getPlayerList().getPlayers().stream().allMatch(p -> p == player);
    }

    private static Object host() throws ReflectiveOperationException {
        return Class.forName("dev.goldcraft.GoldCraft").getField("HOST_WORLD").get(null);
    }

    private static Object call(Object object, String method) throws ReflectiveOperationException {
        return object.getClass().getMethod(method).invoke(object);
    }

    private Object actor() throws ReflectiveOperationException {
        return player == null ? null : host().getClass().getMethod("actor", UUID.class).invoke(host(), player.getUUID());
    }

    // Field names checked against pinned MC 1.21.1 Mojang mappings, as in MapMiningProbe.
    @SuppressWarnings("unchecked")
    private void register(MinecraftServer server, boolean add) throws ReflectiveOperationException {
        var listField = PlayerList.class.getDeclaredField("players");
        var mapField = PlayerList.class.getDeclaredField("playersByUUID");
        listField.setAccessible(true); mapField.setAccessible(true);
        var list = (List<ServerPlayer>) listField.get(server.getPlayerList());
        var map = (Map<UUID, ServerPlayer>) mapField.get(server.getPlayerList());
        if (add) { list.add(player); map.put(player.getUUID(), player); }
        else { list.remove(player); map.remove(player.getUUID(), player); }
    }

    void create(MinecraftServer server, ServerLevel level, int selectedSlot) throws Exception {
        if (player != null || !accepts(server) || System.getenv("GOLDCRAFT_HEADLESS_BINDINGS") == null)
            throw new IllegalStateException("Explicit headless delivery and an empty MC server required");
        slot = selectedSlot;
        player = new FakePlayer(level, new GameProfile(UUID.randomUUID(), "TntPairProbe"));
        channel = new EmbeddedChannel(player.connection.getConnection());
        player.setGameMode(GameType.SURVIVAL);
        register(server, true);
    }

    void remove(MinecraftServer server) throws Exception {
        if (!accepts(server)) throw new IllegalStateException("Unexpected real player");
        if (player != null) { register(server, false); player.discard(); player = null; }
        if (channel != null) { channel.finishAndReleaseAll(); channel = null; }
    }

    void tick(MinecraftServer server) {
        ++ticks;
        if (player == null || error != null) return;
        try {
            if (!accepts(server)) throw new IllegalStateException("Unexpected real player");
            Object world = host(); long epoch = (long) call(world, "epoch");
            Object actor = actor();
            if (actor == null) {
                if (ticks < nextPair) return;
                nextPair = ticks + 20;
                Path path = Path.of(System.getenv("GOLDCRAFT_HEADLESS_BINDINGS"));
                if (!Files.exists(path)) return;
                byte[] file;
                try { file = Files.readAllBytes(path); }
                catch (java.nio.file.NoSuchFileException replaced) { return; }
                var bytes = ByteBuffer.wrap(file).order(ByteOrder.LITTLE_ENDIAN);
                if (bytes.remaining() < 12 || bytes.getLong() != epoch) return;
                int count = bytes.getInt();
                if (count < 1 || count > 32 || bytes.remaining() != count * 24) return;
                for (int i = 0; i < count; ++i) {
                    int candidate = bytes.getInt(), serial = bytes.getInt();
                    byte[] token = new byte[16]; bytes.get(token);
                    if (candidate != slot) continue;
                    // Same packet encoding as Wire.Writer and Wire.uuid; token stays local.
                    byte[] uuid = ByteBuffer.allocate(16).putLong(player.getUUID().getMostSignificantBits())
                        .putLong(player.getUUID().getLeastSignificantBits()).array();
                    byte[] payload = ByteBuffer.allocate(48).order(ByteOrder.LITTLE_ENDIAN)
                        .putLong(epoch).putInt(slot).putInt(serial).put(token).put(uuid).array();
                    int kind = Class.forName("dev.goldcraft.bridge.Wire").getField("PAIR_PLAYER").getInt(null);
                    Class.forName("dev.goldcraft.GoldCraft").getMethod("sendToHost", int.class, byte[].class)
                        .invoke(null, kind, payload);
                    break;
                }
                return;
            }
            if ((int) call(actor, "slot") != slot) throw new IllegalStateException("Unexpected paired slot");
            Object feet = call(actor, "minecraftFeet");
            // FakePlayer does not tick locomotion. Place at the authoritative feet;
            // native life/form changes still pass through the real HostSession.
            player.moveTo(((Number) call(feet, "x")).doubleValue(),
                ((Number) call(feet, "y")).doubleValue(), ((Number) call(feet, "z")).doubleValue());
            Class.forName("dev.goldcraft.world.HostSession")
                .getMethod("ready", UUID.class, long.class, int.class, int.class, boolean.class)
                .invoke(null, player.getUUID(), epoch, call(actor, "serial"), call(actor, "life"), true);
        } catch (Exception failure) { error = failure.toString(); failure.printStackTrace(); }
    }

    FakePlayer owner() throws Exception {
        if (error != null || player == null || !player.isAlive() || player.isRemoved() || actor() == null)
            throw new IllegalStateException("No live fixture-owned paired player: " + error);
        return player;
    }

    Map<String, Object> report() throws Exception {
        Map<String, Object> value = new LinkedHashMap<>();
        value.put("tick", ticks); value.put("error", error); value.put("slot", slot);
        if (player == null) return value;
        value.put("uuid", player.getUUID().toString()); value.put("objectIdentity", System.identityHashCode(player));
        value.put("alive", player.isAlive()); value.put("registered", player.getServer().getPlayerList().getPlayer(player.getUUID()) == player);
        Object actor = actor();
        if (actor != null) for (String name : List.of("serial", "life", "spawn", "flags")) value.put(name, call(actor, name));
        return value;
    }
}
