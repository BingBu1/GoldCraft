package dev.goldcraft.bridge;

import java.util.ArrayList;
import java.util.List;

/**
 * Candidate schema-1 payload and authoritative consumer; no production message
 * ID is allocated here. Units are GoldSrc units/s for ADD/SET and dimensionless
 * for SCALE. Adapters still own authentication, physics, collision and input.
 */
public final class HostEffects {
    public static final int SCHEMA_VERSION=1, HEADER_BYTES=96, EVENT_BYTES=32, MAX_PENDING_EVENTS=64;
    public static final int PLAYER=1, OBJECT=2;
    public static final int INPUT_LOCK=1, POSITION_HOLD=2, WISH_SPEED_CAP=4, GRAVITY_FACTOR=8;
    public static final int LOCK_MOVE=1, LOCK_JUMP=2, LOCK_ATTACK=4, LOCK_USE=8;
    public static final int HOLD_X=1, HOLD_Y=2, HOLD_Z=4;
    public static final int ALL_CAPABILITIES=15, ALL_INPUT_LOCKS=15, ALL_HOLD_AXES=7;
    public static final int ADD_VELOCITY=1, SET_VELOCITY=2, SCALE_VELOCITY=3;
    public static final float MAX_WISH_SPEED_CAP=16384.0f, MAX_COMBINED_GRAVITY=64.0f;
    public static final float MAX_MOTION_COMPONENT=1_000_000.0f, MAX_VELOCITY_SCALE=16.0f;

    // long/int retain the complete unsigned wire bit patterns, including their
    // sign bit. Every timeline ordering comparison below is unsigned.
    public record Target(long world,int kind,long key,int serial,int life) {
        public Target {
            if(world==0||key==0||serial==0||life==0||
                (kind!=PLAYER&&kind!=OBJECT)||(kind==PLAYER&&Long.compareUnsigned(key,64)>0))
                throw invalid("Target identity");
        }
    }
    public record Address(Target target,long stream) {
        public Address { if(target==null||stream==0)throw invalid("Motion address"); }
    }
    public record MovementState(int capability,int inputLocks,int holdAxes,float wishSpeedCap,float gravityFactor) {
        public MovementState {
            if((capability&~ALL_CAPABILITIES)!=0||(inputLocks&~ALL_INPUT_LOCKS)!=0||(holdAxes&~ALL_HOLD_AXES)!=0||
                !finiteRange(wishSpeedCap,0,MAX_WISH_SPEED_CAP)||!finiteRange(gravityFactor,0,MAX_COMBINED_GRAVITY)||
                ((capability&INPUT_LOCK)==0&&inputLocks!=0)||((capability&POSITION_HOLD)==0&&holdAxes!=0)||
                ((capability&WISH_SPEED_CAP)==0&&wishSpeedCap!=0)||((capability&GRAVITY_FACTOR)==0&&gravityFactor!=1))
                throw invalid("Movement state");
        }
        public static MovementState unrestricted(){return new MovementState(0,0,0,0,1);}
    }
    public record Snapshot(Target target,long revision,MovementState movement) {
        public Snapshot { if(target==null||revision==0||movement==null)throw invalid("Constraint snapshot"); }
    }
    public record MotionEvent(long id,long basisPose,int operation,float x,float y,float z) {
        public MotionEvent {
            if(id==0||operation<ADD_VELOCITY||operation>SCALE_VELOCITY||
                !validComponent(operation,x)||!validComponent(operation,y)||!validComponent(operation,z))
                throw invalid("Motion event");
        }
    }
    public record Frame(Address address,Snapshot constraints,long basisPose,long acknowledgedEvent,long issuedEvent,
                        List<MotionEvent> events) {
        public Frame {
            if(address==null||constraints==null||!address.target().equals(constraints.target())||events==null||
                events.size()>MAX_PENDING_EVENTS||Long.compareUnsigned(acknowledgedEvent,issuedEvent)>0||
                issuedEvent-acknowledgedEvent!=events.size())throw invalid("Motion frame bounds");
            long previous=acknowledgedEvent,previousBasis=0;
            for(var event:events){
                if(event==null||previous== -1L||event.id()!=previous+1||
                    Long.compareUnsigned(event.basisPose(),basisPose)>0||
                    Long.compareUnsigned(event.basisPose(),previousBasis)<0)throw invalid("Motion event order");
                previous=event.id();previousBasis=event.basisPose();
            }
            events=List.copyOf(events);
        }
        public byte[] encode(){
            var target=address.target();var state=constraints.movement();
            var w=new Wire.Writer().i32(SCHEMA_VERSION).i64(target.world()).i64(address.stream())
                .i32(target.kind()).i64(target.key()).i32(target.serial()).i32(target.life())
                .i64(constraints.revision()).i64(basisPose).i64(acknowledgedEvent).i64(issuedEvent)
                .i32(state.capability()).i32(state.inputLocks()).i32(state.holdAxes())
                .f32(state.wishSpeedCap()).f32(state.gravityFactor()).i32(events.size());
            for(var event:events)w.i64(event.id()).i64(event.basisPose()).i32(event.operation())
                .f32(event.x()).f32(event.y()).f32(event.z());
            return w.toByteArray();
        }
    }

