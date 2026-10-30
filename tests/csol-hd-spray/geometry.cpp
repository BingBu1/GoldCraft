// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise production BSP traversal/clipping against actual 92/108-byte SDK layouts.
#include "../../native/csol/client/hd_spray_gl.cpp"
#include <iostream>
namespace csol::client {
metahook_api_t table{};metahook_api_t* api=&table;
cl_enginefunc_t engine{};int* hostTextureCache{};
bool EnsureGraphics(){return false;}
GLuint UploadImage(const Image&){return 0;}
}
using namespace csol::client;
unsigned checks{};int engineType{};
void require(bool b){++checks;if(!b)throw std::runtime_error("BSP spray geometry check "+std::to_string(checks));}
void Log(const char*,...){}
float Area(const Geometry& g){float area=0;for(size_t i=0;i<g.vertices.size();i+=3){auto a=Sub(g.vertices[i+1].position,g.vertices[i].position),b=Sub(g.vertices[i+2].position,g.vertices[i].position);const auto c=Cross(a,b);area+=std::sqrt(Dot(c,c))*.5f;}return area;}
int main(){try{
    table.GetEngineType=[](){return engineType;};engine.Con_DPrintf=Log;
    for(int type:{ENGINE_GOLDSRC,ENGINE_GOLDSRC_HL25}){
        engineType=type;const size_t stride=type==ENGINE_GOLDSRC?sizeof(msurface_t):sizeof(msurface_hl25_t);
        std::vector<std::byte> memory(stride*2);mplane_t plane{};plane.normal[0]=1;
        model_t map{};map.numsurfaces=2;map.surfaces=reinterpret_cast<msurface_t*>(memory.data());
        std::array<mvertex_t,8> points{};std::array<medge_t,8> edges{};std::array<int,8> indices{};
        const std::array<std::array<float,2>,8> yz{{{-32,-32},{0,-32},{0,32},{-32,32},{0,-32},{32,-32},{32,32},{0,32}}};
        for(int i=0;i<8;++i){points[i].position[1]=yz[i][0];points[i].position[2]=yz[i][1];edges[i].v[0]=i;edges[i].v[1]=i/4*4+(i+1)%4;indices[i]=i;}
        map.vertexes=points.data();map.numvertexes=8;map.edges=edges.data();map.numedges=8;map.surfedges=indices.data();map.numsurfedges=8;
        for(int i=0;i<2;++i){auto* s=reinterpret_cast<msurface_t*>(memory.data()+stride*i);s->plane=&plane;s->numedges=4;s->firstedge=i*4;}
        SprayPlacement p{1,1,0,1,{0,0,0},{1,0,0},{48,48}};csol::Image image{48,48,{}};
        Geometry g{p,{}};Build(g,&map);require(g.built);require(std::abs(Area(g)-2304)<.01f);
        for(auto v:g.vertices){require(std::abs(v.position[0]-.04f)<.001f);require(v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1);}
        image.Height=p.original.height=24;g={p,{}};Build(g,&map);require(std::abs(Area(g)-1152)<.01f);
        image.Height=p.original.height=48;p.position[1]=31;g={p,{}};Build(g,&map);require(std::abs(Area(g)-1200)<.01f);
        p.position[1]=0;p.position[0]=10;g={p,{}};Build(g,&map);require(g.vertices.empty());
        p.position[0]=0;map.surfaces->flags=DrawSky;g={p,{}};Build(g,&map);require(std::abs(Area(g)-1152)<.01f);
        map.surfaces->flags=0;map.surfaces->firstedge=100;g={p,{}};Build(g,&map);require(std::abs(Area(g)-1152)<.01f);
        map.surfaces->firstedge=0;for(auto& point:points){point.position[1]*=64;point.position[2]*=64;}
        p.original={128,64};image={128,64,{}};g={p,{}};Build(g,&map);require(std::abs(Area(g)-128*64)<.01f);
        p.original={256,128};image={256,128,{}};g={p,{}};Build(g,&map);require(std::abs(Area(g)-256*128)<.01f);
        p.original={2048,1024};image={512,256,{}};g={p,{}};Build(g,&map);require(std::abs(Area(g)-2048*1024)<.01f);
    }
    auto animation=std::make_shared<const csol::spray::Animation>(csol::spray::Animation{{{csol::Image{1,1,{1,2,3,255}},100},{csol::Image{1,1,{4,5,6,255}},200}},0});
    SprayPlacement first{1,7,0,1,{0,0,0},{1,0,0},{1,1},10},second=first;second.id=2;second.started=11;
    AddSprayGeometry(first,animation);AddSprayGeometry(second,animation);
    require(geometries.size()==2&&geometries[0].frames==geometries[1].frames&&geometries[0].frames->animation==animation);
    RemoveSprayGeometry(1);require(geometries.size()==1&&geometries[0].frames->animation==animation);
    SprayVideoReset();require(geometries.empty());
    std::cout<<checks<<" actual SDK BSP clipping/layout/resource-sharing checks passed; GL/live display pending\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
