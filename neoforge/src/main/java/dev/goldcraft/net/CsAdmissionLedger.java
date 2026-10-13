package dev.goldcraft.net;

import dev.goldcraft.bridge.Wire;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.function.Supplier;

/** Server-thread state only; no Minecraft classes, tokens in logs, or client-asserted UUIDs. */
public final class CsAdmissionLedger {
    public static final int MAX_CLIENTS = 32;
    public static final long TIMEOUT_NANOS = 15_000_000_000L;
    public record Authority(long generation, long world) {
        public boolean ready() { return generation != 0 && world != 0; }
    }
    public record Credential(long world, int slot, int serial, long tokenLow, long tokenHigh) {
        public Credential {
            if (world == 0 || slot < 1 || slot > MAX_CLIENTS || serial == 0 || (tokenLow | tokenHigh) == 0)
                throw new IllegalArgumentException("Invalid CS connection identity");
        }
        public static Credential decode(byte[] bytes) {
            if (bytes == null || bytes.length != 32) throw new IllegalArgumentException("CS identity length");
            var reader = new Wire.Reader(bytes);
            var value = new Credential(reader.i64(), reader.i32(), reader.i32(), reader.i64(), reader.i64());
            reader.finish();
            return value;
        }
        public byte[] encode() {
            return new Wire.Writer().i64(world).i32(slot).i32(serial).i64(tokenLow).i64(tokenHigh).toByteArray();
        }
        public boolean sameConnection(Credential other) {
            return world == other.world && slot == other.slot && serial == other.serial;
        }
        // Deliberately exclude the bearer credential from diagnostics and assertion output.
        @Override public String toString() { return "CS identity[slot=" + slot + ", serial=" + serial + "]"; }
    }
    public record Result(long world, int slot, int serial, int status, UUID player) {
        public static Result decode(byte[] bytes) {
            var reader = new Wire.Reader(bytes);
            var value = new Result(reader.i64(), reader.i32(), reader.i32(), reader.i32(), Wire.uuid(reader.bytes(16)));
            reader.finish();
            if (value.status < 0 || value.status > 7) throw new IllegalArgumentException("Pair status");
            return value;
        }
    }
    public interface Peer {
        boolean open();
        void challenge(UUID nonce, long world);
        void accept();
        void reject(String reason);
    }
    @FunctionalInterface public interface Sender {
        boolean send(Authority authority, UUID authenticatedPlayer, byte[] identity);
    }
    private enum Phase { CHALLENGE, VERIFYING, READY, PLAY, REBIND, TOMBSTONE }
    private static final class Entry {
        final UUID player;
        Authority authority;
        final UUID nonce;
        Object owner;
        Peer peer;
        Phase phase = Phase.CHALLENGE;
        Credential credential;
        boolean inFlight;
        long deadline;
        Entry(UUID player, Object owner, Peer peer, Authority authority, UUID nonce, long now) {
            this.player = player; this.owner = owner; this.peer = peer;
            this.authority = authority; this.nonce = nonce; deadline = now + TIMEOUT_NANOS;
        }
    }
    private record Verified(UUID player, Credential credential) {}
    private final Map<UUID, Entry> entries = new HashMap<>();
    // One most recent successful credential per native slot; bounded even across MC account churn.
    private final Map<Integer, Verified> verified = new HashMap<>();
    private final Sender sender;
    private final Supplier<UUID> nonces;
    public CsAdmissionLedger(Sender sender, Supplier<UUID> nonces) { this.sender = sender; this.nonces = nonces; }

    public void start(UUID authenticatedPlayer, Object owner, Peer peer, Authority authority, long now) {
        if (!authority.ready() || authenticatedPlayer == null || authenticatedPlayer.equals(new UUID(0, 0))) {
            peer.reject("The CS authority is unavailable."); return;
        }
        if (entries.containsKey(authenticatedPlayer)) {
            peer.reject("This Minecraft account already has a connection or a pending CS check. Retry shortly."); return;
        }
        if (entries.size() >= MAX_CLIENTS) {
            peer.reject("The CS admission queue is full. Retry shortly."); return;
        }
        Entry entry = new Entry(authenticatedPlayer, owner, peer, authority, nonces.get(), now);
        entries.put(authenticatedPlayer, entry);
        peer.challenge(entry.nonce, authority.world);
    }

    public void reply(UUID authenticatedPlayer, Object owner, UUID nonce, byte[] bytes, Authority authority, long now) {
        Entry entry = entries.get(authenticatedPlayer);
        if (entry == null || entry.owner != owner) return;
        if (entry.phase != Phase.CHALLENGE || !entry.nonce.equals(nonce)) {
            close(entry, "Unexpected or repeated CS admission reply."); return;
        }
        if (!live(entry, authority, now)) { close(entry, "The CS admission check expired."); return; }
        try {
            Credential credential = Credential.decode(bytes);
            if (credential.world != authority.world || conflictsWithVerified(authenticatedPlayer, credential)) {
                close(entry, "The CS connection has changed. Reconnect to CS first."); return;
            }
            entry.credential = credential;
            entry.phase = Phase.VERIFYING;
            entry.deadline = now + TIMEOUT_NANOS;
            entry.inFlight = sender.send(authority, authenticatedPlayer, credential.encode());
            if (!entry.inFlight) close(entry, "The CS authority is unavailable.");
        } catch (IllegalArgumentException invalid) {
            close(entry, "Connect to the matching CS server before joining Minecraft.");
        }
    }

    private boolean conflictsWithVerified(UUID player, Credential credential) {
        Verified previous = verified.get(credential.slot);
        return previous != null && previous.credential.sameConnection(credential)
            && (!previous.player.equals(player) || !previous.credential.equals(credential));
    }

