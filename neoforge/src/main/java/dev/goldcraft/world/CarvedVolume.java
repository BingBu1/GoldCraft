package dev.goldcraft.world;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/** Double-precision counterpart of native world_volume/edited_hull geometry.
 * Built once per accepted edit, never by a Minecraft collision-cell query. */
final class CarvedVolume {
    private static final double EPS = Math.ulp(1.0);
    record P(double x, double y, double z) {
        P { if (!Double.isFinite(x)||!Double.isFinite(y)||!Double.isFinite(z))
            throw new IllegalArgumentException("Carved coordinate bounds"); }
        P(BspMap.Vec p) { this(p.x(),p.y(),p.z()); }
        double at(int i) { return i==0?x:i==1?y:z; }
        P at(int i,double v) { return new P(i==0?v:x,i==1?v:y,i==2?v:z); }
        P add(P p) { return new P(x+p.x,y+p.y,z+p.z); }
        P sub(P p) { return new P(x-p.x,y-p.y,z-p.z); }
        P scale(double k) { return new P(x*k,y*k,z*k); }
        double dot(P p) { return x*p.x+y*p.y+z*p.z; }
        P cross(P p) { return new P(y*p.z-z*p.y,z*p.x-x*p.z,x*p.y-y*p.x); }
        BspMap.Vec vec() { return new BspMap.Vec((float)x,(float)y,(float)z); }
    }
    static final P ZERO = new P(0,0,0);
    record Plane(P n,double d) {
        Plane reverse() { return new Plane(n.scale(-1),-d); }
        double distance(P p) { return p.dot(n)-d; }
        double roundedDistance(P p) {
            double value=distance(p),magnitude=Math.abs(d)+Math.abs(p.x*n.x)+Math.abs(p.y*n.y)+Math.abs(p.z*n.z);
            return Math.abs(value)<=16*EPS*Math.max(1,magnitude)?0:value;
        }
    }
    record Box(P min,P max) {
        Box { for(int i=0;i<3;i++)if(min.at(i)>max.at(i)||Math.abs(min.at(i))>1_000_000||Math.abs(max.at(i))>1_000_000)throw new IllegalArgumentException("Carved bounds"); }
        boolean overlaps(Box b) { for(int i=0;i<3;i++)if(max.at(i)<=b.min.at(i)||min.at(i)>=b.max.at(i))return false;return true; }
        boolean lineBounds(P a,P b) { for(int i=0;i<3;i++)if(Math.min(a.at(i),b.at(i))>max.at(i)||Math.max(a.at(i),b.at(i))<min.at(i))return false;return true; }
    }
    record Face(Plane plane,List<P> vertices) {}
    record Cell(List<Face> faces) {}
    record CollisionCell(Box bounds,List<Plane> planes) {}
    record Span(double begin,double end,Plane enter,Plane leave) {}
    static final Plane NO_PLANE = new Plane(ZERO,0);
    static final Cell NO_CELL = new Cell(List.of());
    static final class Work {
        final long limit;
        final int capacity;
        long used;
        Work(long limit,int capacity) {
            if(limit<=0||capacity<=0)throw new IllegalArgumentException("Carved work limits");
            this.limit=limit;this.capacity=capacity;
        }
        void use(long count) { if(count<0||count>limit-used)throw new IllegalArgumentException("Carved operation budget");used+=count; }
        void add(List<Cell> list,Cell cell) {
            if(cell.faces.isEmpty())return;
            if(list.size()>=capacity)throw new IllegalArgumentException("Carved cell budget");
            list.add(cell);
        }
    }
    static Plane plane(BspMap.Hull hull,int index) {
        var p=hull.planes()[index];
        return new Plane(p.type()<3?ZERO.at(p.type(),1):new P(p.normal()),p.distance());
    }
    static Plane axial(int axis,boolean upper,double value) {
        return new Plane(ZERO.at(axis,upper?1:-1),upper?value:-value);
    }
    private static Cell cube(Box box) {
        var faces=new ArrayList<Face>();
        for(int axis=0;axis<3;axis++)for(boolean upper:new boolean[]{false,true}) {
            int u=(axis+1)%3,v=(axis+2)%3;
            double distance=(upper?box.max:box.min).at(axis);
            var points=new ArrayList<P>();
            for(int j=0;j<4;j++)points.add(ZERO.at(axis,distance).at(u,(j==1||j==2?box.max:box.min).at(u)).at(v,(j>=2?box.max:box.min).at(v)));
            if(!upper)Collections.reverse(points);
            faces.add(new Face(axial(axis,upper,distance),points));
        }
        return new Cell(faces);
    }
    private static boolean equal(P a,P b) {
        for(int i=0;i<3;i++)if(Math.abs(a.at(i)-b.at(i))>32*EPS*Math.max(1,Math.max(Math.abs(a.at(i)),Math.abs(b.at(i)))))return false;
        return true;
    }
    private static void unique(List<P> points,P p,Work work) {
        work.use(points.size()+1L);
        for(var q:points)if(equal(p,q))return;
        points.add(p);
    }
    private static boolean area(List<P> points) {
        if(points.size()<3)return false;
        P normal=ZERO;
        for(int i=1;i+1<points.size();i++)normal=normal.add(points.get(i).sub(points.getFirst()).cross(points.get(i+1).sub(points.getFirst())));
        return normal.dot(normal)>1e-28;
    }
    private static Cell clip(Cell input,Plane plane,Work work) {
        boolean negative=false,positive=false;
        for(var face:input.faces)for(var p:face.vertices) {
            work.use(1);double d=plane.roundedDistance(p);negative|=d<0;positive|=d>0;
        }
        if(!positive)return input;
        if(!negative)return NO_CELL;
        var faces=new ArrayList<Face>();var cap=new ArrayList<P>();
        for(var source:input.faces) {
            var points=new ArrayList<P>();
            for(int i=0;i<source.vertices.size();i++) {
                work.use(1);
                var a=source.vertices.get(i);var b=source.vertices.get((i+1)%source.vertices.size());
                double da=plane.roundedDistance(a),db=plane.roundedDistance(b);
                if(da<=0)points.add(a);
                if(da==0)unique(cap,a,work);
                if((da<0&&db>0)||(da>0&&db<0)) {
                    var p=a.add(b.sub(a).scale(da/(da-db)));
                    int axis=0;
                    for(int j=1;j<3;j++)if(Math.abs(plane.n.at(j))>Math.abs(plane.n.at(axis)))axis=j;
                    p=p.at(axis,0);p=p.at(axis,(plane.d-p.dot(plane.n))/plane.n.at(axis));
                    points.add(p);unique(cap,p,work);
                }
            }
            if(area(points))faces.add(new Face(source.plane,points));
        }
        if(cap.size()<3)throw new IllegalArgumentException("Carved cap precision");
        P center=ZERO;
        for(var p:cap)center=center.add(p.scale(1.0/cap.size()));
        int axis=0;
        for(int j=1;j<3;j++)if(Math.abs(plane.n.at(j))<Math.abs(plane.n.at(axis)))axis=j;
        var u=ZERO.at(axis,1).cross(plane.n);var v=plane.n.cross(u);var origin=center;
        work.use((long)cap.size()*cap.size());
        cap.sort(Comparator.comparingDouble(p->{var q=p.sub(origin);return Math.atan2(q.dot(v),q.dot(u));}));
        if(!area(cap))throw new IllegalArgumentException("Carved cap has no area");
        faces.add(new Face(plane,cap));return new Cell(faces);
    }
    private static void collect(BspMap.Hull hull,int node,Cell cell,Work work,List<Cell> output) {
        if(cell.faces.isEmpty())return;
        work.use(1);
        if(node<0) { if(node==BspMap.SOLID)work.add(output,cell);return; }
        var branch=hull.nodes()[node];var plane=plane(hull,branch.plane());
        collect(hull,branch.front(),clip(cell,plane.reverse(),work),work,output);
        collect(hull,branch.back(),clip(cell,plane,work),work,output);
    }
    private static Box bounds(Cell cell) {
        P min=cell.faces.getFirst().vertices.getFirst(),max=min;
        for(var face:cell.faces)for(var p:face.vertices)for(int i=0;i<3;i++) {
            min=min.at(i,Math.min(min.at(i),p.at(i)));max=max.at(i,Math.max(max.at(i),p.at(i)));
        }
        return new Box(min,max);
    }
    private static void subtract(Cell cell,Box box,Work work,List<Cell> output) {
        if(!bounds(cell).overlaps(box)) { work.add(output,cell);return; }
        Cell remaining=cell;var pieces=new ArrayList<Cell>();
        for(int axis=0;axis<3;axis++)for(boolean upper:new boolean[]{false,true}) {
            var plane=axial(axis,upper,(upper?box.max:box.min).at(axis));
            var inside=clip(remaining,plane,work);
            if(inside.faces.isEmpty()) { work.add(output,cell);return; }
            work.add(pieces,clip(remaining,plane.reverse(),work));remaining=inside;
        }
        for(var piece:pieces)work.add(output,piece);
    }
    static List<Cell> carve(BspMap.Hull hull,Box region,List<Box> cuts,Work work) {
        for(int i=0;i<3;i++)if(region.min.at(i)>=region.max.at(i))throw new IllegalArgumentException("Carved region volume");
        work.use(cuts.size());
        var cells=new ArrayList<Cell>();collect(hull,hull.root(),cube(region),work,cells);
        for(var cut:cuts) {
            var next=new ArrayList<Cell>();
            for(var cell:cells) { work.use(1);subtract(cell,cut,work,next); }
            cells=next;
        }
        return cells;
    }
    private static void addPlane(List<Plane> planes,List<P> points,Box body,P normal,Work work) {
        work.use(1);double length=Math.sqrt(normal.dot(normal));if(length<1e-12)return;
        normal=normal.scale(1/length);work.use(planes.size());
        for(var p:planes)if(equal(p.n,normal))return;
        double distance=Double.NEGATIVE_INFINITY;work.use(points.size());
        for(var p:points)distance=Math.max(distance,p.dot(normal));
        for(int i=0;i<3;i++)distance-=normal.at(i)*(normal.at(i)>=0?body.min:body.max).at(i);
        planes.add(new Plane(normal,distance));
    }
    static List<CollisionCell> expand(List<Cell> cells,Box body,Work work) {
        var result=new ArrayList<CollisionCell>();
        for(var cell:cells) {
            if(cell.faces.size()<4)throw new IllegalArgumentException("Incomplete carved convex cell");
            var original=bounds(cell);var box=new Box(original.min.sub(body.max),original.max.sub(body.min));
            var points=new ArrayList<P>();var planes=new ArrayList<Plane>();
            for(var f:cell.faces)for(var p:f.vertices)unique(points,p,work);
            for(int axis=0;axis<3;axis++) {
                var n=ZERO.at(axis,1);addPlane(planes,points,body,n,work);addPlane(planes,points,body,n.scale(-1),work);
            }
            for(var f:cell.faces) {
                work.use(1);addPlane(planes,points,body,f.plane.n,work);
                for(int i=0;i<f.vertices.size();i++) {
                    var edge=f.vertices.get((i+1)%f.vertices.size()).sub(f.vertices.get(i));
                    for(int axis=0;axis<3;axis++) {
                        var bevel=edge.cross(ZERO.at(axis,1));addPlane(planes,points,body,bevel,work);addPlane(planes,points,body,bevel.scale(-1),work);
                    }
                }
            }
            if(result.size()>=work.capacity)throw new IllegalArgumentException("Carved collision cell budget");
            result.add(new CollisionCell(box,List.copyOf(planes)));
        }
        return List.copyOf(result);
    }
    private static boolean negative(Plane plane,double distance) {
        if(distance!=0)return distance<0;
        for(int i=0;i<3;i++)if(plane.n.at(i)!=0)return plane.n.at(i)<0;
        return false;
    }
    static boolean contains(List<CollisionCell> cells,P point) {
        for(var cell:cells) {
            if(!cell.bounds.lineBounds(point,point))continue;
            boolean inside=true;
            for(var plane:cell.planes)if(!negative(plane,plane.distance(point))) { inside=false;break; }
            if(inside)return true;
        }
        return false;
    }
    static List<Span> spans(List<CollisionCell> cells,P start,P end) {
        var intervals=new ArrayList<Span>();var move=end.sub(start);
        for(var cell:cells) {
            if(!cell.bounds.lineBounds(start,end))continue;
            double enter=Double.NEGATIVE_INFINITY,exit=Double.POSITIVE_INFINITY;
            Plane in=NO_PLANE,out=NO_PLANE;boolean miss=false;
            for(var plane:cell.planes) {
                double distance=plane.distance(start),speed=move.dot(plane.n);
                if(speed==0) { if(!negative(plane,distance)) { miss=true;break; }continue; }
                double t=-distance/speed;
                if(speed<0) { if(t>enter) { enter=t;in=plane; } }
                else if(t<exit) { exit=t;out=plane; }
                if(enter>=exit) { miss=true;break; }
            }
            if(!miss&&enter<1&&exit>0)intervals.add(new Span(enter,exit,in,out));
        }
        return merge(intervals);
    }
    static List<Span> merge(List<Span> intervals) {
        intervals.sort(Comparator.comparingDouble(Span::begin));var merged=new ArrayList<Span>();
        for(var span:intervals) {
            if(merged.isEmpty()||span.begin>merged.getLast().end)merged.add(span);
            else if(span.end>merged.getLast().end) {
                var prev=merged.getLast();merged.set(merged.size()-1,new Span(prev.begin,span.end,prev.enter,span.leave));
            }
        }
        return merged;
    }
    static List<Span> subtract(List<Span> a,List<Span> b) {
        var output=new ArrayList<Span>();int first=0;
        for(var part:a) {
            while(first<b.size()&&b.get(first).end<=part.begin)first++;
            for(int i=first;i<b.size()&&b.get(i).begin<part.end;i++) {
                var cut=b.get(i);
                if(cut.begin>part.begin)output.add(new Span(part.begin,cut.begin,part.enter,cut.enter.reverse()));
                if(cut.end>=part.end) { part=new Span(part.end,part.end,part.enter,part.leave);break; }
                part=new Span(cut.end,part.end,cut.leave.reverse(),part.leave);
            }
            if(part.begin<part.end)output.add(part);
        }
        return output;
    }
    private CarvedVolume() {}
}
