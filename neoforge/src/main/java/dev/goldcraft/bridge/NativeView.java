package dev.goldcraft.bridge;

/** Engine eye position and true water medium, paired with one actor snapshot. */
public final class NativeView {
    public record View(int slot,int serial,int spawn,int life,float x,float y,float z,int flags) {
        public boolean water(){return (flags&1)!=0;}
        public boolean matches(HostWorldState.Actor actor){
            return actor!=null&&slot==actor.slot()&&serial==actor.serial()&&spawn==actor.spawn()&&life==actor.life()
                &&(actor.flags()&5)==1&&actor.health()>0;
        }
    }
    private long epoch,tick,received;
    private View[] views=new View[65];
    public void clear(){epoch=tick=received=0;views=new View[65];}
    public boolean accept(HostWorldState host,byte[] payload,long serverTick){
        var r=new Wire.Reader(payload);long world=r.i64(),nextTick=r.i64();int count=r.i32();
        if(world==0||world!=host.epoch()||nextTick!=host.tick()
            ||epoch==world&&Long.compareUnsigned(nextTick,tick)<=0)return false;
        if(count<0||count>64||r.remaining()!=count*32)throw new IllegalArgumentException("Native view snapshot bounds");
        View[] next=new View[65];
        for(int i=0;i<count;i++){
            var v=new View(r.i32(),r.i32(),r.i32(),r.i32(),r.f32(),r.f32(),r.f32(),r.i32());
            if(v.slot()<1||v.slot()>64||v.serial()==0||v.spawn()==0||v.life()==0||next[v.slot()]!=null
                ||!Float.isFinite(v.x())||!Float.isFinite(v.y())||!Float.isFinite(v.z())||(v.flags()&~1)!=0)
                throw new IllegalArgumentException("Invalid native view");
            var actor=host.actors().stream().filter(a->a.slot()==v.slot()).findFirst().orElse(null);
            if(!v.matches(actor))throw new IllegalArgumentException("Native view actor mismatch");
            next[v.slot()]=v;
        }
        r.finish();views=next;epoch=world;tick=nextTick;received=serverTick;return true;
    }
    public View get(HostWorldState host,HostWorldState.Actor actor,long serverTick){
        if(epoch==0||epoch!=host.epoch()||tick!=host.tick()||serverTick<received||serverTick-received>=7
            ||actor==null||actor.slot()<1||actor.slot()>64)return null;
        var v=views[actor.slot()];return v!=null&&v.matches(actor)?v:null;
    }
}
