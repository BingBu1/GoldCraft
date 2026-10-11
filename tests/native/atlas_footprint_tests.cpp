#include "goldcraft/atlas_footprint.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace goldcraft;
namespace {
unsigned checks{};
void check(bool condition){++checks;assert(condition);}
struct UV {float u,v;};
using Quad=std::array<UV,4>;
AtlasFootprint footprint(const Quad& quad,unsigned width,unsigned height){
    AtlasFootprint result;check(result.assign_quads(std::span<const UV>(quad),width,height));return result;
}
Quad point(float u,float v){return {{{u,v},{u,v},{u,v},{u,v}}};}
unsigned nearest(double coordinate,unsigned size){
    // Independent GL_NEAREST + CLAMP_TO_EDGE oracle, in texel coordinates.
    if(coordinate<=0)return 0;if(coordinate>=1)return size-1;
    return static_cast<unsigned>(std::floor(coordinate*size));
}
void sampled_triangles_are_never_missed(){
    const std::array<unsigned,10> dimensions{1,2,3,16,63,64,65,127,1024,4096};
    const std::array<Quad,8> quads{{
        {{{.08f,.07f},{.32f,.11f},{.23f,.51f},{.02f,.44f}}},
        {{{-.5f,-.5f},{.4f,-.1f},{1.5f,.7f},{-.2f,1.3f}}},
        {{{1.f,0.f},{0.f,1.f},{1.f,1.f},{0.f,0.f}}},
        point(-1000,1000),point(std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()),
        point(.5f,.5f),point(std::nextafter(.5f,0.f),std::nextafter(.5f,1.f)),point(0,1)
    }};
    const unsigned triangles[2][3]{{0,1,2},{0,2,3}};
    for(const auto width:dimensions)for(const auto height:dimensions)for(const auto& quad:quads){
        const auto used=footprint(quad,width,height);
        for(const auto& triangle:triangles)for(unsigned a=0;a<=8;++a)for(unsigned b=0;b<=8-a;++b){
            const double wa=double(a)/8,wb=double(b)/8,wc=1-wa-wb;
            const double u=wa*quad[triangle[0]].u+wb*quad[triangle[1]].u+wc*quad[triangle[2]].u;
            const double v=wa*quad[triangle[0]].v+wb*quad[triangle[1]].v+wc*quad[triangle[2]].v;
            AtlasFootprint changed;changed.add_texel(nearest(u,width),nearest(v,height),width,height);
            check(used.intersects(changed));
        }
    }
}
void separated_quads_and_edges(){
    std::array<UV,8> separated{};const auto left=point(.125f,.125f),right=point(.875f,.875f);
    std::copy(left.begin(),left.end(),separated.begin());std::copy(right.begin(),right.end(),separated.begin()+4);
    AtlasFootprint used;check(used.assign_quads(std::span<const UV>(separated),1024,1024));
    AtlasFootprint middle;middle.add_texel(512,512,1024,1024);check(!used.intersects(middle));
    AtlasFootprint a,b;a.add_texel(128,128,1024,1024);b.add_texel(896,896,1024,1024);
    check(used.intersects(a)&&used.intersects(b));
    // A texel at small/non-divisible dimensions occupies an interval of tiles,
    // not merely the tile holding its center. Probe within that interval using
    // narrow 4096-atlas footprints, independently of the small atlas margin.
    for(const unsigned size:{1u,2u,3u,16u,63u,65u,127u})for(unsigned pixel=0;pixel<size;++pixel){
        AtlasFootprint changed;changed.add_texel(pixel,0,size,1);
        for(const double fraction:{.01,.5,.99}){
            const auto probe=footprint(point(float((pixel+fraction)/size),.8f),4096,4096);
            check(probe.intersects(changed));
        }
        changed={};changed.add_texel(0,pixel,1,size);
        const auto probe=footprint(point(.8f,float((pixel+.99)/size)),4096,4096);check(probe.intersects(changed));
    }
    const auto outside=footprint(point(-1000,1000),1024,1024);
    AtlasFootprint edge;edge.add_texel(0,1023,1024,1024);check(outside.intersects(edge)&&!outside.intersects(middle));
    const auto boundary=footprint(point(16.f/1024,16.f/1024),1024,1024);
    for(unsigned x=15;x<=17;++x)for(unsigned y=15;y<=17;++y){AtlasFootprint changed;changed.add_texel(x,y,1024,1024);check(boundary.intersects(changed));}
}
void exact_alpha_changes_and_overlap(){
    AtlasPixelTiles pixels;check(pixels.reset(128,65));
    std::vector<std::uint8_t> alpha(128*65,255);const auto original=alpha;
    std::array<std::uint8_t,8> rgba{0,0,0,255,1,2,3,255};
    std::array<AtlasPatch,1> patch{{{95,48,2,1,rgba}}};AtlasFootprint changed;
    check(pixels.changes(alpha,128,65,patch,changed)&&changed.empty());
    rgba[7]=0;check(pixels.changes(alpha,128,65,patch,changed)&&!changed.empty());
    check(changed.intersects(footprint(point(96.5f/128,48.5f/65),4096,4096)));
    check(!changed.intersects(footprint(point(.25f,.25f),4096,4096)));
    check(alpha==original); // Detection is transactional and does not commit.
    std::array<std::uint8_t,4> restore{9,8,7,255};
    std::array<AtlasPatch,2> overlapping{{patch[0],{96,48,1,1,restore}}};
    check(pixels.changes(alpha,128,65,overlapping,changed)&&!changed.empty()&&alpha==original);
    // Multiple far changed rectangles must not fill the gap between sprites.
    std::array<std::uint8_t,4> zero{};
    std::array<AtlasPatch,2> distant{{{0,0,1,1,zero},{127,64,1,1,zero}}};
    check(pixels.changes(alpha,128,65,distant,changed));
    check(!changed.intersects(footprint(point(.5f,.5f),4096,4096)));
    const auto near_start=footprint(point(0,0),4096,4096),near_end=footprint(point(1,1),4096,4096);
    check(changed.intersects(near_start)&&changed.intersects(near_end));
    // Dimensions are part of the accepted full-atlas state.
    check(!pixels.changes(alpha,65,128,distant,changed)&&changed.intersects(near_start)&&changed.intersects(near_end));
    pixels.clear();check(!pixels.changes(alpha,128,65,distant,changed));
    check(pixels.reset(2,1));std::array<std::uint8_t,2> small{255,255};
    std::array<AtlasPatch,1> small_patch{{{0,0,1,1,zero}}};
    check(pixels.changes(small,2,1,small_patch,changed));
    check(changed.intersects(footprint(point(.49f,.9f),4096,4096)));
    check(!changed.intersects(footprint(point(.9f,.9f),4096,4096)));
}
void unknown_is_conservative(){
    const auto distant=footprint(point(.9f,.9f),1024,1024);AtlasFootprint used;
    auto quad=point(.1f,.1f);
    for(const float value:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        quad[2].u=value;check(!used.assign_quads(std::span<const UV>(quad),1024,1024)&&used.intersects(distant));
    }
    quad=point(.1f,.1f);
    check(!used.assign_quads(std::span<const UV>(quad).first(3),1024,1024)&&used.intersects(distant));
    for(const auto dimensions:std::array<std::array<unsigned,2>,4>{{{0,1},{1,0},{4097,1},{1,4097}}})
        check(!used.assign_quads(std::span<const UV>(quad),dimensions[0],dimensions[1])&&used.intersects(distant));
    check(used.assign_quads(std::span<const UV>(quad),1,1)&&used.intersects(distant));
    check(used.assign_quads(std::span<const UV>(quad),1024,1024)&&!used.intersects(distant)); // Rebuild replaces old bits.
    AtlasPixelTiles pixels;check(pixels.reset(4,4));std::array<std::uint8_t,16> alpha{};
    std::array<std::uint8_t,3> short_rgba{};std::array<AtlasPatch,1> malformed{{{0,0,1,1,short_rgba}}};
    check(!pixels.changes(alpha,4,4,malformed,used)&&used.intersects(distant));
    check(!pixels.reset(0,4));
}
}
int main(){
    sampled_triangles_are_never_missed();separated_quads_and_edges();exact_alpha_changes_and_overlap();unknown_is_conservative();
    std::printf("Atlas footprint / independent nearest-sampling oracle: %u checks passed\n",checks);
}
