package dev.goldcraft.net;

import java.util.UUID;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Configuration buffers do not yet have a registry manager. */
public record CsAdmissionChallengePayload(UUID nonce, long world) implements CustomPayload {
    public static final Id<CsAdmissionChallengePayload> ID = new Id<>(Identifier.of("goldcraft", "cs_admission_challenge"));
    public static final PacketCodec<PacketByteBuf, CsAdmissionChallengePayload> CODEC = PacketCodec.of(
        (payload, buffer) -> { buffer.writeUuid(payload.nonce()); buffer.writeLong(payload.world()); },
        buffer -> new CsAdmissionChallengePayload(buffer.readUuid(), buffer.readLong()));
    @Override public Id<? extends CustomPayload> getId() { return ID; }
}
