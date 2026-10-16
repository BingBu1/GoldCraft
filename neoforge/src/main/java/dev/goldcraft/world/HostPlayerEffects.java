package dev.goldcraft.world;

import dev.goldcraft.bridge.HostEffects;
import dev.goldcraft.bridge.LocalQpcClock;
import dev.goldcraft.bridge.LocalQpcLease;
import dev.goldcraft.mixin.ServerPlayNetworkHandlerAccessor;
import net.minecraft.network.packet.c2s.play.PlayerMoveC2SPacket;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.util.math.MathHelper;
import net.minecraft.util.math.Vec3d;
import java.util.IdentityHashMap;

/** MC 1.21.1 execution candidate mapped to the pinned Yarn development API.
 * HostSession owns authenticated lifecycle admission; this class owns the
 * installed constraint and the vanilla movement/teleport boundary.
 * Only explicit position hold is implemented; all other capabilities reject.
 */
public final class HostPlayerEffects {
    private static final IdentityHashMap<ServerPlayNetworkHandler, Entry> LIVE = new IdentityHashMap<>();
    private static final IdentityHashMap<ServerPlayNetworkHandler, Binding> BOUND = new IdentityHashMap<>();
    private static final LocalQpcClock CLOCK = LocalQpcClock.system();
    private static final double EPSILON = 1.0e-7;
    private static long acceptedSequence;
    private static long acceptedCalls, rejectedCalls;

    /** Position observation from the actual accepted branch. No velocity or
     * physical-event receipt is fabricated from a vanilla position packet. */
    public record Accepted(HostEffects.Address address, long revision, long sequence,
                           Vec3d position, boolean onGround) {}
    /** A fresh view of the installed lease, not a movement-event receipt.
     * A live correction still owns its binding while vanilla waits for ACK. */
    public record Readiness(HostEffects.Address address, HostEffects.Snapshot snapshot,
                            Vec3d anchor, LocalQpcLease lease, boolean ready) {}
    /** Opaque execution generation. Only the trusted lifecycle adapter may
     * create one; a received snapshot must never create or replace a binding. */
    public static final class Binding {
        private final ServerPlayerEntity player;
        private final ServerPlayNetworkHandler networkHandler;
        private final Object level;
        private final HostEffects.Address address;
        private final long initialDeadline, frequency;
        private Binding(ServerPlayerEntity player, HostEffects.Address address, LocalQpcClock.Sample now) {
            this.player=player;networkHandler=player.networkHandler;level=player.getWorld();this.address=address;
            frequency=now.frequency();initialDeadline=now.milliseconds()+LocalQpcLease.MAX_DURATION_MILLIS;
        }
    }
    private static final class Entry {
        final Binding binding;
        final ServerPlayerEntity player;
        final Object level;
        final HostEffects.Address address;
        HostEffects.Snapshot snapshot;
        Vec3d anchor;
        LocalQpcLease lease;
        Accepted accepted;
        boolean correction;
        Entry(Binding binding, HostEffects.Snapshot snapshot, Vec3d anchor, LocalQpcLease lease) {
            this.binding=binding;player=binding.player;level=binding.level;address=binding.address;this.snapshot=snapshot;
            this.anchor=anchor;this.lease=lease;
        }
    }
    private HostPlayerEffects() {}

