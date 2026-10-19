package dev.goldcraft.bridge;

/** Engine velocity/grounding belonging to exactly one authenticated actor snapshot. */
public final class NativeMovement {
    public record State(int slot,int serial,int spawn,int life,float x,float y,float z,int flags) {
        public boolean grounded(){return (flags&1)!=0;}
        public boolean walking(){return (flags&2)!=0;}
        // GoldSrc units/second -> Minecraft blocks/tick, including the up axis.
        public double velocityX(){return x/640.0;}
        public double velocityY(){return z/640.0;}
        public double velocityZ(){return -y/640.0;}
        public boolean matches(HostWorldState.Actor actor){
            return actor!=null&&slot==actor.slot()&&serial==actor.serial()&&spawn==actor.spawn()&&life==actor.life()
                &&(actor.flags()&5)==1&&actor.health()>0;
        }
    }
    private long epoch,tick,received;
    private State[] states=new State[65];
    public void clear(){epoch=tick=received=0;states=new State[65];}
    public boolean accept(HostWorldState host,byte[] payload,long serverTick){
        var r=new Wire.Reader(payload);long world=r.i64(),nextTick=r.i64();int count=r.i32();
        if(world==0||world!=host.epoch()||nextTick!=host.tick()||nextTick==0
            ||epoch==world&&Long.compareUnsigned(nextTick,tick)<=0)return false;
        if(count<0||count>64||r.remaining()!=count*32)throw new IllegalArgumentException("Native movement snapshot bounds");
        State[] next=new State[65];
        for(int i=0;i<count;i++){
            var state=new State(r.i32(),r.i32(),r.i32(),r.i32(),r.f32(),r.f32(),r.f32(),r.i32());
            if(state.slot()<1||state.slot()>64||state.serial()==0||state.spawn()==0||state.life()==0||next[state.slot()]!=null
                ||!finite(state.x())||!finite(state.y())||!finite(state.z())||(state.flags()&~3)!=0)
                throw new IllegalArgumentException("Invalid native movement");
            var actor=host.actors().stream().filter(a->a.slot()==state.slot()).findFirst().orElse(null);
            if(!state.matches(actor))throw new IllegalArgumentException("Native movement actor mismatch");
            next[state.slot()]=state;
        }
        r.finish();states=next;epoch=world;tick=nextTick;received=serverTick;return true;
    }
    private static boolean finite(float value){return Float.isFinite(value)&&Math.abs(value)<=65536;}
    public State get(HostWorldState host,HostWorldState.Actor actor,long serverTick){
        if(epoch==0||epoch!=host.epoch()||tick!=host.tick()||serverTick<received||serverTick-received>=7
            ||actor==null||actor.slot()<1||actor.slot()>64)return null;
        var state=states[actor.slot()];return state!=null&&state.matches(actor)?state:null;
    }
}
