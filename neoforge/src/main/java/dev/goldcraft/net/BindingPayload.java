package dev.goldcraft.net;

import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Delivered through the actual CS user-message channel, then associated with the authenticated MC player. */
public record BindingPayload(byte[] identity) implements CustomPayload {
    public static final Id<BindingPayload> ID=new Id<>(Identifier.of("goldcraft","binding"));
    public static final PacketCodec<RegistryByteBuf,BindingPayload> CODEC=PacketCodec.of(
        (payload,buffer)->buffer.writeBytes(payload.identity()),
        buffer->{ byte[] bytes=new byte[32]; buffer.readBytes(bytes); return new BindingPayload(bytes); });
    public BindingPayload {
        if (identity.length!=32) throw new IllegalArgumentException("Binding length");
        identity=identity.clone();
    }
    @Override public Id<? extends CustomPayload> getId() { return ID; }
}
