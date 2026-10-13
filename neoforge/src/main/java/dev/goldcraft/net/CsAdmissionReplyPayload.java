package dev.goldcraft.net;

import java.util.UUID;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** No UUID claim: the account always comes from the authenticated configuration listener. */
public record CsAdmissionReplyPayload(UUID nonce, byte[] identity) implements CustomPayload {
    public static final Id<CsAdmissionReplyPayload> ID = new Id<>(Identifier.of("goldcraft", "cs_admission_reply"));
    public static final PacketCodec<PacketByteBuf, CsAdmissionReplyPayload> CODEC = PacketCodec.of(
        (payload, buffer) -> { buffer.writeUuid(payload.nonce()); buffer.writeBytes(payload.identity); },
        buffer -> { UUID nonce = buffer.readUuid(); byte[] identity = new byte[32]; buffer.readBytes(identity); return new CsAdmissionReplyPayload(nonce, identity); });
    public CsAdmissionReplyPayload {
        if (identity == null || identity.length != 32) throw new IllegalArgumentException("CS identity length");
        identity = identity.clone();
    }
    @Override public byte[] identity() { return identity.clone(); }
    @Override public Id<? extends CustomPayload> getId() { return ID; }
}