    // Package-private: only the trusted HostSession admission path creates bindings.
    static Binding bind(ServerPlayerEntity player, HostEffects.Address address) {
        if (!player.getServer().isOnThread()) throw new IllegalStateException("Server thread required");
        if (address==null || address.target().kind()!=HostEffects.PLAYER || player.networkHandler==null ||
            player.networkHandler.player!=player || !player.networkHandler.getConnection().isOpen() ||
            teleportPending(player) || player.isRemoved() || !player.isAlive()) return null;
        if (!BOUND.containsKey(player.networkHandler) && BOUND.size()>=64) return null;
        var now=sample();
        if(now==null || now.milliseconds()>Long.MAX_VALUE-LocalQpcLease.MAX_DURATION_MILLIS) return null;
        detach(player.networkHandler);
        Binding binding=new Binding(player,address,now);BOUND.put(player.networkHandler,binding);return binding;
    }
    private static boolean sameLife(Binding b) {
        return b.player.networkHandler==b.networkHandler && b.networkHandler.player==b.player &&
               b.networkHandler.getConnection().isOpen() &&
               b.player.getWorld()==b.level && !b.player.isRemoved() && b.player.isAlive();
    }
    private static boolean teleportPending(ServerPlayerEntity player) {
        return ((ServerPlayNetworkHandlerAccessor)player.networkHandler).goldcraft$requestedTeleportPos()!=null;
    }
    private static void retire(Binding b) {
        // A queued old token must never remove its replacement.
        if (BOUND.get(b.networkHandler)==b) {BOUND.remove(b.networkHandler);LIVE.remove(b.networkHandler);}
    }
    /** Anchor is supplied by the authenticated native authority in MC feet
     * coordinates. Installing a snapshot never derives its anchor locally.
     * Lease is an absolute native QPC deadline; socket/queue time is never added.
     * This only accepts a pre-established trusted binding. Wire ingress remains
     * separate and must verify the local-QPC capability and stream identity. */
    public static boolean install(Binding binding, HostEffects.Snapshot snapshot,
                                  Vec3d authoritativeAnchor, LocalQpcLease lease) {
        if (binding==null) return false;
        ServerPlayerEntity player=binding.player;
        if (!player.getServer().isOnThread()) throw new IllegalStateException("Server thread required");
        if (BOUND.get(binding.networkHandler)!=binding) return false;
        if (!sameLife(binding)) {retire(binding);return false;}
        var now=sample();
        if(now==null) return false;
        if(now.frequency()!=binding.frequency ||
           !LIVE.containsKey(binding.networkHandler) && now.milliseconds()>=binding.initialDeadline) {
            retire(binding);return false;
        }
        if (snapshot==null || !validAnchor(authoritativeAnchor) || !binding.address.target().equals(snapshot.target()) ||
            lease==null || !lease.liveAt(now) ||
            (snapshot.movement().capability() & ~HostEffects.POSITION_HOLD)!=0) return false;
        Entry old=current(player);
        // current() retires expired state, including the execution token. A
        // late snapshot cannot restart it even with a higher revision.
        if (BOUND.get(binding.networkHandler)!=binding) return false;
        // Re-read at the install boundary; never restart a deadline on receipt.
        now=sample();
        if(now==null || !lease.liveAt(now)) return false;
        if(now.frequency()!=binding.frequency || old==null && now.milliseconds()>=binding.initialDeadline) {
            retire(binding);return false;
        }
        if(old!=null && !old.lease.liveAt(now)) {retire(binding);return false;}
        if (old!=null) {
            if (Long.compareUnsigned(lease.publication(),old.lease.publication())<=0) return false;
            if (Long.compareUnsigned(snapshot.revision(),old.snapshot.revision())<0) return false;
            // Equal semantic revisions can renew time, but cannot move held
            // axes. Ignore unheld components, which are not part of this hold.
            if (snapshot.revision()==old.snapshot.revision() &&
                (!snapshot.equals(old.snapshot) || !sameHeldAnchor(old,authoritativeAnchor))) return false;
            if (snapshot.revision()!=old.snapshot.revision()) old.accepted=null;
            old.snapshot=snapshot;old.anchor=authoritativeAnchor;old.lease=lease;
            reconcile(old);
            return current(player)==old;
        }
        Entry installed=new Entry(binding,snapshot,authoritativeAnchor,lease);
        LIVE.put(binding.networkHandler,installed);
        reconcile(installed);
        return current(player)==installed;
    }
    private static boolean validAnchor(Vec3d a) {
        return a!=null&&Double.isFinite(a.x)&&Double.isFinite(a.y)&&Double.isFinite(a.z)&&
            Math.abs(a.x)<=3.0E7&&Math.abs(a.y)<=2.0E7&&Math.abs(a.z)<=3.0E7;
    }
    private static boolean sameHeldAnchor(Entry e,Vec3d a) {
        int axes=e.snapshot.movement().holdAxes();
        return ((axes&1)==0||a.x==e.anchor.x)&&((axes&2)==0||a.z==e.anchor.z)&&
               ((axes&4)==0||a.y==e.anchor.y);
    }
    private static void reconcile(Entry e) {
        if(outside(e,e.player.getX(),e.player.getY(),e.player.getZ())) correct(e);
    }
    private static boolean correct(Entry e) {
        ServerPlayerEntity p=e.player;
        if(current(p)!=e) return false;
        int axes=e.snapshot.movement().holdAxes();
        double x=(axes&1)!=0?e.anchor.x:p.getX();
        double y=(axes&4)!=0?e.anchor.y:p.getY();
        double z=(axes&2)!=0?e.anchor.z:p.getZ();
        // Preserve vanilla's pending requestTeleport/ACK barrier and the exact binding.
        // Neither setPos inside requestTeleport nor its ACK creates an observation.
        e.accepted=null;
        boolean previous=e.correction;e.correction=true;
        try {p.networkHandler.requestTeleport(x,y,z,p.getYaw(),p.getPitch());}
        finally {e.correction=previous;}
        return true;
    }
    private static LocalQpcClock.Sample sample() {
        var now=CLOCK.read();
        if(now==null) clear(); // Keep CLOCK's permanent fault latch.
        return now;
    }
    private static Entry current(ServerPlayerEntity player) {
        Entry e=LIVE.get(player.networkHandler);
        if (e==null) return null;
        // An old respawn instance may still reference the reused handler. It
        // has no authority to retire that handler's replacement player.
        if (e.player!=player) return null;
        var now=sample();
        if (now==null || !sameLife(e.binding) || !e.lease.liveAt(now)) {
            retire(e.binding);return null;
        }
        return e;
    }
    private static void detach(ServerPlayNetworkHandler networkHandler) { LIVE.remove(networkHandler);BOUND.remove(networkHandler); }
    public static void revoke(Binding binding) { if(binding!=null)retire(binding); }
    public static void clear() { LIVE.clear();BOUND.clear(); }
    /** Called by HostSession immediately before publishing a proved pose.
     * No caller may use a previous successful result as a renewed lease. */
    public static Readiness readiness(Binding binding,long provedFrequency) {
        if(binding==null)return null;
        if(!binding.player.getServer().isOnThread())throw new IllegalStateException("Server thread required");
        if(BOUND.get(binding.networkHandler)!=binding)return null;
        if(provedFrequency<=0||binding.frequency!=provedFrequency){retire(binding);return null;}
        Entry e=current(binding.player);
        if(e==null||e.binding!=binding)return null;
        if(!teleportPending(e.player)&&outside(e,e.player.getX(),e.player.getY(),e.player.getZ()))correct(e);
        // requestTeleport can re-enter lifecycle hooks; check the exact token again.
        if(current(binding.player)!=e||BOUND.get(binding.networkHandler)!=binding)return null;
        return new Readiness(e.address,e.snapshot,e.anchor,e.lease,
            !teleportPending(e.player)&&!outside(e,e.player.getX(),e.player.getY(),e.player.getZ()));
    }
    /** The production server tick must call this even if no movement arrives. */
    public static void pump(MinecraftServer server) {
        if (!server.isOnThread()) throw new IllegalStateException("Server thread required");
        var entries=BOUND.entrySet().iterator();
        while(entries.hasNext()) {
            var row=entries.next();var networkHandler=row.getKey();var binding=row.getValue();var e=LIVE.get(networkHandler);
            if(binding.player.getServer()!=server) continue;
            var now=sample();
            if(now==null) return; // sample cleared all maps; do not reuse iterator.
            if(!sameLife(binding) || now.frequency()!=binding.frequency ||
               (e==null?now.milliseconds()>=binding.initialDeadline:!e.lease.liveAt(now))) {
                entries.remove();LIVE.remove(networkHandler);
            }
        }
    }
    public static long acceptedCalls() { return acceptedCalls; }
    public static long rejectedCalls() { return rejectedCalls; }

