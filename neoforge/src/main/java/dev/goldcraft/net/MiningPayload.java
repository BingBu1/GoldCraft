package dev.goldcraft.net;

import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Held intent only. No client-selected target, coordinates, tool speed or damage. */
public record MiningPayload(long epoch,long revision,int serial,int life,boolean held) implements CustomPayload {
    public static final Id<MiningPayload> ID=new Id<>(Identifier.of("goldcraft","map_mining"));
    public static final PacketCodec<RegistryByteBuf,MiningPayload> CODEC=PacketCodec.of(
        (p,b)->b.writeLong(p.epoch).writeLong(p.revision).writeInt(p.serial).writeInt(p.life).writeBoolean(p.held),
        b->new MiningPayload(b.readLong(),b.readLong(),b.readInt(),b.readInt(),b.readBoolean()));
    @Override public Id<? extends CustomPayload> getId(){return ID;}
}
