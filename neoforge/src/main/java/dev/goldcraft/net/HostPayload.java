package dev.goldcraft.net;

import dev.goldcraft.bridge.Wire;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Bounded server-to-client fragment; world identity remains inside the authenticated message. */
public record HostPayload(int type,int total,int offset,byte[] data) implements CustomPayload {
    public static final int FRAGMENT=24*1024;
    public static final Id<HostPayload> ID=new Id<>(Identifier.of("goldcraft","host"));
    public static final PacketCodec<RegistryByteBuf,HostPayload> CODEC=PacketCodec.of(
        (p,b)->{b.writeVarInt(p.type).writeVarInt(p.total).writeVarInt(p.offset).writeByteArray(p.data);},
        b->new HostPayload(b.readVarInt(),b.readVarInt(),b.readVarInt(),b.readByteArray(FRAGMENT)));
    public HostPayload {
        if(!Wire.knownType(type)||total<0||total>Wire.MAX_PAYLOAD||offset<0||data.length>FRAGMENT||offset>total-data.length)throw new IllegalArgumentException("Host fragment bounds");
        data=data.clone();
    }
    @Override public Id<? extends CustomPayload> getId(){return ID;}
}
