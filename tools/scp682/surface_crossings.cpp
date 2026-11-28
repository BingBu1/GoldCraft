// Offline narrow-phase test for originally separated patches of an articulated surface.
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace {
struct Vec {
    double x,y,z;
    double axis(int i) const { return i==0 ? x : i==1 ? y : z; }
    Vec operator-(Vec p) const { return {x-p.x,y-p.y,z-p.z}; }
    double dot(Vec p) const { return x*p.x+y*p.y+z*p.z; }
    Vec cross(Vec p) const { return {y*p.z-z*p.y,z*p.x-x*p.z,x*p.y-y*p.x}; }
};
struct Box {
    Vec lo,hi;
    static Box merge(Box a,Box b) {
        return {{std::min(a.lo.x,b.lo.x),std::min(a.lo.y,b.lo.y),std::min(a.lo.z,b.lo.z)},
                {std::max(a.hi.x,b.hi.x),std::max(a.hi.y,b.hi.y),std::max(a.hi.z,b.hi.z)}};
    }
    double gap2(Box b) const {
        auto d=[](double a,double c,double e,double f){return std::max({0.,a-f,e-c});};
        Vec v={d(lo.x,hi.x,b.lo.x,b.hi.x),d(lo.y,hi.y,b.lo.y,b.hi.y),d(lo.z,hi.z,b.lo.z,b.hi.z)};
        return v.dot(v);
    }
    bool overlaps(Box b) const {
        return lo.x<=b.hi.x && hi.x>=b.lo.x && lo.y<=b.hi.y && hi.y>=b.lo.y &&
               lo.z<=b.hi.z && hi.z>=b.lo.z;
    }
};
using Face=std::array<int,3>;
Box bounds(const Vec* vertices,Face face) {
    Box box={vertices[face[0]],vertices[face[0]]};
    for(int k=1;k<3;++k){Vec v=vertices[face[k]];box=Box::merge(box,{v,v});}
    return box;
}
bool pierces(Vec from,Vec to,Vec a,Vec b,Vec c) {
    Vec d=to-from,e=b-a,f=c-a;
    Vec cross=d.cross(f);
    double det=e.dot(cross);
    if(std::abs(det)<1e-13)return false;
    Vec offset=from-a;
    double u=offset.dot(cross)/det;
    Vec q=offset.cross(e);
    double v=d.dot(q)/det,t=f.dot(q)/det;
    constexpr double eps=1e-6;
    return u>eps && v>eps && u+v<1.-eps && t>eps && t<1.-eps;
}
bool crosses(const Vec* v,Face a,Face b) {
    for(int k=0;k<3;++k){
        if(pierces(v[a[k]],v[a[(k+1)%3]],v[b[0]],v[b[1]],v[b[2]]) ||
           pierces(v[b[k]],v[b[(k+1)%3]],v[a[0]],v[a[1]],v[a[2]]))return true;
    }
    return false;
}
struct Node {
    Box box{};
    int left=-1,right=-1,begin=0,end=0;
};
class Surface {
public:
    std::vector<Box> bind,posed;
    std::vector<Node> nodes;
    std::vector<int> order;
    Surface(const Vec* rest,const Vec* xyz,const Face* faces,int n):bind(n),posed(n),order(n) {
        std::iota(order.begin(),order.end(),0);
        for(int i=0;i<n;++i){bind[i]=bounds(rest,faces[i]);posed[i]=bounds(xyz,faces[i]);}
        nodes.reserve(2*n);
        build(0,n);
    }
    int build(int begin,int end) {
        int id=static_cast<int>(nodes.size());
        nodes.emplace_back();
        Node& node=nodes[id];
        node.begin=begin;node.end=end;
        node.box=posed[order[begin]];
        for(int j=begin+1;j<end;++j)node.box=Box::merge(node.box,posed[order[j]]);
        if(end-begin>8){
            Vec span=node.box.hi-node.box.lo;
            int axis=span.y>span.x?1:0;
            if(span.z>span.axis(axis))axis=2;
            int mid=(begin+end)/2;
            std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+end,[&](int a,int b){
                return posed[a].lo.axis(axis)+posed[a].hi.axis(axis)<posed[b].lo.axis(axis)+posed[b].hi.axis(axis);});
            node.left=build(begin,mid);node.right=build(mid,end);
        }
        return id;
    }
};
}
extern "C" __declspec(dllexport) int surface_crossings(const Vec* rest,const Vec* xyz,
    const Face* faces,int n,double minGap,int* pairs,int capacity) {
    Surface surface(rest,xyz,faces,n);
    int found=0;
    for(int i=0;i<n;++i){
        int stack[64]={0},top=1;
        while(top){
            const auto& node=surface.nodes[stack[--top]];
            if(!surface.posed[i].overlaps(node.box))continue;
            if(node.left>=0){stack[top++]=node.left;stack[top++]=node.right;continue;}
            for(int p=node.begin;p<node.end;++p){
                int j=surface.order[p];
                if(j<=i||!surface.posed[i].overlaps(surface.posed[j])||
                    surface.bind[i].gap2(surface.bind[j])<=minGap*minGap)continue;
                if(crosses(xyz,faces[i],faces[j])){
                    if(found<capacity){pairs[2*found]=i;pairs[2*found+1]=j;}
                    ++found;
                }
            }
        }
    }
    return found;
}
