package dev.goldcraft.bridge;

import java.util.ArrayList;
import java.util.List;

/** Native-map authority records. Coordinates stay local to the identified BSP model. */
public final class MapEdits {
    public static final int MAX_CUTS=65536, ADD=1, REMOVE_TARGET=2, CLEAR=3;
    public record Target(int slot,int serial,int model) {
        public Target {
            if(slot<0||slot>32767||model<0||model>4095||(slot==0&&(serial!=0||model!=0))||(slot!=0&&model==0))
                throw new IllegalArgumentException("Map edit target bounds");
        }
    }
    public record Box(float minX,float minY,float minZ,float maxX,float maxY,float maxZ) {
        public Box { axis(minX,maxX);axis(minY,maxY);axis(minZ,maxZ); }
        private static void axis(float min,float max){
            if(!Float.isFinite(min)||!Float.isFinite(max)||min>=max||min< -32768||max>32768)
                throw new IllegalArgumentException("Map edit volume bounds");
        }
    }
    public record Cut(long id,Target target,Box box) {
        public Cut { if(id==0||target==null||box==null)throw new IllegalArgumentException("Map edit identity"); }
    }
    public record Snapshot(long epoch,long revision,List<Cut> cuts) {
        public Snapshot {
            if(epoch==0||revision==0||cuts.size()>MAX_CUTS)throw new IllegalArgumentException("Map edit snapshot bounds");
            cuts=List.copyOf(cuts);long previous=0;
            for(var cut:cuts){
                if(Long.compareUnsigned(cut.id(),previous)<=0||Long.compareUnsigned(cut.id(),revision)>0)
                    throw new IllegalArgumentException("Map edit identity order");
                previous=cut.id();
            }
        }
        public byte[] encode(){
            var w=new Wire.Writer().i64(epoch).i64(revision).i32(cuts.size());
            for(var c:cuts){w.i64(c.id());write(w,c.target());write(w,c.box());}return w.toByteArray();
        }
    }
    public record Delta(long epoch,long base,long revision,int operation,Target target,Box box) {
        public Delta {
            if(epoch==0||base==0||base== -1L||revision!=base+1||operation<ADD||operation>CLEAR||
                (operation!=CLEAR&&target==null)||(operation==ADD&&box==null))
                throw new IllegalArgumentException("Map edit delta bounds");
        }
        public byte[] encode(){
            var w=new Wire.Writer().i64(epoch).i64(base).i64(revision).i32(operation);
            if(operation!=CLEAR)write(w,target);if(operation==ADD)write(w,box);return w.toByteArray();
        }
    }
    private static void write(Wire.Writer w,Target t){w.i32(t.slot()).i32(t.serial()).i32(t.model());}
    private static void write(Wire.Writer w,Box b){w.f32(b.minX()).f32(b.minY()).f32(b.minZ()).f32(b.maxX()).f32(b.maxY()).f32(b.maxZ());}
    private static Target target(Wire.Reader r){return new Target(r.i32(),r.i32(),r.i32());}
    private static Box box(Wire.Reader r){return new Box(r.f32(),r.f32(),r.f32(),r.f32(),r.f32(),r.f32());}
    public static Snapshot snapshot(byte[] bytes){
        var r=new Wire.Reader(bytes);long epoch=r.i64(),revision=r.i64();int count=r.i32();
        if(count<0||count>MAX_CUTS||r.remaining()!=count*44)throw new IllegalArgumentException("Map edit snapshot length");
        var cuts=new ArrayList<Cut>(count);for(int i=0;i<count;i++)cuts.add(new Cut(r.i64(),target(r),box(r)));
        r.finish();return new Snapshot(epoch,revision,cuts);
    }
    public static Delta delta(byte[] bytes){
        var r=new Wire.Reader(bytes);long epoch=r.i64(),base=r.i64(),revision=r.i64();int op=r.i32();
        var target=op==CLEAR?null:target(r);var box=op==ADD?box(r):null;
        r.finish();return new Delta(epoch,base,revision,op,target,box);
    }
    public enum Applied { IGNORED, CHANGED, NEED_SNAPSHOT }
    public static final class Replica {
        private long epoch,revision;
        private List<Cut> cuts=List.of();
        private boolean ready;
        public void reset(long epoch){this.epoch=epoch;revision=0;cuts=List.of();ready=false;}
        public boolean ready(){return ready;}
        public void invalidate(){ready=false;}
        public long revision(){return revision;}
        public List<Cut> cuts(){return cuts;}
        public Snapshot snapshot(){if(!ready)throw new IllegalStateException("Map edits need snapshot");return new Snapshot(epoch,revision,cuts);}
        public Applied accept(Snapshot s){
            if(epoch==0||s.epoch()!=epoch||Long.compareUnsigned(s.revision(),revision)<0||(ready&&s.revision()==revision))return Applied.IGNORED;
            cuts=s.cuts();revision=s.revision();ready=true;return Applied.CHANGED;
        }
        public Applied accept(Delta d){
            if(epoch==0||d.epoch()!=epoch||Long.compareUnsigned(d.revision(),revision)<=0)return Applied.IGNORED;
            if(!ready||d.base()!=revision){ready=false;return Applied.NEED_SNAPSHOT;}
            var next=new ArrayList<>(cuts);
            if(d.operation()==ADD){
                if(next.size()>=MAX_CUTS)throw new IllegalArgumentException("Map edit capacity");
                next.add(new Cut(d.revision(),d.target(),d.box()));
            }else if(d.operation()==REMOVE_TARGET)next.removeIf(c->c.target().equals(d.target()));
            else next.clear();
            cuts=List.copyOf(next);revision=d.revision();return Applied.CHANGED;
        }
    }
    private MapEdits(){}
}
