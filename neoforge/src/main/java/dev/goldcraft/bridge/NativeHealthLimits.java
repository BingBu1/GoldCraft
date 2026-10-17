package dev.goldcraft.bridge;

/** Native class limits are separate from current HP and from MC modifiers. */
public final class NativeHealthLimits {
    public static final float MAXIMUM=10_000_000;
    public record Limit(int slot,int serial,int spawn,int life,float base,float effective,long revision) {
        public boolean matches(HostWorldState.Actor actor){
            return actor!=null&&slot==actor.slot()&&serial==actor.serial()&&spawn==actor.spawn()&&life==actor.life()
                &&(actor.flags()&5)==1&&actor.health()>0;
        }
    }
    private long epoch,tick;
    private Limit[] limits=new Limit[65];
    public void clear(){epoch=tick=0;limits=new Limit[65];}
    public static boolean valid(float value){return Float.isFinite(value)&&value>=1&&value<=MAXIMUM;}
    public boolean accept(HostWorldState host,byte[] payload){
        var r=new Wire.Reader(payload);long world=r.i64(),nextTick=r.i64();int count=r.i32();
        if(world==0||world!=host.epoch()||nextTick!=host.tick()
            ||epoch==world&&Long.compareUnsigned(nextTick,tick)<=0)return false;
        if(count<0||count>64||r.remaining()!=count*32)throw new IllegalArgumentException("Health limit snapshot bounds");
        Limit[] next=new Limit[65];
        for(int i=0;i<count;i++){
            var value=new Limit(r.i32(),r.i32(),r.i32(),r.i32(),r.f32(),r.f32(),r.i64());
            if(value.slot()<1||value.slot()>64||value.serial()==0||value.spawn()==0||value.life()==0
                ||next[value.slot()]!=null||!valid(value.base())||!valid(value.effective()))
                throw new IllegalArgumentException("Invalid native health limit");
            var actor=host.actors().stream().filter(a->a.slot()==value.slot()).findFirst().orElse(null);
            if(!value.matches(actor))throw new IllegalArgumentException("Health limit actor mismatch");
            next[value.slot()]=value;
        }
        r.finish();limits=next;epoch=world;tick=nextTick;return true;
    }
    /** Never combine a current actor's HP with a different snapshot's limits. */
    public Limit get(HostWorldState host,HostWorldState.Actor actor){
        if(epoch==0||epoch!=host.epoch()||tick!=host.tick()||actor==null||actor.slot()<1||actor.slot()>64)return null;
        var value=limits[actor.slot()];return value!=null&&value.matches(actor)?value:null;
    }
    public static byte[] encode(long epoch,long revision,Limit identity,float effective){
        if(epoch==0||revision==0||identity==null||identity.slot()<1||identity.slot()>64||identity.serial()==0
            ||identity.spawn()==0||identity.life()==0||!valid(identity.base())||!valid(effective))
            throw new IllegalArgumentException("Invalid effective health limit");
        return new Wire.Writer(40).i64(epoch).i64(revision).i32(identity.slot()).i32(identity.serial())
            .i32(identity.spawn()).i32(identity.life()).f32(identity.base()).f32(effective).toByteArray();
    }
}
