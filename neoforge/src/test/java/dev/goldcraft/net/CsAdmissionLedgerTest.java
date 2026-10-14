package dev.goldcraft.net;

import dev.goldcraft.bridge.Wire;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class CsAdmissionLedgerTest {
    private static final UUID PLAYER = new UUID(1, 2), OTHER = new UUID(3, 4);
    private static final CsAdmissionLedger.Authority AUTHORITY = new CsAdmissionLedger.Authority(7, 19);
    private static final CsAdmissionLedger.Credential IDENTITY = new CsAdmissionLedger.Credential(19, 3, 6, 11, 17);
    private static final class Peer implements CsAdmissionLedger.Peer {
        boolean open = true;
        UUID nonce;
        long world;
        int accepted;
        String rejection;
        public boolean open() { return open; }
        public void challenge(UUID value, long epoch) { nonce = value; world = epoch; }
        public void accept() { accepted++; }
        public void reject(String reason) { rejection = reason; open = false; }
    }
    private record Request(CsAdmissionLedger.Authority authority, UUID player, byte[] identity) {}
    private static final class Fixture {
        final List<Request> sent = new ArrayList<>();
        boolean writable = true;
        long nonceCounter;
        final CsAdmissionLedger ledger = new CsAdmissionLedger((authority, player, bytes) -> {
            if (!writable) return false;
            sent.add(new Request(authority, player, bytes)); return true;
        }, () -> new UUID(0, ++nonceCounter));
        final Object owner = new Object();
        final Peer peer = new Peer();
        void start() { ledger.start(PLAYER, owner, peer, AUTHORITY, 100); }
        void reply() { ledger.reply(PLAYER, owner, peer.nonce, IDENTITY.encode(), AUTHORITY, 101); }
        void ready() { start(); reply(); assertFalse(ledger.result(result(0), AUTHORITY, 102)); }
        void joined() { ready(); assertTrue(ledger.joined(PLAYER, owner, peer, AUTHORITY, 103)); }
    }
    private static CsAdmissionLedger.Result result(int status) { return new CsAdmissionLedger.Result(19, 3, 6, status, PLAYER); }

    @Test void offlineProfileStillNeedsNativeSuccessAndCannotReuseOnlineBinding() {
        UUID offline = UUID.nameUUIDFromBytes("OfflinePlayer:PackageProbe".getBytes(StandardCharsets.UTF_8));
        var f = new Fixture();
        f.ledger.start(offline, f.owner, f.peer, AUTHORITY, 100);
        assertFalse(f.ledger.joined(offline, f.owner, f.peer, AUTHORITY, 101));
        f.ledger.reply(offline, f.owner, f.peer.nonce, IDENTITY.encode(), AUTHORITY, 102);
        assertEquals(0, f.peer.accepted);
        assertEquals(offline, f.sent.getFirst().player());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 103));
        assertEquals(0, f.peer.accepted); // An old online UUID cannot approve this profile.
        f.ledger.result(new CsAdmissionLedger.Result(19, 3, 6, 0, offline), AUTHORITY, 104);
        assertTrue(f.ledger.joined(offline, f.owner, f.peer, AUTHORITY, 105));
        var other = new Peer(); var otherOwner = new Object();
        f.ledger.start(PLAYER, otherOwner, other, AUTHORITY, 106);
        f.ledger.reply(PLAYER, otherOwner, other.nonce, IDENTITY.encode(), AUTHORITY, 107);
        assertNotNull(other.rejection);
        assertEquals(1, f.sent.size());
    }

    @Test void onlyAuthoritativeSuccessCompletesConfigurationAndPlayStillRevalidates() {
        var f = new Fixture(); f.start();
        assertEquals(19, f.peer.world); assertEquals(0, f.peer.accepted); assertTrue(f.sent.isEmpty());
        f.reply(); assertEquals(0, f.peer.accepted);
        assertEquals(PLAYER, f.sent.getFirst().player()); assertArrayEquals(IDENTITY.encode(), f.sent.getFirst().identity());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 102)); assertEquals(1, f.peer.accepted);
        assertTrue(f.ledger.joined(PLAYER, f.owner, f.peer, AUTHORITY, 103));
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), AUTHORITY, 104);
        assertEquals(2, f.sent.size()); assertTrue(f.ledger.result(result(0), AUTHORITY, 105));
        assertFalse(f.ledger.result(result(0), AUTHORITY, 106)); assertEquals(1, f.peer.accepted);
    }
    @Test void missingMalformedAndEmptyCredentialsNeverReachNative() {
        var invalid = List.of(new byte[0], new byte[31], new byte[32], new byte[33],
            new Wire.Writer().i64(19).i32(0).i32(6).i64(11).i64(17).toByteArray(),
            new Wire.Writer().i64(19).i32(33).i32(6).i64(11).i64(17).toByteArray(),
            new Wire.Writer().i64(19).i32(3).i32(0).i64(11).i64(17).toByteArray(),
            new Wire.Writer().i64(19).i32(3).i32(6).i64(0).i64(0).toByteArray());
        for (byte[] bytes : invalid) {
            var f = new Fixture(); f.start();
            f.ledger.reply(PLAYER, f.owner, f.peer.nonce, bytes, AUTHORITY, 101);
            assertNotNull(f.peer.rejection); assertTrue(f.sent.isEmpty()); assertEquals(0, f.ledger.size());
        }
    }
    @Test void oldMapCredentialIsRejectedBeforeSending() {
        var f = new Fixture(); f.start();
        f.ledger.reply(PLAYER, f.owner, f.peer.nonce, new CsAdmissionLedger.Credential(18, 3, 6, 11, 17).encode(), AUTHORITY, 101);
        assertNotNull(f.peer.rejection); assertTrue(f.sent.isEmpty());
    }
    @Test void allNativeRejectionStatusesDenyAdmission() {
        for (int status = 1; status <= 7; status++) {
            var f = new Fixture(); f.start(); f.reply();
            assertFalse(f.ledger.result(result(status), AUTHORITY, 102));
            assertNotNull(f.peer.rejection); assertEquals(0, f.peer.accepted); assertEquals(0, f.ledger.size());
        }
    }
    @Test void unmatchingWorldSlotSerialAccountOrGenerationCannotFinishAnotherCheck() {
        var f = new Fixture(); f.start(); f.reply();
        for (var wrong : List.of(new CsAdmissionLedger.Result(20, 3, 6, 0, PLAYER),
            new CsAdmissionLedger.Result(19, 4, 6, 0, PLAYER), new CsAdmissionLedger.Result(19, 3, 7, 0, PLAYER),
            new CsAdmissionLedger.Result(19, 3, 6, 0, OTHER))) assertFalse(f.ledger.result(wrong, AUTHORITY, 102));
        assertFalse(f.ledger.result(result(0), new CsAdmissionLedger.Authority(6, 19), 102));
        assertEquals(0, f.peer.accepted); assertNull(f.peer.rejection);
        assertFalse(f.ledger.result(result(0), AUTHORITY, 103)); assertEquals(1, f.peer.accepted);
    }
    @Test void duplicateAccountDoesNotReplaceOriginalPendingOwner() {
        var f = new Fixture(); f.start();
        var duplicate = new Peer(); var otherOwner = new Object();
        f.ledger.start(PLAYER, otherOwner, duplicate, AUTHORITY, 101);
        assertNotNull(duplicate.rejection); assertNull(f.peer.rejection);
        f.ledger.reply(PLAYER, otherOwner, f.peer.nonce, IDENTITY.encode(), AUTHORITY, 102);
        assertTrue(f.sent.isEmpty()); f.reply(); assertEquals(1, f.sent.size());
    }
    @Test void staleNonceRejectsAndDoesNotReachNative() {
        var f = new Fixture(); f.start();
        f.ledger.reply(PLAYER, f.owner, new UUID(99, 99), IDENTITY.encode(), AUTHORITY, 101);
        assertNotNull(f.peer.rejection); assertTrue(f.sent.isEmpty());
    }
    @Test void repeatedReplyClosesConnectionAndRetainsResponseTombstone() {
        var f = new Fixture(); f.start(); f.reply(); f.reply();
        assertNotNull(f.peer.rejection); assertEquals(1, f.sent.size()); assertEquals(1, f.ledger.size());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 103)); assertEquals(0, f.peer.accepted); assertEquals(0, f.ledger.size());
    }
    @Test void canceledCheckCannotAuthorizeNewOwnerWithForgedTokenViaLateSuccess() {
        var f = new Fixture(); f.start(); f.reply(); f.peer.open = false;
        f.ledger.tick(AUTHORITY, 102);
        var second = new Peer(); var secondOwner = new Object();
        f.ledger.start(PLAYER, secondOwner, second, AUTHORITY, 103);
        assertNotNull(second.rejection); assertEquals(1, f.ledger.size());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 104)); assertEquals(0, second.accepted);
        var third = new Peer(); var thirdOwner = new Object();
        f.ledger.start(PLAYER, thirdOwner, third, AUTHORITY, 105);
        byte[] forged = new CsAdmissionLedger.Credential(19, 3, 6, 12, 17).encode();
        f.ledger.reply(PLAYER, thirdOwner, third.nonce, forged, AUTHORITY, 106);
        assertNotNull(third.rejection); assertEquals(1, f.sent.size());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 107)); assertEquals(0, third.accepted);
    }
    @Test void timedOutRequestCannotBeReusedUntilNativeResponseOrAuthorityReset() {
        var f = new Fixture(); f.start(); f.reply();
        long expired = CsAdmissionLedger.TIMEOUT_NANOS + 200;
        f.ledger.tick(AUTHORITY, expired); assertNotNull(f.peer.rejection); assertEquals(1, f.ledger.size());
        f.ledger.tick(AUTHORITY, expired * 100); assertEquals(1, f.ledger.size());
        var duplicate = new Peer(); f.ledger.start(PLAYER, new Object(), duplicate, AUTHORITY, expired * 100);
        assertNotNull(duplicate.rejection);
        f.ledger.reset("Authority retired"); assertEquals(0, f.ledger.size());
    }
    @Test void lateSuccessAtTimeoutBoundaryCannotAcceptEvenBeforeTick() {
        var f = new Fixture(); f.start(); f.reply();
        assertFalse(f.ledger.result(result(0), AUTHORITY, 101 + CsAdmissionLedger.TIMEOUT_NANOS));
        assertEquals(0, f.peer.accepted); assertNotNull(f.peer.rejection);
    }
    @Test void playRequestAlsoLeavesTombstoneAcrossDisconnect() {
        var f = new Fixture(); f.joined();
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), AUTHORITY, 104);
        f.ledger.disconnected(PLAYER, f.owner);
        var newPeer = new Peer(); f.ledger.start(PLAYER, new Object(), newPeer, AUTHORITY, 105);
        assertNotNull(newPeer.rejection); assertFalse(f.ledger.result(result(0), AUTHORITY, 106));
        assertEquals(0, f.ledger.size());
    }
    @Test void repeatedPlayBindingOnlySendsOneRequestAndTimesOutFailClosed() {
        var f = new Fixture(); f.joined();
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), AUTHORITY, 104);
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), AUTHORITY, 105);
        assertEquals(2, f.sent.size());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 104 + CsAdmissionLedger.TIMEOUT_NANOS));
        assertNotNull(f.peer.rejection);
    }
    @Test void bridgeQueueFailureDoesNotLeaveAnUnsentTombstone() {
        var f = new Fixture(); f.start(); f.writable = false; f.reply();
        assertNotNull(f.peer.rejection); assertEquals(0, f.ledger.size()); assertTrue(f.sent.isEmpty());
    }
    @Test void bridgeDisconnectInvalidatesPendingAndReadyConnections() {
        for (boolean ready : List.of(false, true)) {
            var f = new Fixture(); if (ready) f.ready(); else { f.start(); f.reply(); }
            f.ledger.tick(new CsAdmissionLedger.Authority(0, 19), 103);
            assertNotNull(f.peer.rejection);
            f.ledger.reset("Bridge retired"); assertEquals(0, f.ledger.size());
        }
    }
    @Test void boundQueueCannotGrowWithAccountsWithoutAResponse() {
        var f = new Fixture();
        for (int i = 1; i <= CsAdmissionLedger.MAX_CLIENTS; i++)
            f.ledger.start(new UUID(0, i), new Object(), new Peer(), AUTHORITY, 100);
        var overflow = new Peer(); f.ledger.start(PLAYER, new Object(), overflow, AUTHORITY, 100);
        assertNotNull(overflow.rejection); assertEquals(32, f.ledger.size());
        f.ledger.tick(AUTHORITY, 100 + CsAdmissionLedger.TIMEOUT_NANOS); assertEquals(0, f.ledger.size());
    }
    @Test void joiningRequiresTheSameActualConnectionAndCompletedConfiguration() {
        var f = new Fixture(); f.start();
        assertFalse(f.ledger.joined(PLAYER, f.owner, f.peer, AUTHORITY, 101)); f.reply();
        f.ledger.result(result(0), AUTHORITY, 102);
        assertFalse(f.ledger.joined(PLAYER, new Object(), f.peer, AUTHORITY, 103));
        assertTrue(f.ledger.joined(PLAYER, f.owner, f.peer, AUTHORITY, 103));
        assertFalse(f.ledger.joined(PLAYER, f.owner, f.peer, AUTHORITY, 104));
    }
    @Test void ordinaryReconnectWithSameValidatedCsConnectionIsAllowed() {
        var f = new Fixture(); f.joined(); f.ledger.disconnected(PLAYER, f.owner);
        var peer = new Peer(); var owner = new Object(); f.ledger.start(PLAYER, owner, peer, AUTHORITY, 105);
        f.ledger.reply(PLAYER, owner, peer.nonce, IDENTITY.encode(), AUTHORITY, 106);
        f.ledger.result(result(0), AUTHORITY, 107);
        assertEquals(1, peer.accepted); assertNull(peer.rejection);
    }
    @Test void newNativeSerialCanPresentFreshTokenButMustBeValidatedAgain() {
        var f = new Fixture(); f.joined(); f.ledger.disconnected(PLAYER, f.owner);
        var peer = new Peer(); var owner = new Object(); f.ledger.start(PLAYER, owner, peer, AUTHORITY, 105);
        var fresh = new CsAdmissionLedger.Credential(19, 3, 7, 44, 55);
        f.ledger.reply(PLAYER, owner, peer.nonce, fresh.encode(), AUTHORITY, 106);
        assertEquals(0, peer.accepted); assertEquals(2, f.sent.size());
        assertFalse(f.ledger.result(result(0), AUTHORITY, 107)); assertEquals(0, peer.accepted);
        f.ledger.result(new CsAdmissionLedger.Result(19, 3, 7, 0, PLAYER), AUTHORITY, 108);
        assertEquals(1, peer.accepted);
    }
    @Test void mapResetPreservesExistingPlayConnectionAndRequiresFreshNativeBinding() {
        var f = new Fixture(); f.joined();
        f.ledger.retireAuthority("Map changed", 110);
        assertTrue(f.peer.open); assertNull(f.peer.rejection);
        var next = new CsAdmissionLedger.Authority(7, 20);
        f.ledger.tick(new CsAdmissionLedger.Authority(0, 0), 111);
        assertTrue(f.peer.open);
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), next, 112);
        assertEquals(1, f.sent.size());
        var fresh = new CsAdmissionLedger.Credential(20, 3, 9, 55, 66);
        f.ledger.play(PLAYER, f.owner, fresh.encode(), next, 113);
        assertEquals(2, f.sent.size());
        assertFalse(f.ledger.result(result(0), next, 114));
        assertTrue(f.ledger.result(new CsAdmissionLedger.Result(20, 3, 9, 0, PLAYER), next, 115));
        assertEquals(1, f.peer.accepted); // No second configuration task completion.
        assertTrue(f.peer.open);
    }
    @Test void resetDeniesUnfinishedConfigurationWhileRetainingAdmittedPlay() {
        var f = new Fixture(); f.joined();
        var pending = new Peer(); f.ledger.start(OTHER, new Object(), pending, AUTHORITY, 105);
        f.ledger.retireAuthority("Map changed", 106);
        assertTrue(f.peer.open); assertNotNull(pending.rejection); assertEquals(1, f.ledger.size());
        var competing = new Peer(); f.ledger.start(PLAYER, new Object(), competing, AUTHORITY, 107);
        assertNotNull(competing.rejection); // A new owner cannot inherit the old admission.
    }
    @Test void reconnectGraceHasAHardDeadlineThatRetriesAndRepeatedResetsCannotExtend() {
        var f = new Fixture(); f.joined(); f.ledger.retireAuthority("Bridge retired", 110);
        long deadline = 110 + CsAdmissionLedger.TIMEOUT_NANOS;
        f.ledger.retireAuthority("Waiting for replacement world", deadline - 2);
        f.ledger.tick(new CsAdmissionLedger.Authority(0, 0), deadline - 1); assertTrue(f.peer.open);
        f.ledger.tick(new CsAdmissionLedger.Authority(0, 0), deadline); assertNotNull(f.peer.rejection);
    }
    @Test void nativeDisconnectDuringPlayAllowsBoundedCsReconnectWithoutNewMinecraftLogin() {
        var f = new Fixture(); f.joined();
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), AUTHORITY, 110);
        assertFalse(f.ledger.result(result(2), AUTHORITY, 111)); assertTrue(f.peer.open);
        var fresh = new CsAdmissionLedger.Credential(19, 3, 8, 55, 66);
        f.ledger.play(PLAYER, f.owner, fresh.encode(), AUTHORITY, 112);
        assertTrue(f.ledger.result(new CsAdmissionLedger.Result(19, 3, 8, 0, PLAYER), AUTHORITY, 113));
        assertTrue(f.peer.open); assertEquals(1, f.peer.accepted);
    }
    @Test void admittedClientThatStopsSendingBindingCannotRemainIndefinitely() {
        var f = new Fixture(); f.joined();
        f.ledger.tick(AUTHORITY, 103 + CsAdmissionLedger.TIMEOUT_NANOS - 1); assertTrue(f.peer.open);
        f.ledger.tick(AUTHORITY, 103 + CsAdmissionLedger.TIMEOUT_NANOS); assertNotNull(f.peer.rejection);
        assertEquals(0, f.ledger.size());
    }
    @Test void successfulPeriodicNativeValidationRenewsThePlayDeadline() {
        var f = new Fixture(); f.joined();
        long check = 103 + CsAdmissionLedger.TIMEOUT_NANOS - 2;
        f.ledger.play(PLAYER, f.owner, IDENTITY.encode(), AUTHORITY, check);
        assertTrue(f.ledger.result(result(0), AUTHORITY, check + 1));
        f.ledger.tick(AUTHORITY, 103 + CsAdmissionLedger.TIMEOUT_NANOS); assertTrue(f.peer.open);
        f.ledger.tick(AUTHORITY, check + 1 + CsAdmissionLedger.TIMEOUT_NANOS); assertNotNull(f.peer.rejection);
    }
    @Test void joinedReplacesConfigurationCallbacksWithTheActualPlayListener() {
        var f = new Fixture(); f.ready(); var play = new Peer();
        assertTrue(f.ledger.joined(PLAYER, f.owner, play, AUTHORITY, 103));
        f.ledger.tick(AUTHORITY, 103 + CsAdmissionLedger.TIMEOUT_NANOS);
        assertNotNull(play.rejection); assertNull(f.peer.rejection);
        assertEquals(1, f.peer.accepted); assertEquals(0, play.accepted); assertNull(play.nonce);
    }
    @Test void credentialCodecMatchesNativeLittleEndianAndHidesBearerToken() {
        byte[] encoded = IDENTITY.encode();
        assertEquals(32, encoded.length); assertEquals(19, encoded[0]); assertEquals(3, encoded[8]);
        assertEquals(6, encoded[12]); assertEquals(11, encoded[16]); assertEquals(17, encoded[24]);
        assertEquals(IDENTITY, CsAdmissionLedger.Credential.decode(encoded));
        assertFalse(IDENTITY.toString().contains("token"));
        byte[] pair = new Wire.Writer().i64(19).i32(3).i32(6).i32(0).bytes(Wire.uuid(PLAYER)).toByteArray();
        assertEquals(result(0), CsAdmissionLedger.Result.decode(pair));
        assertThrows(IllegalArgumentException.class, () -> CsAdmissionLedger.Result.decode(new byte[35]));
    }
}
