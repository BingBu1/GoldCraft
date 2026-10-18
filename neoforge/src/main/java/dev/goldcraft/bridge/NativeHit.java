package dev.goldcraft.bridge;

/** Native post-settlement notification; no health request or client authority. */
public record NativeHit(long epoch,long event,long tick,int slot,int serial,int spawn,int life,
                        int attacker,int attackerSerial,int attackerSpawn,int kind,float amount,float x,float y,float z) {
    public NativeHit {
        if(epoch==0||event==0||tick==0||slot<1||slot>64||serial==0||spawn==0||life==0
            ||attacker<0||attacker>64||(attacker==0?(attackerSerial!=0||attackerSpawn!=0):(attackerSerial==0||attackerSpawn==0))
            ||kind<0||kind>7||!Float.isFinite(amount)||amount<0||amount>10000000
            ||!coordinate(x)||!coordinate(y)||!coordinate(z))throw new IllegalArgumentException("Invalid native hurt");
    }
    private static boolean coordinate(float v){return Float.isFinite(v)&&Math.abs(v)<=1048576;}
    public static NativeHit read(byte[] bytes){
        var r=new Wire.Reader(bytes);
        var value=new NativeHit(r.i64(),r.i64(),r.i64(),r.i32(),r.i32(),r.i32(),r.i32(),r.i32(),r.i32(),r.i32(),r.i32(),r.f32(),r.f32(),r.f32(),r.f32());
        r.finish();return value;
    }
}
