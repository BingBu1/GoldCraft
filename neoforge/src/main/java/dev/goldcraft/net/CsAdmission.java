package dev.goldcraft.net;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import java.util.UUID;
import java.util.function.Consumer;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerConfigurationNetworkHandler;
import net.minecraft.server.network.ServerPlayerConfigurationTask;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.text.Text;
import net.neoforged.neoforge.network.configuration.ICustomConfigurationTask;
import net.neoforged.neoforge.network.event.RegisterConfigurationTasksEvent;
import net.neoforged.neoforge.network.event.RegisterPayloadHandlersEvent;
import net.neoforged.neoforge.network.handling.IPayloadContext;

/** Optional in development; required by the public deployment launcher's environment. */
public final class CsAdmission {
    public static final int VERSION = 1;
    private static final boolean REQUIRED = required(System.getenv("GOLDCRAFT_REQUIRE_CS_PAIR"));
    private static final ServerPlayerConfigurationTask.Key KEY = new ServerPlayerConfigurationTask.Key("goldcraft:cs_admission");
    private static final CsAdmissionLedger LEDGER = new CsAdmissionLedger((authority, player, identity) ->
        authority.equals(authority()) && GoldCraft.sendToHost(Wire.PAIR_PLAYER,
            new Wire.Writer().bytes(identity).bytes(Wire.uuid(player)).toByteArray()), UUID::randomUUID);
    private static MinecraftServer server;
    private CsAdmission() {}

    static boolean required(String value) {
        if (value == null || value.isBlank() || value.equals("0")) return false;
        if (value.equals("1")) return true;
        throw new IllegalArgumentException("GOLDCRAFT_REQUIRE_CS_PAIR must be 0 or 1");
    }
    private static CsAdmissionLedger.Authority authority() {
        return new CsAdmissionLedger.Authority(GoldCraft.currentAuthorityGeneration(), GoldCraft.HOST_WORLD.epoch());
    }
    public static void started(MinecraftServer value) {
        if (REQUIRED && !value.isOnlineMode())
            throw new IllegalStateException("CS admission requires online-mode=true");
        server = value;
    }
    public static void stopped() { if (REQUIRED) LEDGER.reset("The server stopped."); server = null; }
    public static void reset() { if (REQUIRED) LEDGER.retireAuthority("The CS map or authority changed during admission. Retry joining.", System.nanoTime()); }
    public static void tick() { if (REQUIRED) LEDGER.tick(authority(), System.nanoTime()); }

    public static void registerPayloads(RegisterPayloadHandlersEvent event) {
        // Optional negotiation preserves the old local development flow when the gate is off.
        // The required task explicitly rejects missing channels before completing configuration.
        var registrar = event.registrar("cs-admission-" + VERSION).optional();
        registrar.configurationToClient(CsAdmissionChallengePayload.ID, CsAdmissionChallengePayload.CODEC, HostNetwork::receiveAdmission);
        registrar.configurationToServer(CsAdmissionReplyPayload.ID, CsAdmissionReplyPayload.CODEC, CsAdmission::reply);
    }
    public static void registerTasks(RegisterConfigurationTasksEvent event) {
        if (!REQUIRED) return;
        if (!(event.getListener() instanceof ServerConfigurationNetworkHandler listener)) {
            event.getListener().disconnect(Text.literal("Unsupported CS admission listener.")); return;
        }
        event.register(new ICustomConfigurationTask() {
            @Override public ServerPlayerConfigurationTask.Key getKey() { return KEY; }
            @Override public void run(Consumer<CustomPayload> sender) {
                if (server == null || !server.isOnlineMode()
                    || !listener.hasChannel(CsAdmissionChallengePayload.ID) || !listener.hasChannel(CsAdmissionReplyPayload.ID)) {
                    listener.disconnect(Text.literal("This server requires the matching GoldCraft client and an authenticated CS connection.")); return;
                }
                // In the pinned mapped class this public method directly returns getProfile();
                // it exposes the configuration profile validated by vanilla online login.
                UUID id = listener.getDebugProfile().getId();
                if (server.getPlayerManager().getPlayer(id) != null) {
                    listener.disconnect(Text.literal("This Minecraft account is already in the server.")); return;
                }
                LEDGER.start(id, listener.getConnection(), new CsAdmissionLedger.Peer() {
                    @Override public boolean open() { return listener.getConnection().isOpen(); }
                    @Override public void challenge(UUID nonce, long world) { sender.accept(new CsAdmissionChallengePayload(nonce, world)); }
                    @Override public void accept() { listener.onTaskFinished(KEY); }
                    @Override public void reject(String reason) { listener.disconnect(Text.literal(reason)); }
                }, authority(), System.nanoTime());
            }
        });
    }
    private static void reply(CsAdmissionReplyPayload payload, IPayloadContext context) {
        if (!REQUIRED || !(context.listener() instanceof ServerConfigurationNetworkHandler listener)) {
            context.disconnect(Text.literal("Unexpected CS admission reply.")); return;
        }
        LEDGER.reply(listener.getDebugProfile().getId(), context.connection(), payload.nonce(), payload.identity(), authority(), System.nanoTime());
    }
    public static boolean joined(ServerPlayerEntity player) {
        if (!REQUIRED) return true;
        var handler = player.networkHandler;
        if (LEDGER.joined(player.getUuid(), handler.getConnection(), new CsAdmissionLedger.Peer() {
            @Override public boolean open() { return handler.getConnection().isOpen(); }
            @Override public void challenge(UUID nonce, long world) { throw new IllegalStateException("Already in PLAY"); }
            @Override public void accept() { throw new IllegalStateException("Already in PLAY"); }
            @Override public void reject(String reason) { handler.disconnect(Text.literal(reason)); }
        }, authority(), System.nanoTime())) return true;
        player.networkHandler.disconnect(Text.literal("CS admission was not completed. Join CS before Minecraft."));
        return false;
    }
    public static void disconnected(ServerPlayerEntity player) {
        if (REQUIRED) LEDGER.disconnected(player.getUuid(), player.networkHandler.getConnection());
    }
    /** Return true when the gate owns this request, including a deliberately rejected one. */
    public static boolean playBinding(ServerPlayerEntity player, byte[] identity) {
        if (!REQUIRED) return false;
        LEDGER.play(player.getUuid(), player.networkHandler.getConnection(), identity, authority(), System.nanoTime());
        return true;
    }
    /** Return true only when GoldCraft may deliver this result to an existing play connection. */
    public static boolean deliverResult(byte[] bytes) {
        return !REQUIRED || LEDGER.result(CsAdmissionLedger.Result.decode(bytes), authority(), System.nanoTime());
    }
}
