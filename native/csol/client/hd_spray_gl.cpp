// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "hd_spray.hpp"
#include "gl_state.hpp"
#include "assets.hpp"
#include <IMetaRendererPostScene.h>
#include <cmath>

namespace csol::client {
namespace {
using V=std::array<float,3>;
// Matched GoldSrc public/rehlds/model.h surface flags. The client SDK omits these.
constexpr int PlaneBack=2,DrawSky=4,DrawTurbulent=0x10;
V Sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
float Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V Cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V Unit(V a){float length=std::sqrt(Dot(a,a));for(auto& f:a)f/=length;return a;}
struct Vertex {V position;float u,v;};
HGLRC context{};
struct Frames {
    std::shared_ptr<const spray::Animation> animation;std::vector<GLuint> textures;
    ~Frames(){if(context&&context==wglGetCurrentContext()&&!textures.empty())glDeleteTextures(GLsizei(textures.size()),textures.data());}
};
struct Geometry {
    SprayPlacement placement;std::shared_ptr<Frames> frames;std::vector<Vertex> vertices;
    GLuint vao{},vbo{};bool built{};
};
std::vector<Geometry> geometries;
GLuint program{};GLint world{},projection{},model{},linearSpace{};
IMetaRendererPostScene* renderer{};

std::vector<Vertex> Clip(std::vector<Vertex> poly,int axis,float bound,bool greater){
    std::vector<Vertex> out;if(poly.empty())return out;
    auto coord=[&](const Vertex& v){return axis?v.v:v.u;};
    auto prev=poly.back();float d0=(coord(prev)-bound)*(greater?1:-1);
    for(auto cur:poly){float d1=(coord(cur)-bound)*(greater?1:-1);
        if((d0>=0)!=(d1>=0)){const float t=d0/(d0-d1);Vertex cut{};
            for(int i=0;i<3;++i)cut.position[i]=prev.position[i]+t*(cur.position[i]-prev.position[i]);
            cut.u=prev.u+t*(cur.u-prev.u);cut.v=prev.v+t*(cur.v-prev.v);
            if(axis)cut.v=bound;else cut.u=bound;out.push_back(cut);}
        if(d1>=0)out.push_back(cur);prev=cur;d0=d1;
    }return out;
}
void Build(Geometry& g,model_t* map){
    const auto& p=g.placement;V n=p.normal;
    const V right=Unit(Cross(std::abs(n[2])>.99f?V{0,1,0}:V{0,0,1},n)),up=Cross(n,right);
    // One original image pixel occupies one GoldSrc world unit. Texture
    // downsampling affects bandwidth/quality, never the wall footprint.
    const float width=float(p.original.width),height=float(p.original.height);
    const size_t stride=api->GetEngineType()==ENGINE_GOLDSRC_HL25?sizeof(msurface_hl25_t):sizeof(msurface_t);
    const int first=p.entity?map->firstmodelsurface:0,count=p.entity?map->nummodelsurfaces:map->numsurfaces;
    if(first<0||count<0||first>map->numsurfaces||count>map->numsurfaces-first)return;
    for(int i=first;i<first+count;++i){
        const auto* s=reinterpret_cast<const msurface_t*>(reinterpret_cast<const char*>(map->surfaces)+size_t(i)*stride);
        if(!s->plane||(s->flags&(DrawSky|DrawTurbulent))||s->numedges<3||s->numedges>128||s->firstedge<0||s->firstedge>map->numsurfedges-s->numedges)continue;
        V normal{s->plane->normal[0],s->plane->normal[1],s->plane->normal[2]};
        if(s->flags&PlaneBack)for(auto& f:normal)f=-f;
        if(Dot(normal,n)<.99f||std::abs(Dot(p.position,{s->plane->normal[0],s->plane->normal[1],s->plane->normal[2]})-s->plane->dist)>.75f)continue;
        std::vector<Vertex> polygon;polygon.reserve(s->numedges);bool valid=true;
        for(int j=0;j<s->numedges;++j){const int edge=map->surfedges[s->firstedge+j];
            if(edge==INT_MIN||std::abs(edge)>=map->numedges){valid=false;break;}
            const auto v=map->edges[std::abs(edge)].v[edge>=0?0:1];if(v>=map->numvertexes){valid=false;break;}
            const auto* point=map->vertexes[v].position;V pos{point[0],point[1],point[2]};const auto delta=Sub(pos,p.position);
            for(int k=0;k<3;++k)pos[k]+=n[k]*.04f;
            polygon.push_back({pos,Dot(delta,right)/width+.5f,.5f-Dot(delta,up)/height});
        }
        if(!valid)continue;
        polygon=Clip(std::move(polygon),0,0,true);polygon=Clip(std::move(polygon),0,1,false);
        polygon=Clip(std::move(polygon),1,0,true);polygon=Clip(std::move(polygon),1,1,false);
        for(size_t j=2;j<polygon.size();++j){g.vertices.push_back(polygon[0]);g.vertices.push_back(polygon[j-1]);g.vertices.push_back(polygon[j]);}
    }
    g.built=true;
    engine.Con_DPrintf("HD spray geometry: %u triangles=%u\n",p.id,unsigned(g.vertices.size()/3));
}
void Delete(Geometry& g){if(context&&context==wglGetCurrentContext()){
    if(g.vbo)glDeleteBuffers(1,&g.vbo);if(g.vao)glDeleteVertexArrays(1,&g.vao);
}g.vbo=g.vao=0;}
GLuint Shader(GLenum type,const char* source){
    auto id=glCreateShader(type);glShaderSource(id,1,&source,nullptr);glCompileShader(id);GLint ok;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(id);throw std::runtime_error("HD spray shader failed");}return id;
}
void Render(const MetaRendererPostSceneFrame& frame) noexcept {
    if(geometries.empty()||!EnsureGraphics())return;
    try {
        if(context!=wglGetCurrentContext()){for(auto& g:geometries){g.vbo=g.vao=0;g.frames->textures.clear();}program=0;context=wglGetCurrentContext();}
        GLState state;
        if(!program){
            auto v=Shader(GL_VERTEX_SHADER,R"(#version 330 core
layout(location=0) in vec3 point;layout(location=1) in vec2 uvIn;uniform mat4 world,projection,model;out vec2 uv;
void main(){uv=uvIn;gl_Position=projection*world*model*vec4(point,1);})");
            auto f=Shader(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec2 uv;uniform sampler2D image;uniform bool linearSpace;out vec4 color;
void main(){color=texture(image,uv);if(color.a<.004)discard;
if(linearSpace)color.rgb=mix(color.rgb/12.92,pow((color.rgb+.055)/1.055,vec3(2.4)),step(vec3(.04045),color.rgb));})");
            program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);
            GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(program);program=0;throw std::runtime_error("HD spray link failed");}
            world=glGetUniformLocation(program,"world");projection=glGetUniformLocation(program,"projection");model=glGetUniformLocation(program,"model");
            linearSpace=glGetUniformLocation(program,"linearSpace");
        }
        glUseProgram(program);glUniformMatrix4fv(world,1,GL_FALSE,frame.world_matrix);glUniformMatrix4fv(projection,1,GL_FALSE,frame.projection_matrix);
        glUniform1i(linearSpace,(frame.flags&META_POST_SCENE_LINEAR)!=0);
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);
        for(auto& g:geometries){
            auto* entity=engine.GetEntityByIndex(g.placement.entity);
            if(!entity||!entity->model||entity->model->type!=mod_brush||(g.placement.entity&&entity->curstate.modelindex!=int(g.placement.model)))continue;
            if(!g.built||g.vertices.empty())continue;
            // Immutable frames are uploaded once and shared by all placements
            // of this asset. Playback only selects a texture; no frame uploads.
            auto& frames=*g.frames;
            while(frames.textures.size()<frames.animation->frames.size()){
                frames.textures.push_back(UploadImage(frames.animation->frames[frames.textures.size()].image));
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
            if(!g.vao){
                glGenVertexArrays(1,&g.vao);glBindVertexArray(g.vao);glGenBuffers(1,&g.vbo);glBindBuffer(GL_ARRAY_BUFFER,g.vbo);
                glBufferData(GL_ARRAY_BUFFER,g.vertices.size()*sizeof(Vertex),g.vertices.data(),GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
                glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(sizeof(V)));}
            float matrix[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            if(g.placement.entity){float f[3],r[3],u[3];engine.pfnAngleVectors(entity->angles,f,r,u);
                for(int i=0;i<3;++i){matrix[i]=f[i];matrix[4+i]=-r[i];matrix[8+i]=u[i];matrix[12+i]=entity->origin[i];}}
            const auto frameIndex=spray::FrameAt(*frames.animation,engine.GetClientTime()-g.placement.started);
            glUniformMatrix4fv(model,1,GL_FALSE,matrix);glBindTexture(GL_TEXTURE_2D,frames.textures[frameIndex]);glBindVertexArray(g.vao);glDrawArrays(GL_TRIANGLES,0,GLsizei(g.vertices.size()));
        }
    }catch(const std::exception& e){engine.Con_DPrintf("HD spray render: %s\n",e.what());}
}
class Callback final:public IMetaRendererPostSceneCallbacks {void DrawPostScene(const MetaRendererPostSceneFrame& frame)noexcept override{Render(frame);}} callback;
}
void AddSprayGeometry(const SprayPlacement& p,std::shared_ptr<const spray::Animation> animation){
    std::shared_ptr<Frames> frames;
    for(const auto& geometry:geometries)if(geometry.placement.asset==p.asset){frames=geometry.frames;break;}
    if(!frames){frames=std::make_shared<Frames>();frames->animation=std::move(animation);}
    geometries.push_back({p,std::move(frames)});
}
void PrepareSprayGeometry(){
    // Map traversal and clipping happen once per placement, outside rendering.
    for(auto& g:geometries)if(!g.built){auto* e=engine.GetEntityByIndex(g.placement.entity);
        if(e&&e->model&&e->model->type==mod_brush&&(!g.placement.entity||e->curstate.modelindex==int(g.placement.model)))Build(g,e->model);}
}
void RemoveSprayGeometry(unsigned id){for(auto& g:geometries)if(g.placement.id==id)Delete(g);std::erase_if(geometries,[&](auto& g){return g.placement.id==id;});}
void SprayVideoReset(){for(auto& g:geometries)Delete(g);geometries.clear();if(program&&context==wglGetCurrentContext())glDeleteProgram(program);program=0;context=nullptr;}
void DrawSprays(){
    if(renderer)return;auto module=api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");if(!module)module=api->GetPluginModuleHandleByBaseFileName("Renderer");
    const auto factory=module?Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(module)):nullptr;
    if(factory){renderer=static_cast<IMetaRendererPostScene*>(factory(METARENDERER_POST_SCENE_INTERFACE_VERSION,nullptr));if(renderer&&!renderer->RegisterPostSceneCallbacks(&callback))renderer=nullptr;}
}
void ShutdownSprayGraphics(){if(renderer)renderer->UnregisterPostSceneCallbacks(&callback);renderer=nullptr;}
}
