package dev.goldcraft.net;

import java.util.Objects;
import java.util.function.Consumer;
import net.neoforged.neoforge.network.handling.IPayloadContext;

/** Client callbacks stay out of classes loaded by a dedicated server. */
public final class HostNetwork {
    private static Consumer<HostPayload> receiver = payload -> {};
    private HostNetwork() {}
    public static void clientReceiver(Consumer<HostPayload> value) {
        receiver = Objects.requireNonNull(value);
    }
    public static void receive(HostPayload payload, IPayloadContext context) {
        // RegisterPayloadHandlersEvent defaults to the game thread.
        receiver.accept(payload);
    }
}