    public static Frame decode(byte[] bytes){
        if(bytes==null||bytes.length<HEADER_BYTES||bytes.length>HEADER_BYTES+MAX_PENDING_EVENTS*EVENT_BYTES)
            throw invalid("Motion frame length");
        var r=new Wire.Reader(bytes);
        if(r.i32()!=SCHEMA_VERSION)throw invalid("Motion schema version");
        long world=r.i64(),stream=r.i64();
        var target=new Target(world,r.i32(),r.i64(),r.i32(),r.i32());
        var address=new Address(target,stream);
        long revision=r.i64(),basisPose=r.i64(),acknowledgedEvent=r.i64(),issuedEvent=r.i64();
        var state=new MovementState(r.i32(),r.i32(),r.i32(),r.f32(),r.f32());
        int count=r.i32();
        if(count<0||count>MAX_PENDING_EVENTS||r.remaining()!=count*EVENT_BYTES)throw invalid("Motion event count");
        var events=new ArrayList<MotionEvent>(count);
        for(int i=0;i<count;i++)events.add(new MotionEvent(r.i64(),r.i64(),r.i32(),r.f32(),r.f32(),r.f32()));
        r.finish();return new Frame(address,new Snapshot(target,revision,state),basisPose,acknowledgedEvent,issuedEvent,events);
    }

    public enum MotionResult {
        OK, IDENTITY, INVALID, STALE_REVISION, FUTURE_REVISION, STALE_POSE, STALE_ACK,
        FUTURE_ACK, UNPUBLISHED, FULL, EXHAUSTED, EVENT_GAP, CONFLICT, NO_APPLICATION
    }
    public record MotionApplication(long ticket,Snapshot constraints,long basisPose,long throughEvent,List<MotionEvent> events) {
        public MotionApplication {
            if(ticket==0||constraints==null||events==null||events.size()>MAX_PENDING_EVENTS)
                throw invalid("Motion application");
            events=List.copyOf(events);
        }
    }

    /**
     * Exactly one movement application endpoint owns this watermark. Vanilla
     * player integration occurs on the client: server setVelocity alone is not
     * evidence of completion. The future MC server adapter must mediate a
     * receipt tied to an accepted post-effect move, without replaying the same
     * event as a second impulse. Use only on the endpoint's game thread.
     */
    public static final class MotionReplica {
        private Address address;
        private Snapshot appliedConstraints;
        private long appliedEvent,appliedBasisPose,nextTicket;
        private MotionApplication pending;
        private boolean bound;

        /** Only a real new world/life/target/authority connection may reset. */
        public MotionResult reset(Address next){
            if(next==null)return MotionResult.INVALID;
            // Remember the last address even while invalidated. Rebinding the
            // same connection must not clear its applied-event watermark.
            if(next.equals(address))return MotionResult.CONFLICT;
            address=next;appliedConstraints=null;appliedEvent=appliedBasisPose=0;pending=null;bound=true;
            // nextTicket deliberately survives every lifecycle transition.
            return MotionResult.OK;
        }
        public void invalidate(){bound=false;pending=null;}

