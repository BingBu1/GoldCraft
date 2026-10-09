package dev.goldcraft.world;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.zip.CRC32;

/** Bounded BSP30 reader. Coordinates and standard hull traces use GoldSrc units. */
public final class BspMap {
    public static final int MAX_BYTES=32*1024*1024, EMPTY=-1, SOLID=-2;
    public static final int CLEAR=1, FILLED=2, MIXED=3;
    private static final float TRACE_EPSILON=0.03125f;
    public record Vec(float x,float y,float z) {
        public Vec { if(!Float.isFinite(x)||!Float.isFinite(y)||!Float.isFinite(z))throw invalid("Non-finite coordinate"); }
        public Vec add(Vec b){return new Vec(x+b.x,y+b.y,z+b.z);}
        public Vec subtract(Vec b){return new Vec(x-b.x,y-b.y,z-b.z);}
        public Vec scale(float f){return new Vec(x*f,y*f,z*f);}
        public float dot(Vec b){return x*b.x+y*b.y+z*b.z;}
        public float axis(int i){return i==0?x:i==1?y:z;}
    }
    public record Bounds(Vec min,Vec max) {
        public Bounds {if(min.x>max.x||min.y>max.y||min.z>max.z)throw invalid("Inverted bounds");}
    }
    public record Trace(float fraction,Vec end,Vec normal,float planeDistance,boolean startSolid,boolean allSolid,boolean inOpen,boolean inWater) {}
    record Plane(Vec normal,float distance,int type) {
        float signedDistance(Vec point){return (type<3?point.axis(type):normal.dot(point))-distance;}
    }
    record Node(int plane,int front,int back) {int child(int side){return side==0?front:back;}}
    record Hull(Plane[] planes,Node[] nodes,int root) {}
    private record Model(Bounds bounds,Vec origin,int[] heads) {}
    private final Plane[] planes;
    private final Node[] pointNodes,clipNodes;
    private final Model[] models;
    private final long crc;

    private BspMap(Plane[] planes,Node[] nodes,Node[] clips,Model[] models,long crc) {
        this.planes=planes;pointNodes=nodes;clipNodes=clips;this.models=models;this.crc=crc;
    }
    private static IllegalArgumentException invalid(String message){return new IllegalArgumentException("BSP30: "+message);}
    private static Vec vec(ByteBuffer b){return new Vec(b.getFloat(),b.getFloat(),b.getFloat());}
    public static long checksum(byte[] bytes){CRC32 c=new CRC32();c.update(bytes);return c.getValue();}
    public long crc(){return crc;}
    public int modelCount(){return models.length;}
    public int planeCount(){return planes.length;}
    public int nodeCount(){return pointNodes.length;}
    public Bounds bounds(int model){return model(model).bounds;}
    private Model model(int index){if(index<0||index>=models.length)throw invalid("Model index");return models[index];}
    Hull hull(int model,int hull){if(hull<0||hull>3)throw invalid("Hull index");return new Hull(planes,hull==0?pointNodes:clipNodes,model(model).heads[hull]);}

    public static BspMap read(byte[] bytes) {
        if(bytes.length<124||bytes.length>MAX_BYTES)throw invalid("File size");
        ByteBuffer data=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if(data.getInt()!=30)throw invalid("Only classic GoldSrc BSP version 30 is supported");
        int[] offsets=new int[15],lengths=new int[15];
        for(int i=0;i<15;i++) {
            int offset=data.getInt(),length=data.getInt();
            if(offset<0||length<0||(long)offset+length>bytes.length||(length>0&&offset<124))throw invalid("Lump range "+i);
            offsets[i]=offset;lengths[i]=length;
            for(int j=0;j<i;j++)if(length>0&&lengths[j]>0&&offset<offsets[j]+lengths[j]&&offsets[j]<offset+length)throw invalid("Overlapping lumps");
        }
        Plane[] planes=new Plane[count(lengths[1],20,65535,"planes")];
        data.position(offsets[1]);
        for(int i=0;i<planes.length;i++) {
            Vec normal=vec(data);float distance=data.getFloat();int type=data.getInt();
            if(!Float.isFinite(distance)||Math.abs(distance)>1_000_000||type<0||type>5||Math.abs(normal.dot(normal)-1)>0.02f)throw invalid("Plane");
            if(type<3&&Math.abs(Math.abs(normal.axis(type))-1)>0.001f)throw invalid("Axial plane orientation");
            planes[i]=new Plane(normal,distance,type);
        }
        int[] contents=new int[count(lengths[10],28,32767,"leaves")];
        for(int i=0;i<contents.length;i++) {contents[i]=data.getInt(offsets[10]+i*28);validateContents(contents[i]);}
        Node[] nodes=new Node[count(lengths[5],24,32767,"nodes")];
        for(int i=0;i<nodes.length;i++) {
            data.position(offsets[5]+i*24);int plane=data.getInt();
            int a=leafChild(data.getShort(),contents,nodes.length),b=leafChild(data.getShort(),contents,nodes.length);
            if(plane<0||plane>=planes.length)throw invalid("Node plane");
            nodes[i]=new Node(plane,a,b);
        }
        Node[] clips=new Node[count(lengths[9],8,32767,"clipnodes")];
        for(int i=0;i<clips.length;i++) {
            data.position(offsets[9]+i*8);int plane=data.getInt(),a=data.getShort(),b=data.getShort();
            if(plane<0||plane>=planes.length)throw invalid("Clipnode plane");
            validateChild(a,clips.length);validateChild(b,clips.length);clips[i]=new Node(plane,a,b);
        }
        Model[] models=new Model[count(lengths[14],64,4096,"models")];
        for(int i=0;i<models.length;i++) {
            data.position(offsets[14]+i*64);Bounds bounds=new Bounds(vec(data),vec(data));Vec origin=vec(data);int[] heads=new int[4];
            for(int j=0;j<4;j++){heads[j]=data.getInt();validateChild(heads[j],j==0?nodes.length:clips.length);}
            models[i]=new Model(bounds,origin,heads);
        }
        if(planes.length==0||nodes.length==0||clips.length==0||contents.length==0||models.length==0)throw invalid("Missing collision lump");
        validateTree(nodes);validateTree(clips);
        return new BspMap(planes,nodes,clips,models,checksum(bytes));
    }
    private static int count(int size,int stride,int limit,String name){if(size%stride!=0||size/stride>limit)throw invalid("Record count: "+name);return size/stride;}
    private static void validateContents(int c){if(c>-1||c<-15)throw invalid("Contents value");}
    private static void validateChild(int c,int count){if(c<0)validateContents(c);else if(c>=count)throw invalid("Node child");}
    private static int leafChild(int c,int[] contents,int count){if(c>=0){if(c>=count)throw invalid("Node child");return c;}int leaf=-1-c;if(leaf>=contents.length)throw invalid("Leaf child");return contents[leaf];}
    private static void validateTree(Node[] nodes){byte[] state=new byte[nodes.length];for(int i=0;i<nodes.length;i++)validateBranch(nodes,state,i,0);}
    private static void validateBranch(Node[] nodes,byte[] state,int index,int depth) {
        if(index<0)return;
        if(depth>256)throw invalid("Tree depth exceeds 256");
        if(state[index]==1)throw invalid("Cyclic tree");
        if(state[index]==2)return;
        state[index]=1;validateBranch(nodes,state,nodes[index].front,depth+1);validateBranch(nodes,state,nodes[index].back,depth+1);state[index]=2;
    }
    private int contents(Node[] nodes,int node,Vec point) {
        while(node>=0){Node n=nodes[node];node=planes[n.plane].signedDistance(point)<0?n.back:n.front;}
        return node;
    }
    public int contents(int model,int hull,Vec point){if(hull<0||hull>3)throw invalid("Hull index");return contents(hull==0?pointNodes:clipNodes,model(model).heads[hull],point);}

