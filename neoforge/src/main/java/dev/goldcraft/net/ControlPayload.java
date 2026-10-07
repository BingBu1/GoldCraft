package dev.goldcraft.net;

import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** A paired player's readiness; positions still come from the Minecraft server's validated entity. */
public record ControlPayload(long epoch,int serial,int life,boolean ready) implements CustomPayload {
    public static final Id<ControlPayload> ID=new Id<>(Identifier.of("goldcraft","control"));
    public static final PacketCodec<RegistryByteBuf,ControlPayload> CODEC=PacketCodec.of(
        (p,b)->b.writeLong(p.epoch).writeInt(p.serial).writeInt(p.life).writeBoolean(p.ready),
        b->new ControlPayload(b.readLong(),b.readInt(),b.readInt(),b.readBoolean()));
    @Override public Id<? extends CustomPayload> getId(){return ID;}
}
