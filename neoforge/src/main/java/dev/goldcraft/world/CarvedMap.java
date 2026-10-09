package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapEdits;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import static dev.goldcraft.world.CarvedVolume.*;

/** Immutable geometry prepared before a host edit becomes visible to physics. */
public final class CarvedMap {
    private static final MapEdits.Target WORLD=new MapEdits.Target(0,0,0);
    private record Target(List<MapEdits.Box> cuts,CarvedHull[] hulls) {}
    private final BspMap map;
    private final Map<MapEdits.Target,Target> targets;
    public CarvedMap(BspMap map,List<MapEdits.Cut> cuts) {
        this(map,cuts,32_000_000);
    }
    CarvedMap(BspMap map,List<MapEdits.Cut> cuts,long limit) {
        this.map=map;
        if(cuts.size()>MapEdits.MAX_CUTS)throw new IllegalArgumentException("Carved map capacity");
        var groups=new HashMap<MapEdits.Target,List<MapEdits.Box>>();
        for(var cut:cuts) {
            if(cut.target().model()>=map.modelCount())throw new IllegalArgumentException("Carved model index");
            groups.computeIfAbsent(cut.target(),ignored->new ArrayList<>()).add(cut.box());
        }
        var prepared=new HashMap<MapEdits.Target,Target>();long operations=0;
        for(var group:groups.entrySet()) {
            var boxes=group.getValue().stream().map(b->new Box(new P(b.minX(),b.minY(),b.minZ()),new P(b.maxX(),b.maxY(),b.maxZ()))).toList();
            var hulls=new CarvedHull[4];
            for(int hull=0;hull<4;hull++) {
                hulls[hull]=new CarvedHull(map,group.getKey().model(),hull,boxes,Math.min(4_194_304,limit-operations));
                operations+=hulls[hull].operations;
            }
            prepared.put(group.getKey(),new Target(List.copyOf(group.getValue()),hulls));
        }
        targets=Map.copyOf(prepared);
    }
    private static MapEdits.Target identity(HostWorldState.Brush brush) {
        return brush==null?WORLD:new MapEdits.Target(brush.slot(),brush.serial(),brush.model());
    }
    public BspMap.Trace trace(HostWorldState.Brush brush,int hull,BspMap.Vec start,BspMap.Vec end) {
        if(hull<0||hull>3)throw new IllegalArgumentException("Carved hull index");
        var target=identity(brush);var edit=targets.get(target);
        return edit!=null&&edit.hulls[hull].affects(start,end)?edit.hulls[hull].trace(start,end):map.trace(target.model(),hull,start,end);
    }
    public int contents(HostWorldState.Brush brush,int hull,BspMap.Vec point) {
        if(hull<0||hull>3)throw new IllegalArgumentException("Carved hull index");
        var target=identity(brush);var edit=targets.get(target);
        return edit!=null&&edit.hulls[hull].affects(point,point)?edit.hulls[hull].contents(point):map.contents(target.model(),hull,point);
    }
    /** Classify remaining material, keeping HostCollision's existing bounded voxel resolution.
     * Subtract the cut union from the query cell before consulting the point BSP. */
    public int classify(HostWorldState.Brush brush,BspMap.Bounds query) {
        var target=identity(brush);var edit=targets.get(target);
        if(edit==null)return map.classify(target.model(),query);
        var pieces=new ArrayList<BspMap.Bounds>();pieces.add(query);boolean removed=false;
        long work=0;
        for(var cut:edit.cuts) {
            var next=new ArrayList<BspMap.Bounds>();
            for(var piece:pieces) {
                if(++work>262144)throw new IllegalArgumentException("Carved voxel query budget");
                float[] lo={cut.minX(),cut.minY(),cut.minZ()},hi={cut.maxX(),cut.maxY(),cut.maxZ()};
                boolean overlaps=true;
                for(int i=0;i<3;i++)if(piece.max().axis(i)<=lo[i]||piece.min().axis(i)>=hi[i])overlaps=false;
                if(!overlaps)next.add(piece);
                else {
                    removed=true;var a=piece.min();var b=piece.max();
                    for(int i=0;i<3;i++) {
                        if(a.axis(i)<lo[i]) { next.add(new BspMap.Bounds(a,axis(b,i,lo[i])));a=axis(a,i,lo[i]); }
                        if(b.axis(i)>hi[i]) { next.add(new BspMap.Bounds(axis(a,i,hi[i]),b));b=axis(b,i,hi[i]); }
                    }
                }
                if(next.size()>4096)throw new IllegalArgumentException("Carved voxel fragment budget");
            }
            pieces=next;if(pieces.isEmpty())return BspMap.CLEAR;
        }
        int value=removed?BspMap.CLEAR:0;
        for(var piece:pieces) { value|=map.classify(target.model(),piece);if(value==BspMap.MIXED)break; }
        return value;
    }
    private static BspMap.Vec axis(BspMap.Vec p,int axis,float value) {
        return new BspMap.Vec(axis==0?value:p.x(),axis==1?value:p.y(),axis==2?value:p.z());
    }
}