    /** Returns CLEAR/FILLED/MIXED for a box in model-local space; mixed results can be subdivided. */
    public int classify(int model,Bounds box){return classify(pointNodes,model(model).heads[0],box.min.add(box.max).scale(0.5f),box.max.subtract(box.min).scale(0.5f));}
    private int classify(Node[] nodes,int node,Vec center,Vec radius) {
        if(node<0)return node==SOLID?FILLED:CLEAR;
        Node n=nodes[node];Plane p=planes[n.plane];float d=p.signedDistance(center);
        float extent=Math.abs(p.normal.x)*radius.x+Math.abs(p.normal.y)*radius.y+Math.abs(p.normal.z)*radius.z;
        if(d-extent>=-0.00001f)return classify(nodes,n.front,center,radius);
        if(d+extent<=0.00001f)return classify(nodes,n.back,center,radius);
        int a=classify(nodes,n.front,center,radius);if(a==MIXED)return MIXED;
        return a|classify(nodes,n.back,center,radius);
    }

    public Trace trace(int model,int hull,Vec start,Vec end) {
        if(hull<0||hull>3)throw invalid("Hull index");
        Model m=model(model);Node[] nodes=hull==0?pointNodes:clipNodes;MutableTrace result=new MutableTrace(end);
        trace(nodes,m.heads[hull],m.heads[hull],0,1,start,end,result);
        return new Trace(result.fraction,result.end,result.normal,result.planeDistance,result.startSolid,result.allSolid,result.inOpen,result.inWater);
    }
    private static final class MutableTrace {
        float fraction=1,planeDistance;Vec end,normal=new Vec(0,0,0);boolean startSolid,allSolid=true,inOpen,inWater;
        MutableTrace(Vec end){this.end=end;}
    }
    /* Same near-side epsilon semantics as the engine trace contract. This is verified against
       real pfnTraceModel calls, rather than assuming the ReHLDS reference equals hw.dll. */
    private boolean trace(Node[] nodes,int root,int node,float from,float to,Vec start,Vec end,MutableTrace result) {
        if(node<0){if(node==SOLID)result.startSolid=true;else {result.allSolid=false;if(node==EMPTY)result.inOpen=true;else if(node!=-15)result.inWater=true;}return true;}
        Node n=nodes[node];Plane plane=planes[n.plane];float a=plane.signedDistance(start),b=plane.signedDistance(end);
        if(a>=0&&b>=0)return trace(nodes,root,n.front,from,to,start,end,result);
        if(a<0&&b<0)return trace(nodes,root,n.back,from,to,start,end,result);
        int side=a<0?1:0;float fraction=Math.clamp((a+(side==1?TRACE_EPSILON:-TRACE_EPSILON))/(a-b),0,1);
        float middle=from+(to-from)*fraction;Vec point=start.add(end.subtract(start).scale(fraction));
        if(!trace(nodes,root,n.child(side),from,middle,start,point,result))return false;
        if(contents(nodes,n.child(side^1),point)!=SOLID)return trace(nodes,root,n.child(side^1),middle,to,point,end,result);
        if(result.allSolid)return false;
        result.normal=side==0?plane.normal:plane.normal.scale(-1);result.planeDistance=side==0?plane.distance:-plane.distance;
        while(contents(nodes,root,point)==SOLID) {
            fraction-=0.1f;
            if(fraction<0)break;
            middle=from+(to-from)*fraction;point=start.add(end.subtract(start).scale(fraction));
        }
        result.fraction=middle;result.end=point;return false;
    }
}
