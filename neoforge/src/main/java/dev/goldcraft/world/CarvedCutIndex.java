package dev.goldcraft.world;

import dev.goldcraft.bridge.MapEdits;
import java.util.*;

/** Immutable broad phase. Preserve source ordering for exact subtraction. */
final class CarvedCutIndex {
    private record Entry(int ordinal,MapEdits.Box box) {}
    private record Node(MapEdits.Box bounds,int first,int last,Node left,Node right) {}
    private final Entry[] entries;
    private final Node root;
    CarvedCutIndex(List<MapEdits.Box> boxes) {
        entries=new Entry[boxes.size()];
        for(int i=0;i<entries.length;i++)entries[i]=new Entry(i,boxes.get(i));
        root=entries.length==0?null:build(0,entries.length);
    }
    private Node build(int first,int last) {
        var b=entries[first].box();
        float x=b.minX(),y=b.minY(),z=b.minZ(),u=b.maxX(),v=b.maxY(),w=b.maxZ();
        for(int i=first+1;i<last;i++) {
            b=entries[i].box();x=Math.min(x,b.minX());y=Math.min(y,b.minY());z=Math.min(z,b.minZ());
            u=Math.max(u,b.maxX());v=Math.max(v,b.maxY());w=Math.max(w,b.maxZ());
        }
        var bounds=new MapEdits.Box(x,y,z,u,v,w);
        if(last-first<=8)return new Node(bounds,first,last,null,null);
        int axis=u-x>=v-y&&u-x>=w-z?0:v-y>=w-z?1:2;
        Arrays.sort(entries,first,last,Comparator.comparingDouble((Entry e)->center(e.box(),axis)).thenComparingInt(Entry::ordinal));
        int middle=first+(last-first)/2;
        return new Node(bounds,first,last,build(first,middle),build(middle,last));
    }
    private static double center(MapEdits.Box b,int axis) {
        return axis==0?(double)b.minX()+b.maxX():axis==1?(double)b.minY()+b.maxY():(double)b.minZ()+b.maxZ();
    }
    private static boolean overlaps(MapEdits.Box b,BspMap.Bounds q) {
        return b.maxX()>q.min().x()&&b.minX()<q.max().x()&&b.maxY()>q.min().y()&&b.minY()<q.max().y()
            &&b.maxZ()>q.min().z()&&b.minZ()<q.max().z();
    }
    private void visit(Node n,BspMap.Bounds query,List<Entry> found) {
        if(n==null||!overlaps(n.bounds(),query))return;
        if(n.left()!=null){visit(n.left(),query,found);visit(n.right(),query,found);}
        else for(int i=n.first();i<n.last();i++)if(overlaps(entries[i].box(),query))found.add(entries[i]);
    }
    List<MapEdits.Box> query(BspMap.Bounds query) {
        if(root==null||!overlaps(root.bounds(),query))return List.of();
        var found=new ArrayList<Entry>();visit(root,query,found);
        found.sort(Comparator.comparingInt(Entry::ordinal));
        return found.stream().map(Entry::box).toList();
    }
    boolean any(BspMap.Bounds query) { return any(root,query); }
    private boolean any(Node node,BspMap.Bounds query) {
        if(node==null||!overlaps(node.bounds(),query))return false;
        if(node.left()!=null)return any(node.left(),query)||any(node.right(),query);
        for(int i=node.first();i<node.last();i++)if(overlaps(entries[i].box(),query))return true;
        return false;
    }
}
