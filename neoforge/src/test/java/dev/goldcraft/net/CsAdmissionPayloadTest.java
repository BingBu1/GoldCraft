package dev.goldcraft.net;

import io.netty.buffer.Unpooled;
import java.util.UUID;
import net.minecraft.network.PacketByteBuf;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class CsAdmissionPayloadTest {
    @Test void configurationPayloadsUseRegistryFreeFixedSizeCodecs() {
        UUID nonce = new UUID(1, 2);
        var buffer = new PacketByteBuf(Unpooled.buffer());
        try {
            var challenge = new CsAdmissionChallengePayload(nonce, 19);
            CsAdmissionChallengePayload.CODEC.encode(buffer, challenge);
            assertEquals(24, buffer.readableBytes());
            assertEquals(challenge, CsAdmissionChallengePayload.CODEC.decode(buffer));
            assertEquals(0, buffer.readableBytes());
            buffer.clear();
            byte[] identity = new CsAdmissionLedger.Credential(19, 3, 6, 11, 17).encode();
            CsAdmissionReplyPayload.CODEC.encode(buffer, new CsAdmissionReplyPayload(nonce, identity));
            assertEquals(48, buffer.readableBytes());
            var reply = CsAdmissionReplyPayload.CODEC.decode(buffer);
            assertEquals(nonce, reply.nonce()); assertArrayEquals(identity, reply.identity());
            assertEquals(0, buffer.readableBytes());
        } finally { buffer.release(); }
    }
    @Test void responseOwnsItsCredentialBytes() {
        byte[] input = new CsAdmissionLedger.Credential(19, 3, 6, 11, 17).encode();
        var payload = new CsAdmissionReplyPayload(new UUID(1, 2), input);
        input[0] = 0; assertEquals(19, payload.identity()[0]);
        byte[] copy = payload.identity(); copy[0] = 0; assertEquals(19, payload.identity()[0]);
        assertThrows(IllegalArgumentException.class, () -> new CsAdmissionReplyPayload(new UUID(1, 2), new byte[31]));
    }
}
