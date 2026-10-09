package dev.goldcraft.world;

import java.util.ArrayList;
import java.util.List;
import static dev.goldcraft.world.CarvedVolume.*;

/** Native H minus (O minus R). Keeps native empty space and observable clip-only
 * obstacles; clip brushes hidden inside point-solid cannot be inferred from BSP. */
final class CarvedHull {
    private final BspMap.Hull nativeHull;
    private final Box affected;
    private final List<CollisionCell> original,remaining;
    final long operations;
    CarvedHull(BspMap map,int model,int hull,List<Box> cuts,long limit) {
        nativeHull=map.hull(model,hull);
        if(cuts.isEmpty())throw new IllegalArgumentException("Carved hull needs cuts");
        Box body=body(hull);P min=cuts.getFirst().min(),max=cuts.getFirst().max();
        for(var cut:cuts)for(int i=0;i<3;i++) {
            min=min.at(i,Math.min(min.at(i),cut.min().at(i)));max=max.at(i,Math.max(max.at(i),cut.max().at(i)));
        }
        affected=new Box(min.sub(body.max()),max.sub(body.min()));
        var padding=new P(1,1,1);
        var region=new Box(affected.min().add(body.min()).sub(padding),affected.max().add(body.max()).add(padding));
        var work=new Work(limit,4096);
        var old=carve(map.hull(model,0),region,List.of(),work);
        var next=carve(map.hull(model,0),region,cuts,work);
        original=expand(old,body,work);remaining=expand(next,body,work);operations=work.used;
    }
    private static Box body(int hull) {
        return switch(hull) {
            case 0 -> new Box(ZERO,ZERO);
            case 1 -> new Box(new P(-16,-16,-36),new P(16,16,36));
            case 2 -> new Box(new P(-32,-32,-32),new P(32,32,32));
            case 3 -> new Box(new P(-16,-16,-18),new P(16,16,18));
            default -> throw new IllegalArgumentException("Carved hull index");
        };
    }
    boolean affects(BspMap.Vec a,BspMap.Vec b) { return affected.lineBounds(new P(a),new P(b)); }
    private int nativeContents(P point) {
        int node=nativeHull.root();
        while(node>=0) {
            var branch=nativeHull.nodes()[node];
            node=plane(nativeHull,branch.plane()).distance(point)>=0?branch.front():branch.back();
        }
        return node;
    }
    int contents(BspMap.Vec point) { return contents(new P(point)); }
    private int contents(P point) {
        int old=nativeContents(point);
        return old==BspMap.SOLID&&affected.lineBounds(point,point)&&contains(original,point)&&!contains(remaining,point)?BspMap.EMPTY:old;
    }
    private record Leaf(Span interval,int contents) {}
    private void gather(int node,P start,P delta,Span interval,List<Leaf> output,Work work) {
        work.use(1);
        if(node<0) { output.add(new Leaf(interval,node));return; }
        var branch=nativeHull.nodes()[node];var p=plane(nativeHull,branch.plane());
        double distance=p.distance(start),speed=delta.dot(p.n());
        double a=distance+speed*interval.begin(),b=distance+speed*interval.end();
        if(a>=0&&b>=0) { gather(branch.front(),start,delta,interval,output,work);return; }
        if(a<0&&b<0) { gather(branch.back(),start,delta,interval,output,work);return; }
        double t=Math.clamp(-distance/speed,interval.begin(),interval.end());
        int near=speed>0?1:0;var hit=near==1?p.reverse():p;
        if(t>interval.begin())gather(branch.child(near),start,delta,new Span(interval.begin(),t,interval.enter(),hit.reverse()),output,work);
        if(t<interval.end())gather(branch.child(near^1),start,delta,new Span(t,interval.end(),hit,interval.leave()),output,work);
    }
    BspMap.Trace trace(BspMap.Vec a,BspMap.Vec b) {
        var start=new P(a);var end=new P(b);var delta=end.sub(start);
        var leaves=new ArrayList<Leaf>();gather(nativeHull.root(),start,delta,new Span(0,1,NO_PLANE,NO_PLANE),leaves,new Work(4_194_304,4096));
        var nativeSpans=new ArrayList<Span>();for(var leaf:leaves)if(leaf.contents==BspMap.SOLID)nativeSpans.add(leaf.interval);
        var removed=subtract(spans(original,start,end),spans(remaining,start,end));
        var spans=merge(subtract(nativeSpans,removed));
        boolean startSolid=contents(start)==BspMap.SOLID;
        boolean allSolid=startSolid&&contents(end)==BspMap.SOLID&&!spans.isEmpty()&&spans.getFirst().begin()<=0&&spans.getFirst().end()>=1;
        double fraction=1;Plane plane=NO_PLANE;
        for(var span:spans) {
            if(span.begin()<0||(span.begin()==0&&startSolid))continue;
            double speed=-delta.dot(span.enter().n());
            fraction=Math.clamp(span.begin()-(speed>0?0.03125/speed:0),0,1);plane=span.enter();break;
        }
        boolean open=false,water=false;
        for(var leaf:leaves) {
            if(leaf.interval.begin()>fraction)break;
            if(leaf.contents==BspMap.EMPTY)open=true;
            else if(leaf.contents!=BspMap.SOLID&&leaf.contents!=-15)water=true;
            else if(leaf.contents==BspMap.SOLID)for(var hole:removed) {
                if(Math.max(leaf.interval.begin(),hole.begin())<Math.min(Math.min(leaf.interval.end(),hole.end()),fraction))open=true;
            }
        }
        float f=(float)fraction;
        return new BspMap.Trace(f,a.add(b.subtract(a).scale(f)),plane.n().vec(),(float)plane.d(),startSolid,allSolid,open,water);
    }
}