        public MotionResult prepare(Frame frame){
            if(!bound||frame==null||!frame.address().equals(address))return MotionResult.IDENTITY;
            if(Long.compareUnsigned(frame.basisPose(),appliedBasisPose)<0)return MotionResult.STALE_POSE;
            long appliedRevision=appliedRevision();
            if(Long.compareUnsigned(frame.constraints().revision(),appliedRevision)<0)return MotionResult.STALE_REVISION;
            if(frame.constraints().revision()==appliedRevision&&
                !sameMovement(frame.constraints().movement(),appliedConstraints.movement()))return MotionResult.CONFLICT;
            if(Long.compareUnsigned(frame.acknowledgedEvent(),appliedEvent)>0)return MotionResult.EVENT_GAP;
            if(Long.compareUnsigned(frame.issuedEvent(),appliedEvent)<0)return MotionResult.STALE_ACK;
            if(pending!=null){
                if(Long.compareUnsigned(frame.basisPose(),pending.basisPose())<0)return MotionResult.STALE_POSE;
                if(Long.compareUnsigned(frame.constraints().revision(),pending.constraints().revision())<0)return MotionResult.STALE_REVISION;
                if(frame.constraints().revision()==pending.constraints().revision()&&
                    !sameMovement(frame.constraints().movement(),pending.constraints().movement()))return MotionResult.CONFLICT;
                if(Long.compareUnsigned(frame.issuedEvent(),pending.throughEvent())<0)return MotionResult.STALE_ACK;
                for(var known:pending.events()){
                    long index=known.id()-frame.acknowledgedEvent()-1;
                    if(Long.compareUnsigned(index,frame.events().size())>=0||
                        !sameEvent(frame.events().get((int)index),known))return MotionResult.CONFLICT;
                }
            }
            if(nextTicket== -1L)return MotionResult.EXHAUSTED;
            var unapplied=new ArrayList<MotionEvent>(frame.events().size());
            long expected=appliedEvent;
            for(var event:frame.events()){
                if(Long.compareUnsigned(event.id(),appliedEvent)<=0)continue;
                if(expected== -1L||event.id()!=expected+1)return MotionResult.EVENT_GAP;
                if(Long.compareUnsigned(event.basisPose(),appliedBasisPose)<0)return MotionResult.STALE_POSE;
                if(pending!=null&&Long.compareUnsigned(event.id(),pending.throughEvent())>0&&
                    Long.compareUnsigned(event.basisPose(),pending.basisPose())<0)return MotionResult.STALE_POSE;
                unapplied.add(event);expected=event.id();
            }
            var next=new MotionApplication(nextTicket+1,frame.constraints(),frame.basisPose(),frame.issuedEvent(),unapplied);
            pending=next;nextTicket=next.ticket();return MotionResult.OK;
        }
        public MotionApplication pending(){return pending;}

        /**
         * Call only after applying the complete finite calculation once on the
         * same game thread, with no reentrant prepare/reset/invalidate between
         * application and commit. Failure must not commit or emit an ACK.
         */
        public MotionResult commit(long ticket){
            if(pending==null||ticket!=pending.ticket())return MotionResult.NO_APPLICATION;
            appliedConstraints=pending.constraints();appliedEvent=pending.throughEvent();appliedBasisPose=pending.basisPose();pending=null;
            return MotionResult.OK;
        }
        public long appliedRevision(){return appliedConstraints==null?0:appliedConstraints.revision();}
        public long appliedEvent(){return appliedEvent;}
        public long appliedBasisPose(){return appliedBasisPose;}
    }

    /**
     * Pure ordered arithmetic, not physics or a total-speed policy. The caller's
     * result and input arrays remain untouched on any rejected calculation.
     * Input/result aliasing is supported; the final three writes happen only
     * after every component of every declared operation has been validated.
     */
    public static boolean applyMotionEvents(float[] initial,List<MotionEvent> events,float[] result){
        if(initial==null||initial.length!=3||events==null||result==null||result.length!=3)return false;
        double x=initial[0],y=initial[1],z=initial[2];
        if(!validVelocity(x)||!validVelocity(y)||!validVelocity(z))return false;
        for(var event:events){
            if(event==null)return false;
            switch(event.operation()){
                case ADD_VELOCITY -> {x+=event.x();y+=event.y();z+=event.z();}
                case SET_VELOCITY -> {x=event.x();y=event.y();z=event.z();}
                case SCALE_VELOCITY -> {x*=event.x();y*=event.y();z*=event.z();}
                default -> {return false;}
            }
            if(!validVelocity(x)||!validVelocity(y)||!validVelocity(z))return false;
        }
        result[0]=(float)x;result[1]=(float)y;result[2]=(float)z;return true;
    }
    private static boolean sameMovement(MovementState a,MovementState b){
        // C++ float == treats -0 and +0 alike; Java record equals does not.
        return a.capability()==b.capability()&&a.inputLocks()==b.inputLocks()&&a.holdAxes()==b.holdAxes()&&
            a.wishSpeedCap()==b.wishSpeedCap()&&a.gravityFactor()==b.gravityFactor();
    }
    private static boolean sameEvent(MotionEvent a,MotionEvent b){
        return a.id()==b.id()&&a.basisPose()==b.basisPose()&&a.operation()==b.operation()&&a.x()==b.x()&&a.y()==b.y()&&a.z()==b.z();
    }
    private static boolean validComponent(int operation,float value){
        return operation==SCALE_VELOCITY?finiteRange(value,0,MAX_VELOCITY_SCALE):validVelocity(value);
    }
    private static boolean finiteRange(float value,float minimum,float maximum){
        return Float.isFinite(value)&&value>=minimum&&value<=maximum;
    }
    private static boolean validVelocity(double value){return Double.isFinite(value)&&Math.abs(value)<=MAX_MOTION_COMPONENT;}
    private static IllegalArgumentException invalid(String message){return new IllegalArgumentException(message);}
    private HostEffects(){}
}
