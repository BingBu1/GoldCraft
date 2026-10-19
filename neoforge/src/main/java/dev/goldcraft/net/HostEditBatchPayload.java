package dev.goldcraft.net;

import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.bridge.Wire;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Optional channel: the original host channel alone does not promise batch support. */
public record HostEditBatchPayload(HostPayload fragment) implements CustomPayload {
    public static final Id<HostEditBatchPayload> ID=new Id<>(Identifier.of("goldcraft","host_edit_batch"));
    public static final PacketCodec<RegistryByteBuf,HostEditBatchPayload> CODEC=PacketCodec.of(
        (p,b)->HostPayload.CODEC.encode(b,p.fragment),
        b->new HostEditBatchPayload(HostPayload.CODEC.decode(b)));
    public HostEditBatchPayload {
        if(fragment.type()!=Wire.MAP_EDIT_BATCH||fragment.total()<72||fragment.total()>28+44*MapEdits.MAX_CUTS||fragment.data().length==0)
            throw new IllegalArgumentException("Host edit batch fragment bounds");
    }
    @Override public Id<? extends CustomPayload> getId(){return ID;}
}
