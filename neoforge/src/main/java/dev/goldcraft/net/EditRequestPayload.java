package dev.goldcraft.net;

import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Read-only recovery request, never a client-selected map modification. */
public record EditRequestPayload(long epoch) implements CustomPayload {
    public static final Id<EditRequestPayload> ID=new Id<>(Identifier.of("goldcraft","map_edit_query"));
    public static final PacketCodec<RegistryByteBuf,EditRequestPayload> CODEC=PacketCodec.of(
        (p,b)->b.writeLong(p.epoch),b->new EditRequestPayload(b.readLong()));
    @Override public Id<? extends CustomPayload> getId(){return ID;}
}
