package dev.goldcraft.bridge;

import java.util.*;
import dev.goldcraft.world.BspMap;

/** Validated snapshots from the CS game server, kept separate from local client presentation. */
public final class HostWorldState {
    public record Vector(float x,float y,float z) {}
    public record Actor(int slot,int serial,int userid,int team,int flags,int life,float health,float armor,
                        Vector origin,Vector velocity,float pitch,float yaw,Vector mins,Vector maxs,UUID minecraftPlayer,
                        int spawn,long vitalsAck) {
        public Vector minecraftFeet() {return new Vector(origin.x/Wire.UNITS_PER_BLOCK,(origin.z+mins.z)/Wire.UNITS_PER_BLOCK+Wire.Y_OFFSET,-origin.y/Wire.UNITS_PER_BLOCK);}
        public boolean minecraftForm(){return (flags&64)!=0;}
    }
    public record Brush(int slot,int serial,int model,int moveType,int flags,Vector origin,Vector angles,Vector min,Vector max,Vector velocity) {
        public boolean solid(){return (flags&1)!=0;}
        public boolean ladder(){return (flags&2)!=0;}
    }
    private long epoch,tick;
    private String map="",dimension="";
    private List<Actor> actors=List.of();
    private List<Brush> brushes=List.of();
    private BspMap geometry;
    private byte[] bspBytes;
    private long brushTick;
    private boolean freeze;
    private MapMining.Policy mining=MapMining.Policy.none();
    public long epoch(){return epoch;}
    public long tick(){return tick;}
    public String map(){return map;}
    public String dimension(){return dimension;}
    public List<Actor> actors(){return actors;}
    public Actor actor(UUID id){return actors.stream().filter(a->(a.flags&16)!=0&&a.minecraftPlayer.equals(id)).findFirst().orElse(null);}
    public List<Brush> brushes(){return brushes;}
    public BspMap geometry(){return geometry;}
    public byte[] bspBytes(){return bspBytes;}
    public boolean freeze(){return freeze;}
    public MapMining.Policy mining(){return mining;}
    public void mining(byte[] bytes){
        var policy=MapMining.policy(bytes);
        if(epoch==0||policy.epoch()!=epoch||Long.compareUnsigned(policy.revision(),mining.revision())<=0)return;
        mining=policy;
    }
    public void clear(){epoch=tick=brushTick=0;map=dimension="";actors=List.of();brushes=List.of();geometry=null;bspBytes=null;freeze=false;mining=MapMining.Policy.none();}
    private static Vector vector(Wire.Reader r){return new Vector(r.f32(),r.f32(),r.f32());}
    public void world(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload); long newEpoch=r.i64(); String newMap=r.string(128);r.finish();
        if(newEpoch==0||!newMap.matches("[A-Za-z0-9_./-]{1,128}"))throw new IllegalArgumentException("Invalid host map identity");
        if(newEpoch!=epoch){clear();epoch=newEpoch;map=newMap;}
        else if(!newMap.equals(map))throw new IllegalArgumentException("Map name changed without new epoch");
    }
    public void actors(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long world=r.i64(),newTick=r.i64();r.f32();int state=r.i32(),count=r.i32();
        if(world!=epoch||epoch==0||Long.compareUnsigned(newTick,tick)<=0)return;
        if(count<0||count>64||r.remaining()!=count*116||state<0||state>1)throw new IllegalArgumentException("Actor snapshot bounds");
        Set<Integer> slots=new HashSet<>(); List<Actor> next=new ArrayList<>(count);
        for(int i=0;i<count;i++) {
            int slot=r.i32(),serial=r.i32(),userid=r.i32(),team=r.i32(),flags=r.i32(),life=r.i32();
            if(slot<1||slot>64||!slots.add(slot)||team<0||team>3||(flags&~127)!=0)throw new IllegalArgumentException("Invalid host player record");
            float health=r.f32(),armor=r.f32();Vector origin=vector(r),velocity=vector(r);float pitch=r.f32(),yaw=r.f32();Vector mins=vector(r),maxs=vector(r);UUID uuid=Wire.uuid(r.bytes(16));
            if(mins.x>maxs.x||mins.y>maxs.y||mins.z>maxs.z)throw new IllegalArgumentException("Inverted actor bounds");
            int spawn=r.i32();long vitalsAck=r.i64();
            next.add(new Actor(slot,serial,userid,team,flags,life,health,armor,origin,velocity,pitch,yaw,mins,maxs,uuid,spawn,vitalsAck));
        }
        r.finish();actors=List.copyOf(next);tick=newTick;freeze=state==1;
    }
    public void bsp(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long world=r.i64(),crc=Integer.toUnsignedLong(r.i32());int length=r.i32();
        if(world!=epoch||epoch==0)return;
        if(length<124||length>BspMap.MAX_BYTES||length!=r.remaining())throw new IllegalArgumentException("BSP transfer length");
        byte[] bytes=r.bytes(length);r.finish();
        if(BspMap.checksum(bytes)!=crc)throw new IllegalArgumentException("BSP transfer checksum");
        BspMap parsed=BspMap.read(bytes);geometry=parsed;bspBytes=bytes;brushes=List.of();brushTick=0;
        dimension="goldcraft:"+map.toLowerCase(Locale.ROOT)+"_"+String.format(Locale.ROOT,"%08x",geometry.crc());
    }
    public void brushes(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long world=r.i64(),nextTick=r.i64();int count=r.i32();
        if(world!=epoch||epoch==0||Long.compareUnsigned(nextTick,brushTick)<=0)return;
        if(count<0||count>4096||r.remaining()!=count*80)throw new IllegalArgumentException("Brush snapshot bounds");
        List<Brush> result=new ArrayList<>(count);Set<Integer> slots=new HashSet<>();
        for(int i=0;i<count;i++) {
            int slot=r.i32(),serial=r.i32(),model=r.i32(),moveType=r.i32(),flags=r.i32();Vector origin=vector(r),angles=vector(r),min=vector(r),max=vector(r),velocity=vector(r);
            if(slot<1||slot>32767||!slots.add(slot)||model<1||(geometry!=null&&model>=geometry.modelCount())||flags==0||(flags&~3)!=0||min.x>max.x||min.y>max.y||min.z>max.z)throw new IllegalArgumentException("Brush identity/bounds");
            result.add(new Brush(slot,serial,model,moveType,flags,origin,angles,min,max,velocity));
        }
        r.finish();brushes=List.copyOf(result);brushTick=nextTick;
    }
}
