package dev.goldcraft.bridge;

/** Server-only equipment defense. Requests are 64 bytes; responses are 44. */
public final class NativeArmorProtocol {
    public static final int CAPABILITY=64, REQUEST=80, RESULT=81;
    private NativeArmorProtocol(){}
    public record Request(long world,long event,int slot,int serial,int spawn,int life,
                          int attacker,int attackerSerial,int attackerSpawn,float damage,int kind,
                          float x,float y,float z) {
        public Request {
            if(world==0||event==0||slot<1||slot>64||serial==0||spawn==0||life==0||attacker<0||attacker>64
                ||(attacker==0?(attackerSerial!=0||attackerSpawn!=0):(attackerSerial==0||attackerSpawn==0))
                ||!Float.isFinite(damage)||damage<=0||damage>500000||kind<1||kind>7
                ||!coordinate(x)||!coordinate(y)||!coordinate(z))
                throw new IllegalArgumentException("Invalid native armor request");
        }
        public byte[] result(int status,float afterArmor,float enchantmentMultiplier){
            if(status<0||status>1||!Float.isFinite(afterArmor)||afterArmor<0||afterArmor>damage
                ||!Float.isFinite(enchantmentMultiplier)||enchantmentMultiplier<0||enchantmentMultiplier>1)
                throw new IllegalArgumentException("Invalid native armor result");
            return new Wire.Writer().i64(world).i64(event).i32(slot).i32(serial).i32(spawn).i32(life)
                .i32(status).f32(afterArmor).f32(enchantmentMultiplier).toByteArray();
        }
    }
    private static boolean coordinate(float value){return Float.isFinite(value)&&Math.abs(value)<=1048576;}
    public static Request read(byte[] bytes){
        var r=new Wire.Reader(bytes);
        var request=new Request(r.i64(),r.i64(),r.i32(),r.i32(),r.i32(),r.i32(),
            r.i32(),r.i32(),r.i32(),r.f32(),r.i32(),r.f32(),r.f32(),r.f32());
        r.finish();return request;
    }
}
