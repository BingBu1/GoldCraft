package dev.goldcraft.bridge;

/** Fixed-width host mining protocol. HLDS alone supplies mode and revision. */
public final class MapMining {
    private MapMining() {}
    public static final int DISABLED=0, DAMAGEABLE_ENTITIES=1, ALL_GEOMETRY=2;
    public static final int ENTITY_DAMAGE=1, GEOMETRY_CARVING=2, APPLIED=0;
    public static final int STONE=0, WOOD=1, METAL=2, GLASS=3, SOIL=4, TILE=5, FLESH=6, UNBREAKABLE=7, WATER=8, SNOW=9;
    public record Policy(long epoch,long revision,int mode,int capabilities) {
        public static Policy none(){return new Policy(0,0,DISABLED,0);}
        public boolean enabled(){return epoch!=0&&revision!=0&&mode!=DISABLED;}
        public boolean canRequest(int model){
            return enabled()&&(model>0?(capabilities&ENTITY_DAMAGE)!=0:mode==ALL_GEOMETRY&&(capabilities&GEOMETRY_CARVING)!=0);
        }
        public byte[] encode(){return new Wire.Writer().i64(epoch).i64(revision).i32(mode).i32(capabilities).toByteArray();}
    }
    public static Policy policy(byte[] bytes){
        var r=new Wire.Reader(bytes);var p=new Policy(r.i64(),r.i64(),r.i32(),r.i32());r.finish();
        if(p.epoch()==0||p.revision()==0||p.mode()<0||p.mode()>2||(p.capabilities()&~3)!=0)throw new IllegalArgumentException("Mining policy bounds");
        return p;
    }
    public record Result(long epoch,long event,long revision,int slot,int serial,int life,int target,int targetSerial,int status,float before,float after) {}
    public static Result result(byte[] bytes){
        var r=new Wire.Reader(bytes);
        var value=new Result(r.i64(),r.i64(),r.i64(),r.i32(),r.i32(),r.i32(),r.i32(),r.i32(),r.i32(),r.f32(),r.f32());r.finish();
        if(value.epoch()==0||value.event()==0||value.revision()==0||value.slot()<1||value.slot()>64||value.serial()==0||value.life()==0
            ||value.target()<0||value.target()>32767||value.status()<0||value.status()>9)throw new IllegalArgumentException("Mining result bounds");
        return value;
    }
    public record Cell(int x,int y,int z) {}
    public record Surface(Result result,int kind,int material,float x,float y,float z,float nx,float ny,float nz,
                          long editRevision,Cell cell) {}
    public static Surface surface(byte[] bytes){
        var r=new Wire.Reader(bytes);
        var s=new Surface(result(r.bytes(56)),r.i32(),r.i32(),r.f32(),r.f32(),r.f32(),r.f32(),r.f32(),r.f32(),
            r.i64(),new Cell(r.i32(),r.i32(),r.i32()));r.finish();
        float length=s.nx()*s.nx()+s.ny()*s.ny()+s.nz()*s.nz();
        if(s.kind()<0||s.kind()>2||s.material()<0||s.material()>9
            ||s.cell().x() < -1024||s.cell().x()>=1024||s.cell().y() < -1024||s.cell().y()>=1024||s.cell().z() < -1024||s.cell().z()>=1024
            ||s.editRevision()!=0&&(s.kind()!=2||s.result().status()!=APPLIED)
            ||Math.abs(s.x())>16384||Math.abs(s.y())>16384||Math.abs(s.z())>16384
            ||s.result().status()==APPLIED&&(s.kind()==0||length<0.98f||length>1.02f))throw new IllegalArgumentException("Mining surface bounds");
        return s;
    }
    public static byte[] request(Policy policy,long event,HostWorldState.Actor actor,int target,int serial,int model,
                                 float x,float y,float z,float damage,float reach,long sample){
        if(!policy.enabled()||event==0||target<0||target>32767||model<0||model>4095||(target==0&&(serial!=0||model!=0))
            ||(target!=0&&model==0)||damage<=0||damage>5000||reach<=0||reach>192||Math.abs(x)>16384||Math.abs(y)>16384||Math.abs(z)>16384)
            throw new IllegalArgumentException("Mining request bounds");
        return new Wire.Writer().i64(policy.epoch()).i64(policy.revision()).i64(event)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.life()).bytes(Wire.uuid(actor.minecraftPlayer()))
            .i32(target).i32(serial).i32(model).f32(x).f32(y).f32(z).f32(damage).f32(reach).i64(sample).toByteArray();
    }
}