    /** True only for a current, authenticated play request; configuration successes are consumed here. */
    public boolean result(Result result, Authority authority, long now) {
        Entry entry = entries.get(result.player);
        if (entry == null || !entry.inFlight || !entry.authority.equals(authority)
            || entry.credential.slot != result.slot || entry.credential.serial != result.serial
            || result.world != authority.world) return false;
        boolean current = entry.phase != Phase.TOMBSTONE && live(entry, authority, now);
        entry.inFlight = false;
        if (result.status == 0) verified.put(result.slot, new Verified(entry.player, entry.credential));
        if (entry.phase == Phase.TOMBSTONE) { entries.remove(entry.player); return false; }
        if (!current) { close(entry, "The CS admission check expired."); return false; }
        if (result.status != 0) {
            if (entry.phase == Phase.PLAY || entry.phase == Phase.REBIND) rebind(entry, now);
            else close(entry, "The CS server rejected this connection identity.");
            return false;
        }
        if (entry.phase == Phase.VERIFYING) {
            entry.phase = Phase.READY;
            entry.deadline = now + TIMEOUT_NANOS;
            entry.peer.accept();
            return false;
        }
        if (entry.phase == Phase.REBIND) entry.phase = Phase.PLAY;
        if (entry.phase == Phase.PLAY) entry.deadline = now + TIMEOUT_NANOS;
        return entry.phase == Phase.PLAY;
    }

    public boolean joined(UUID player, Object owner, Peer playPeer, Authority authority, long now) {
        Entry entry = entries.get(player);
        if (entry == null || entry.owner != owner || entry.phase != Phase.READY) return false;
        if (!live(entry, authority, now)) { close(entry, "The CS admission check expired."); return false; }
        if (!playPeer.open()) { close(entry, "The Minecraft connection closed."); return false; }
        entry.peer = playPeer;
        entry.phase = Phase.PLAY;
        entry.deadline = now + TIMEOUT_NANOS;
        return true;
    }

    /** Periodic play binding retries share the configuration request lane and can never orphan a response. */
    public void play(UUID player, Object owner, byte[] bytes, Authority authority, long now) {
        Entry entry = entries.get(player);
        if (entry == null || entry.owner != owner || (entry.phase != Phase.PLAY && entry.phase != Phase.REBIND)) return;
        if (entry.phase == Phase.PLAY && !entry.authority.equals(authority)) rebind(entry, now);
        if (!live(entry, authority, now)) { close(entry, "The CS authority is unavailable."); return; }
        if (!authority.ready() || entry.inFlight) return;
        try {
            Credential credential = Credential.decode(bytes);
            if (credential.world != authority.world) return; // The next GCBind follows the map reset.
            if (conflictsWithVerified(player, credential)) {
                close(entry, "The CS connection identity changed unexpectedly."); return;
            }
            if (entry.credential != null && !entry.credential.equals(credential)) rebind(entry, now);
            entry.credential = credential;
            entry.authority = authority;
        } catch (IllegalArgumentException invalid) { close(entry, "Invalid CS connection identity."); return; }
        if (entry.phase == Phase.PLAY) entry.deadline = now + TIMEOUT_NANOS;
        entry.inFlight = sender.send(authority, player, entry.credential.encode());
        if (!entry.inFlight) rebind(entry, now);
    }

    public void disconnected(UUID player, Object owner) {
        Entry entry = entries.get(player);
        if (entry != null && entry.owner == owner) close(entry, "The Minecraft connection closed.");
    }
    public void tick(Authority authority, long now) {
        for (Entry entry : new ArrayList<>(entries.values())) {
            if (entry.phase == Phase.PLAY && !entry.authority.equals(authority)) rebind(entry, now);
            if (entry.phase != Phase.TOMBSTONE && !live(entry, authority, now))
                close(entry, "The CS admission check expired or the CS bridge disconnected.");
        }
    }
    private static boolean live(Entry entry, Authority authority, long now) {
        if (entry.peer == null || !entry.peer.open()) return false;
        if (entry.phase == Phase.REBIND) return now - entry.deadline < 0;
        return authority.ready() && entry.authority.equals(authority) && now - entry.deadline < 0;
    }
    private static void rebind(Entry entry, long now) {
        if (entry.phase != Phase.REBIND) entry.deadline = now + TIMEOUT_NANOS;
        entry.phase = Phase.REBIND;
    }
    private void close(Entry entry, String reason) {
        Peer peer = entry.peer;
        if (entry.inFlight) {
            // PAIR_RESULT has no request id: never recycle this UUID until its response arrives
            // or the entire authenticated native transport/world is retired.
            entry.phase = Phase.TOMBSTONE; entry.owner = null; entry.peer = null;
        } else entries.remove(entry.player);
        if (peer != null && peer.open()) peer.reject(reason);
    }
    public void reset(String reason) {
        var old = new ArrayList<>(entries.values());
        entries.clear(); verified.clear();
        for (Entry entry : old) if (entry.peer != null && entry.peer.open()) entry.peer.reject(reason);
    }
    /** Existing play connections retain admission while the normal world/GCBind lifecycle catches up. */
    public void retireAuthority(String reason, long now) {
        verified.clear();
        for (Entry entry : new ArrayList<>(entries.values())) {
            if (entry.phase == Phase.PLAY || entry.phase == Phase.REBIND) {
                rebind(entry, now);
                entry.authority = new Authority(0, 0);
                entry.credential = null;
                entry.inFlight = false;
            } else {
                entries.remove(entry.player);
                if (entry.peer != null && entry.peer.open()) entry.peer.reject(reason);
            }
        }
    }
    int size() { return entries.size(); }
}
