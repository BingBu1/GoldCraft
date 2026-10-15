package dev.goldcraft.net;

import java.util.Objects;
import java.util.function.Consumer;
import java.util.function.Supplier;
import net.minecraft.text.Text;
import net.neoforged.neoforge.network.handling.IPayloadContext;

/** Client callbacks stay out of classes loaded by a dedicated server. */
public final class HostNetwork {
    private static Consumer<HostPayload> receiver = payload -> {};
    private static Supplier<byte[]> localBinding = () -> null;
    private static Supplier<String> localPairingProblem = () -> "Join the matching CS server first, then reconnect to Minecraft.";
    private static CsAdmissionChallengePayload admission;
    private static IPayloadContext admissionContext;
    private static IPayloadContext pairingContext;
    private static long admissionDeadline;
    private HostNetwork() {}
    public static void clientReceiver(Consumer<HostPayload> value) {
        receiver = Objects.requireNonNull(value);
    }
    public static void receive(HostPayload payload, IPayloadContext context) {
        // RegisterPayloadHandlersEvent defaults to the game thread.
        receiver.accept(payload);
    }
    public static void clientBinding(Supplier<byte[]> value) { localBinding = Objects.requireNonNull(value); }
    public static void clientPairingProblem(Supplier<String> value) { localPairingProblem = Objects.requireNonNull(value); }
    /** Discovery must match the live server challenge, never a guessed address or player name. */
    public static long admissionWorld() {
        return admission!=null&&admissionContext!=null&&admissionContext.connection().isOpen()
            &&admissionContext.connection().getPacketListener()==admissionContext.listener()?admission.world():0;
    }
    /** Keep discovery through the configuration/play handoff, never past disconnect. */
    public static boolean pairingConnectionOpen() {
        if(pairingContext!=null&&!pairingContext.connection().isOpen())pairingContext=null;
        return pairingContext!=null;
    }
    public static void receiveAdmission(CsAdmissionChallengePayload payload, IPayloadContext context) {
        if (admissionContext != null && admissionContext.connection().isOpen()
            && admissionContext.connection().getPacketListener() == admissionContext.listener()) {
            context.disconnect(Text.literal("Repeated CS admission challenge.")); return;
        }
        admission = payload; admissionContext = context; pairingContext = context;
        admissionDeadline = System.nanoTime() + 10_000_000_000L;
        tickAdmission();
    }
    /** Called after draining CLIENT_BINDING even while Minecraft has no world or player. */
    public static void tickAdmission() {
        IPayloadContext context = admissionContext;
        if (context == null) return;
        if (!context.connection().isOpen() || context.connection().getPacketListener() != context.listener()) {
            clearAdmission(); return;
        }
        byte[] identity = localBinding.get();
        if (identity != null) {
            try {
                if (CsAdmissionLedger.Credential.decode(identity).world() == admission.world()) {
                    var response = new CsAdmissionReplyPayload(admission.nonce(), identity);
                    clearAdmission(); context.reply(response); return;
                }
            } catch (IllegalArgumentException invalid) { /* Wait for a fresh local CS binding. */ }
        }
        if (System.nanoTime() - admissionDeadline >= 0) {
            String problem=localPairingProblem.get();
            clearAdmission();
            context.disconnect(Text.literal(problem));
        }
    }
    private static void clearAdmission() { admission = null; admissionContext = null; admissionDeadline = 0; }
}
