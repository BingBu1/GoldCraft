package dev.goldcraft.world;

import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import static dev.goldcraft.world.CarvedVolume.*;

/** Double-precision broad phase; inclusive bounds retain shared-plane contacts. */
final class CarvedBoundsIndex {
    private record Entry(int ordinal,Box box) {}
    private record Node(Box bounds,int first,int last,Node left,Node right) {}
    private final Entry[] entries;
    private final Node root;
    static final class Candidates {
        private int[] ids=new int[8];
        private int size;
        int size(){return size;}
        int get(int at){return ids[at];}
        private void add(int id) {
            if(size==ids.length)ids=Arrays.copyOf(ids,ids.length*2);
            ids[size++]=id;
        }
    }
    CarvedBoundsIndex(List<Box> boxes,Work work) {
        work.use(boxes.size());entries=new Entry[boxes.size()];
        for(int i=0;i<entries.length;i++)entries[i]=new Entry(i,boxes.get(i));
        root=entries.length==0?null:build(0,entries.length,work);
    }
    private Node build(int first,int last,Work work) {
        work.use(last-first);var box=entries[first].box();
        double x=box.min().x(),y=box.min().y(),z=box.min().z();
        double u=box.max().x(),v=box.max().y(),w=box.max().z();
        for(int i=first+1;i<last;i++) {
            box=entries[i].box();x=Math.min(x,box.min().x());y=Math.min(y,box.min().y());z=Math.min(z,box.min().z());
            u=Math.max(u,box.max().x());v=Math.max(v,box.max().y());w=Math.max(w,box.max().z());
        }
        var bounds=new Box(new P(x,y,z),new P(u,v,w));
        if(last-first<=8)return new Node(bounds,first,last,null,null);
        int axis=u-x>=v-y&&u-x>=w-z?0:v-y>=w-z?1:2;
        work.use((long)(last-first)*(32-Integer.numberOfLeadingZeros(last-first)));
        Arrays.sort(entries,first,last,Comparator.comparingDouble((Entry e)->e.box().min().at(axis)+e.box().max().at(axis)).thenComparingInt(Entry::ordinal));
        int middle=first+(last-first)/2;
        return new Node(bounds,first,last,build(first,middle,work),build(middle,last,work));
    }
    private void visit(Node node,P a,P b,Candidates output,Work work) {
        if(node==null)return;
        if(work!=null)work.use(1);
        if(!node.bounds().lineBounds(a,b))return;
        if(node.left()!=null) {
            visit(node.left(),a,b,output,work);visit(node.right(),a,b,output,work);
        }else {
            if(work!=null)work.use(node.last()-node.first());
            for(int i=node.first();i<node.last();i++)
                if(entries[i].box().lineBounds(a,b))output.add(entries[i].ordinal());
        }
    }
    void query(P a,P b,Candidates output,Work work) {
        output.size=0;visit(root,a,b,output,work);
        if(work!=null)work.use((long)output.size*(32-Integer.numberOfLeadingZeros(output.size)));
        Arrays.sort(output.ids,0,output.size);
    }
    // Existence has no plane tie ordering; stop at the first real solid cell.
    boolean contains(List<CollisionCell> cells,P point) {return contains(root,cells,point);}
    private boolean contains(Node node,List<CollisionCell> cells,P point) {
        if(node==null||!node.bounds().lineBounds(point,point))return false;
        if(node.left()!=null)return contains(node.left(),cells,point)||contains(node.right(),cells,point);
        for(int i=node.first();i<node.last();i++)
            if(CarvedVolume.contains(cells.get(entries[i].ordinal()),point))return true;
        return false;
    }
}