    private static boolean outside(Entry e, double x, double y, double z) {
        int axes=e.snapshot.movement().holdAxes();
        // GoldSrc axes are X=MC X, Y=-MC Z, Z=MC Y.
        return ((axes&1)!=0&&Math.abs(x-e.anchor.x)>EPSILON) ||
               ((axes&2)!=0&&Math.abs(z-e.anchor.z)>EPSILON) ||
               ((axes&4)!=0&&Math.abs(y-e.anchor.y)>EPSILON);
    }
    /** Called after vanilla invalid-number/world/pending-requestTeleport checks.
     * True cancels before the physical move and absolute target snap. */
    public static boolean rejectMove(ServerPlayerEntity player, PlayerMoveC2SPacket packet) {
        if (!player.getServer().isOnThread()) throw new IllegalStateException("Server thread required");
        Entry e=current(player);
        if (e==null || !packet.changesPosition() || player.hasVehicle() || player.isSleeping()) return false;
        // Exact 1.21.1 bounds: vanilla clamps infinities rather than rejecting
        // them. Compare the same target vanilla would physically accept.
        double x=MathHelper.clamp(packet.getX(player.getX()),-3.0E7,3.0E7);
        double y=MathHelper.clamp(packet.getY(player.getY()),-2.0E7,2.0E7);
        double z=MathHelper.clamp(packet.getZ(player.getZ()),-3.0E7,3.0E7);
        if (!outside(e,x,y,z)) return false;
        // Use vanilla correction/ack instead of changing client ownership.
        if(!correct(e)) return false;
        rejectedCalls++;
        return true;
    }
    /** Called only after onPlayerMove's final updatedZ store. */
    public static void accepted(ServerPlayerEntity player, PlayerMoveC2SPacket packet) {
        if (!player.getServer().isOnThread()) throw new IllegalStateException("Server thread required");
        acceptedCalls++;
        Entry e=current(player);
        if (e==null || !packet.changesPosition() || player.hasVehicle() || player.isSleeping()) return;
        if (outside(e,player.getX(),player.getY(),player.getZ())) {e.accepted=null;return;}
        if (acceptedSequence==Long.MAX_VALUE) {clear();throw new IllegalStateException("Accepted move sequence exhausted");}
        e.accepted=new Accepted(e.address,e.snapshot.revision(),++acceptedSequence,player.getPos(),player.isOnGround());
    }
    public static Accepted observation(ServerPlayerEntity player) {
        Entry e=current(player);
        if(e!=null && e.accepted!=null && player.getPos().squaredDistanceTo(e.accepted.position())>EPSILON*EPSILON)
            e.accepted=null; // A separate requestTeleport/direct movement is not this accepted packet.
        return e==null?null:e.accepted;
    }
    public static void vanillaCorrection(ServerPlayerEntity player, Runnable correction) {
        Entry e=current(player);
        if(e==null) {HostSession.effectCorrection(player,correction);return;}
        boolean previous=e.correction;e.correction=true;
        try {correction.run();} finally {e.correction=previous;}
    }
    public static void teleporting(ServerPlayerEntity player,double x,double y,double z) {
        Binding binding=BOUND.get(player.networkHandler);
        if(binding==null || binding.player!=player || binding.networkHandler!=player.networkHandler) {
            HostSession.effectTeleporting(player);return;
        }
        Entry e=current(player);
        if (e!=null) {
            e.accepted=null;
            if(e.correction)return;
            var grant=HostSession.relocationGrant(player,binding);
            // requestTeleport arguments are absolute in pinned 1.21.1, even
            // when PositionFlag encodes a relative packet. Keep the existing
            // token only for an admitted, live, same-world server relocation.
            // readiness() still blocks pose publication until vanilla's ACK.
            if(grant!=null&&e.address.equals(grant.address())&&e.snapshot.equals(grant.snapshot())&&
                e.lease.equals(grant.lease())&&!outside(e,x,y,z)&&HostMovement.canTeleport(player,new Vec3d(x,y,z)))return;
            HostSession.effectTeleporting(player);retire(binding);
        } else {
            HostSession.effectTeleporting(player);
            retire(binding);
        }
    }
}
