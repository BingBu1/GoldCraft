package dev.goldcraft.bridge;

import java.util.UUID;

/** Strict codec only. An incoming grant still needs the authenticated lifecycle gate. */
public final class HostEffectGrant {
    public static final int SCHEMA=1, OPEN=1, UPDATE=2, GRANT_BYTES=164, POSE_BYTES=128, REOPEN_BYTES=64;
    private static final UUID ZERO_UUID=new UUID(0,0);
    private HostEffectGrant() {}
    public record Point(float x,float y,float z) {
        public Point {
            if(!finite(x)||!finite(y)||!finite(z))throw new IllegalArgumentException("Invalid native grant position");
        }
        private static boolean finite(float v){return Float.isFinite(v)&&Math.abs(v)<=16384;}
    }
    public record Grant(int operation,long serviceGeneration,long nativeConnection,
                        HostEffects.Snapshot constraints,UUID minecraftUuid,long stream,
                        LocalQpcLease lease,boolean bootstrap,Point nativeFeet,Point nativeAnchorFeet) {
        public Grant {
            if((operation!=OPEN&&operation!=UPDATE)||serviceGeneration==0||nativeConnection==0||
               constraints==null||constraints.target().kind()!=HostEffects.PLAYER||minecraftUuid==null||
               minecraftUuid.equals(ZERO_UUID)||stream==0||lease==null||nativeFeet==null||nativeAnchorFeet==null)
                throw new IllegalArgumentException("Invalid effect grant");
        }
        public HostEffects.Address address(){return new HostEffects.Address(constraints.target(),stream);}
        public HostEffects.Snapshot snapshot(){return constraints;}
        public long publication(){return lease.publication();}
        public long frequency(){return lease.frequency();}
    }
    public static Grant decode(byte[] bytes) {
        if(bytes.length!=GRANT_BYTES)throw new IllegalArgumentException("Invalid effect grant size");
        Wire.Reader r=new Wire.Reader(bytes);
        if(r.i32()!=SCHEMA)throw new IllegalArgumentException("Invalid effect grant schema");
        int operation=r.i32();long service=r.i64(),connection=r.i64();
        var target=new HostEffects.Target(r.i64(),r.i32(),r.i64(),r.i32(),r.i32());
        UUID uuid=Wire.uuid(r.bytes(16));long stream=r.i64(),publication=r.i64(),revision=r.i64();
        var lease=new LocalQpcLease(r.i64(),r.i64(),r.i64(),publication);
        int bootstrap=r.i32();
        if(bootstrap<0||bootstrap>1)throw new IllegalArgumentException("Invalid effect bootstrap flag");
        Point feet=new Point(r.f32(),r.f32(),r.f32()),anchor=new Point(r.f32(),r.f32(),r.f32());
        var movement=new HostEffects.MovementState(r.i32(),r.i32(),r.i32(),r.f32(),r.f32());r.finish();
        return new Grant(operation,service,connection,new HostEffects.Snapshot(target,revision,movement),uuid,
            stream,lease,bootstrap==1,feet,anchor);
    }
    public static byte[] encode(Grant g) {
        var t=g.constraints().target();var m=g.constraints().movement();var lease=g.lease();
        var w=new Wire.Writer().i32(SCHEMA).i32(g.operation()).i64(g.serviceGeneration()).i64(g.nativeConnection())
            .i64(t.world()).i32(t.kind()).i64(t.key()).i32(t.serial()).i32(t.life()).bytes(Wire.uuid(g.minecraftUuid()))
            .i64(g.stream()).i64(lease.publication()).i64(g.constraints().revision()).i64(lease.frequency())
            .i64(lease.issuedAtMillis()).i64(lease.deadlineMillis()).i32(g.bootstrap()?1:0);
        for(Point p:new Point[]{g.nativeFeet(),g.nativeAnchorFeet()})w.f32(p.x()).f32(p.y()).f32(p.z());
        return w.i32(m.capability()).i32(m.inputLocks()).i32(m.holdAxes()).f32(m.wishSpeedCap()).f32(m.gravityFactor()).toByteArray();
    }
    /** Append the existing 92-byte pose body; null is a release-only envelope. */
    public static Wire.Writer posePrefix(Grant g) {
        return new Wire.Writer().i32(SCHEMA).i64(g==null?0:g.serviceGeneration()).i64(g==null?0:g.nativeConnection())
            .i64(g==null?0:g.stream()).i64(g==null?0:g.publication());
    }
    public static byte[] reopen(Grant g) {
        var t=g.constraints().target();
        return new Wire.Writer().i32(SCHEMA).i64(t.world()).i32((int)t.key()).i32(t.serial()).i32(t.life())
            .bytes(Wire.uuid(g.minecraftUuid())).i64(g.serviceGeneration()).i64(g.nativeConnection()).i64(g.stream()).toByteArray();
    }
}
