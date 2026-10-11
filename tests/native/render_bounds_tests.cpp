#include "goldcraft/render_bounds.hpp"
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>

using goldcraft::RenderBounds;
namespace {
unsigned checks{};
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
using Point=std::array<float,3>;
void add(RenderBounds& bounds,const Point& point){bounds.add(point[0],point[1],point[2]);}
void contains(const RenderBounds& bounds,const std::array<double,3>& point){
    check(bounds.valid(),"geometry must have usable bounds");
    for(unsigned i=0;i<3;++i)
        check(double(bounds.min[i])<=point[i]&&point[i]<=double(bounds.max[i]),"bounds exclude submitted geometry");
}
void basic(){
    RenderBounds bounds;
    check(!bounds.valid(),"empty geometry must not supply culling bounds");
    bounds.add(-32,64,-2048);
    for(unsigned i=0;i<3;++i)check(bounds.min[i]==bounds.max[i],"point geometry must remain degenerate");
    contains(bounds,{-32,64,-2048});
    bounds.add(96,-128,32);bounds.add(-64,32,-4096);
    const Point low{-64,-128,-4096},high{96,64,32};
    for(unsigned i=0;i<3;++i)check(bounds.min[i]==low[i]&&bounds.max[i]==high[i],"negative/reflected world axes lost");
    // Bounds use the received mesh, including models extending past their
    // nominal section and animation vertices distant from an actor origin.
    bounds.add(8192,-8192,0);
    contains(bounds,{8192,-8192,0});contains(bounds,{-64,-128,-4096});
    auto next=bounds;next.add(16384,1,1);
    check(bounds.max[0]==8192&&next.max[0]==16384,"replacement preparation changed published bounds");
    next={};check(!next.valid()&&bounds.valid(),"new empty mesh must not retain previous bounds");
    next.add(1,2,3);contains(next,{1,2,3});
    check(next.min[0]==1&&next.max[0]==1,"new revision must start with its own geometry");
}
void invalid(){
    const float infinity=std::numeric_limits<float>::infinity();
    const std::array<float,3> bad{infinity,-infinity,std::numeric_limits<float>::quiet_NaN()};
    for(float value:bad)for(unsigned axis=0;axis<3;++axis)for(bool prefix:{false,true}){
        RenderBounds bounds;if(prefix)bounds.add(-1,-2,-3);
        Point point{1,2,3};point[axis]=value;add(bounds,point);
        check(!bounds.valid(),"any nonfinite component must disable whole-batch culling");
        bounds.add(4,5,6);bounds.add(-4,-5,-6);
        check(!bounds.valid(),"later finite vertices must not revive invalid bounds");
        auto copied=bounds;copied.add(0,0,0);
        check(!copied.valid(),"copy must retain invalid-batch state");
        bounds={};bounds.add(1,1,1);
        check(bounds.valid(),"a newly constructed mesh must recover from prior invalid geometry");
    }
    // Wire f32 can be finite yet overflow when the receive path scales by 32.
    // Volatile forces the same runtime float operation as that conversion.
    volatile float source=std::numeric_limits<float>::max();
    const float transformed=source*32.0f;
    check(std::isfinite(source)&&!std::isfinite(transformed),"finite-to-nonfinite conversion precondition");
    RenderBounds bounds;bounds.add(0,0,0);bounds.add(transformed,0,0);bounds.add(1,1,1);
    check(!bounds.valid(),"conversion overflow must keep the whole mesh unculled");
}
void boundaries(){
    // No center, radius, subtraction or addition is needed to retain extrema,
    // so even the complete finite float range must not overflow the bounds.
    const float largest=std::numeric_limits<float>::max();
    RenderBounds bounds;bounds.add(-largest,-largest,-largest);bounds.add(largest,largest,largest);
    check(bounds.valid(),"finite endpoints must not be rejected by bounds arithmetic");
    for(unsigned i=0;i<3;++i)check(bounds.min[i]==-largest&&bounds.max[i]==largest,"extreme endpoints changed");
    const float below=std::nextafter(32.0f,0.0f),above=std::nextafter(32.0f,64.0f);
    bounds={};bounds.add(below,0,32);bounds.add(above,0,32);
    check(bounds.min[0]==below&&bounds.max[0]==above,"adjacent representable vertices must not be rounded inward");
    contains(bounds,{below,0,32});contains(bounds,{above,0,32});contains(bounds,{32,0,32});
    check(bounds.min[1]==0&&bounds.max[1]==0&&bounds.min[2]==32&&bounds.max[2]==32,"planar geometry must retain exact touching planes");
    bounds={};bounds.add(-0.0f,0.0f,-0.0f);bounds.add(0.0f,-0.0f,0.0f);
    contains(bounds,{0,0,0});
}
void triangles(){
    // Actual world-space vertices from distinct quads in one material batch;
    // the second pair includes a triangle encoded with a duplicate last vertex.
    constexpr std::array<Point,8> vertices{{
        {-64,32,-2048},{96,32,-1984},{96,160,-1920},{-64,160,-2048},
        {8192,-1024,256},{8256,-960,288},{8192,-896,320},{8192,-896,320}}};
    constexpr std::array<std::array<unsigned,3>,4> triangles{{{0,1,2},{0,2,3},{4,5,6},{4,6,7}}};
    RenderBounds bounds;for(const auto& point:vertices)add(bounds,point);
    for(const auto& point:vertices)contains(bounds,{point[0],point[1],point[2]});
    // Binary-exact barycentric weights exercise interiors, edges and every
    // vertex without introducing an epsilon that could hide inward bounds.
    for(const auto& triangle:triangles)for(unsigned u=0;u<=16;++u)for(unsigned v=0;v<=16-u;++v){
        std::array<double,3> point{};
        for(unsigned axis=0;axis<3;++axis)
            point[axis]=(double(vertices[triangle[0]][axis])*(16-u-v)+
                double(vertices[triangle[1]][axis])*u+double(vertices[triangle[2]][axis])*v)/16;
        contains(bounds,point);
    }
}
}
int main(){
    static_assert(std::is_trivially_copyable_v<RenderBounds>);
    static_assert(noexcept(RenderBounds{}.add(0,0,0))&&noexcept(RenderBounds{}.valid()));
    try{
        basic();invalid();boundaries();triangles();
        std::printf("{\"renderBoundsChecks\":%u,\"nonfiniteFailOpen\":true,\"geometryConservative\":true,\"foregroundFPSValidated\":false}\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"render bounds: %s\n",error.what());return 1;}
}
