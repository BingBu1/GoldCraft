// SPDX-License-Identifier: GPL-3.0-or-later
#include "point_light_render.hpp"
#include "core_gl.hpp"
#include <metahook.h>
#include <com_model.h>
#include <GL/gl.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#pragma clang fp contract(off)
namespace guest::cof::client
{
namespace
{
using Vec = std::array<float, 3>;
float Dot(const Vec& a, const Vec& b) noexcept { return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2]; }
Vec Unit(Vec a) noexcept
{
    const float length = float(std::sqrt(double(Dot(a,a))));
    if (length != 0.0F) { const float inv = 1.0F/length; for (auto& c:a) c*=inv; }
    return a;
}
float Neg(float a) noexcept { return std::bit_cast<float>(std::bit_cast<std::uint32_t>(a)^0x80000000U); }
}
float PointLightColor(float input, PointLightGamma g) noexcept
{
    // The exponent=1 branch remains x87 through the caller's multiply by .5.
    // Preserve its extended precision and single final float store.
    if (g.exponent == 1.0F) {
        float result;const float contrast=g.contrast,brightness=g.brightness,half=.5F;
        __asm { fld contrast }
        __asm { fmul input }
        __asm { fadd brightness }
        __asm { fmul half }
        __asm { fstp result }
        return result;
    }
    const float corrected = float(std::pow(double(input),double(g.exponent))*double(g.contrast)+double(g.brightness));
    return corrected*.5F;
}
std::array<float,16> PointLightMatrix(const PointLightBasis& b, const Vec& origin) noexcept
{
    return {Neg(b.tangent[0]),Neg(b.bitangent[0]),Neg(b.normal[0]),0,
            Neg(b.tangent[1]),Neg(b.bitangent[1]),Neg(b.normal[1]),0,
            Neg(b.tangent[2]),Neg(b.bitangent[2]),Neg(b.normal[2]),0,
            Dot(b.tangent,origin),Dot(b.bitangent,origin),Dot(b.normal,origin),1};
}
std::array<float,4> PointLightPlane(unsigned axis, const PointLight& light) noexcept
{
    const float scale=1.0F/(light.radius+light.radius);
    std::array<float,4> plane{};
    if(axis<3) {plane[axis]=scale;plane[3]=float(.5-double(light.origin[axis]*scale));}
    return plane;
}
Vec PointLightBrushOrigin(const PointLightBrush& brush,const Vec& origin) noexcept
{
    const Vec offset{origin[0]-brush.origin[0],origin[1]-brush.origin[1],origin[2]-brush.origin[2]};
    if(!brush.rotated)return offset;
    return {Dot(brush.forward,offset),Neg(Dot(brush.right,offset)),Dot(brush.up,offset)};
}
void BuildPointLightAttenuation1D(std::span<std::uint8_t,128> out) noexcept
{
    for(unsigned x=0;x<128;++x) {
        const float u=float(double(float(x)-64.0F)*(1.0/64.0));
        const float a=(u*u)*255.0F;
        out[x]=std::uint8_t(x ? std::min(a,255.0F) : 255.0F);
    }
}
void BuildPointLightAttenuation2D(std::span<std::uint8_t,128*128> out) noexcept
{
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x) {
        const float u=float(double(float(y)-64.0F)*(1.0/64.0));
        const float v=float(double(float(x)-64.0F)*(1.0/64.0));
        const float a=((v*v)+(u*u))*255.0F;
        out[y*128+x]=std::uint8_t(x&&y?std::min(a,255.0F):255.0F);
    }
}
void BuildPointLightAttenuation3D(std::span<std::uint8_t,32*32*32*3> out) noexcept
{
    for(unsigned z=0;z<32;++z)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
        const float u=float(z)-16.0F,v=float(y)-16.0F,w=float(x)-16.0F;
        const float length=float(std::sqrt(double((w*w)+((v*v)+(u*u)))));
        const float d=std::min(16.0F,length);
        const float a=x&&y&&z&&x!=31&&y!=31&&z!=31?((d*d)*(1.0F/256.0F)-1.0F)*-255.0F:0.0F;
        const auto byte=std::uint8_t(a);
        const auto index=((z*32+y)*32+x)*3;
        out[index]=byte;out[index+1]=byte;out[index+2]=byte;
    }
}
void BuildPointLightCube(std::span<std::uint8_t,6*32*32*3> out) noexcept
{
    for(unsigned face=0;face<6;++face)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
        const float u=((float(x)+.5F)*(1.0F/32.0F))*2.0F-1.0F;
        const float v=((float(y)+.5F)*(1.0F/32.0F))*2.0F-1.0F;
        Vec direction{};
        switch(face) {
        case 0:direction={1,Neg(v),Neg(u)};break;
        case 1:direction={-1,Neg(v),u};break;
        case 2:direction={u,1,v};break;
        case 3:direction={u,-1,Neg(v)};break;
        case 4:direction={u,Neg(v),1};break;
        default:direction={Neg(u),Neg(v),-1};break;
        }
        direction=Unit(direction);
        for(unsigned c=0;c<3;++c)out[((face*32+y)*32+x)*3+c]=std::uint8_t(direction[c]*127.0F+128.0F);
    }
}
namespace
{
constexpr GLenum Texture0=0x84C0,Texture3D=0x806F,Cube=0x8513,ClampEdge=0x812F;
constexpr GLenum Active=0x84E0,ClientActive=0x84E1,ArrayBuffer=0x8892,ArrayBinding=0x8894,CurrentProgram=0x8B8D,VertexArrayBinding=0x85B5;
constexpr GLenum Combine=0x8570,CombineRgb=0x8571,CombineAlpha=0x8572,Source0Rgb=0x8580,Source1Rgb=0x8581,Source0Alpha=0x8588,Source1Alpha=0x8589,Operand0Alpha=0x8598,Operand1Alpha=0x8599,RgbScale=0x8573,AlphaScale=0x0D1C,Previous=0x8578,Primary=0x8577,Dot3=0x86AF;
template<class T> T Proc(const char* name) noexcept {return reinterpret_cast<T>(wglGetProcAddress(name));}
struct GLFunctions
{
    void (APIENTRY* active)(GLenum){};
    void (APIENTRY* clientActive)(GLenum){};
    void (APIENTRY* image3D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*){};
    void (APIENTRY* bindBuffer)(GLenum,GLuint){};
    void (APIENTRY* useProgram)(GLuint){};
    void (APIENTRY* bindVertexArray)(GLuint){};
    void (APIENTRY* combinerParameteri)(GLenum,GLint){};
    void (APIENTRY* combinerParameterfv)(GLenum,const GLfloat*){};
    void (APIENTRY* combinerInput)(GLenum,GLenum,GLenum,GLenum,GLenum,GLenum){};
    void (APIENTRY* combinerOutput)(GLenum,GLenum,GLenum,GLenum,GLenum,GLenum,GLenum,GLboolean,GLboolean,GLboolean){};
    void (APIENTRY* finalCombinerInput)(GLenum,GLenum,GLenum,GLenum){};
    void (APIENTRY* getCombinerInput)(GLenum,GLenum,GLenum,GLenum,GLint*){};
    void (APIENTRY* getCombinerOutput)(GLenum,GLenum,GLenum,GLint*){};
    void (APIENTRY* getFinalCombinerInput)(GLenum,GLenum,GLint*){};
};
// Compatibility attribute stacks preserve texture environments, enables, blend,
// alpha/depth/stencil and client arrays. Texture matrices/program/VAO/buffer are
// outside those stacks and are saved separately. FBO and viewport never change.
struct GLGuard
{
    GLFunctions& gl;
    GLint active{},client{},mode{},program{},buffer{},vao{},unpack{};
    explicit GLGuard(GLFunctions& f):gl(f) {
        glGetIntegerv(Active,&active);glGetIntegerv(ClientActive,&client);glGetIntegerv(GL_MATRIX_MODE,&mode);
        glGetIntegerv(CurrentProgram,&program);glGetIntegerv(ArrayBinding,&buffer);glGetIntegerv(0x88EF,&unpack);
        if(gl.bindVertexArray)glGetIntegerv(VertexArrayBinding,&vao);
        glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        for(unsigned i=0;i<4;++i){gl.active(Texture0+i);glMatrixMode(GL_TEXTURE);glPushMatrix();if(i==2)glLoadIdentity();}
        gl.useProgram(0);gl.bindBuffer(ArrayBuffer,0);gl.bindBuffer(0x88EC,0);
        glMatrixMode(GL_MODELVIEW);
    }
    ~GLGuard() {
        for(unsigned i=0;i<4;++i){gl.active(Texture0+i);glMatrixMode(GL_TEXTURE);glPopMatrix();}
        if(gl.bindVertexArray)gl.bindVertexArray(GLuint(vao));
        glPopClientAttrib();glPopAttrib();gl.bindBuffer(ArrayBuffer,GLuint(buffer));gl.bindBuffer(0x88EC,GLuint(unpack));gl.useProgram(GLuint(program));
        gl.active(GLenum(active));gl.clientActive(GLenum(client));glMatrixMode(GLenum(mode));
    }
};
struct NvGuard
{
    GLFunctions& gl;GLint count{};std::array<GLfloat,4> color{};
    std::array<std::array<GLint,3>,4> inputs{};std::array<GLint,8> output{};
    std::array<std::array<GLint,3>,5> finalInputs{};
    static constexpr std::array<GLenum,8> outputNames{0x854A,0x854B,0x854C,0x8548,0x8549,0x8545,0x8546,0x8547};
    static constexpr std::array<GLenum,5> finalVariables{0x8523,0x8524,0x8525,0x8526,0x8529};
    explicit NvGuard(GLFunctions& f):gl(f) {
        glGetIntegerv(0x854E,&count);glGetFloatv(0x852A,color.data());
        for(unsigned i=0;i<4;++i)for(unsigned p=0;p<3;++p)gl.getCombinerInput(0x8550,GL_RGB,0x8523+i,0x8542+p,&inputs[i][p]);
        for(unsigned p=0;p<8;++p)gl.getCombinerOutput(0x8550,GL_RGB,outputNames[p],&output[p]);
        for(unsigned i=0;i<5;++i)for(unsigned p=0;p<3;++p)gl.getFinalCombinerInput(finalVariables[i],0x8542+p,&finalInputs[i][p]);
    }
    ~NvGuard() {
        gl.combinerParameteri(0x854E,count);gl.combinerParameterfv(0x852A,color.data());
        for(unsigned i=0;i<4;++i)gl.combinerInput(0x8550,GL_RGB,0x8523+i,GLenum(inputs[i][0]),GLenum(inputs[i][1]),GLenum(inputs[i][2]));
        gl.combinerOutput(0x8550,GL_RGB,GLenum(output[0]),GLenum(output[1]),GLenum(output[2]),GLenum(output[3]),GLenum(output[4]),GLboolean(output[5]),GLboolean(output[6]),GLboolean(output[7]));
        for(unsigned i=0;i<5;++i)gl.finalCombinerInput(finalVariables[i],GLenum(finalInputs[i][0]),GLenum(finalInputs[i][1]),GLenum(finalInputs[i][2]));
    }
};
void Env(GLFunctions& gl,unsigned unit,int mode) noexcept
{
    gl.active(Texture0+unit);
    if(!mode){glDisable(GL_TEXTURE_2D);return;}
    glEnable(GL_TEXTURE_2D);
    if(mode==1||mode==4){glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,mode==1?GL_REPLACE:GL_MODULATE);return;}
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,Combine);
    glTexEnvi(GL_TEXTURE_ENV,CombineRgb,mode==7?Dot3:GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV,Source0Rgb,Previous);
    glTexEnvi(GL_TEXTURE_ENV,Source1Rgb,mode==3?Primary:GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV,RgbScale,1);
}
void Envs(GLFunctions& gl,int a,int b,int c,int d) noexcept {Env(gl,0,a);Env(gl,1,b);Env(gl,2,c);Env(gl,3,d);}
void Texgen(const PointLight& light,unsigned coordinate,unsigned axis) noexcept
{
    const auto plane=PointLightPlane(axis,light);
    glTexGeni(GL_S+coordinate,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
    glTexGenfv(GL_S+coordinate,GL_OBJECT_PLANE,plane.data());glEnable(GL_TEXTURE_GEN_S+coordinate);
}
void NoTexgen(unsigned count) noexcept {for(unsigned i=0;i<count;++i)glDisable(GL_TEXTURE_GEN_S+i);}
void Parameters(GLenum target,GLint minFilter,bool volume=false) noexcept
{
    glTexParameteri(target,GL_TEXTURE_MIN_FILTER,minFilter);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(target,GL_TEXTURE_WRAP_S,ClampEdge);
    if(target!=GL_TEXTURE_1D)glTexParameteri(target,GL_TEXTURE_WRAP_T,ClampEdge);
    if(volume)glTexParameteri(target,0x8072,ClampEdge);
}
bool MainBrush(const cl_entity_t& entity) noexcept
{
    for(unsigned i=0;i<3;++i)if(entity.origin[i]!=0.0F||entity.angles[i]!=0.0F)return false;
    const int fx=entity.curstate.renderfx;return fx!=70&&fx!=137&&fx!=184;
}
bool PortalBrush(int fx) noexcept {return fx==70||fx==137||fx==184;}
bool MainBrush(const PointLightBrush& brush) noexcept
{
    return !brush.rotated&&brush.origin[0]==0.0F&&brush.origin[1]==0.0F&&brush.origin[2]==0.0F&&
        !PortalBrush(brush.renderfx);
}
bool Extension(const char* list,const char* name) noexcept
{
    if(!list)return false;const auto length=std::strlen(name);
    for(const char* at=list;(at=std::strstr(at,name));at+=length)
        if((at==list||at[-1]==' ')&&(at[length]==' '||at[length]=='\0'))return true;
    return false;
}
struct Surface {const msurface_t* host{};PointLightBasis basis{};GLuint normal{};int count{};};
struct DrawSurface {std::uint32_t index{};};
}
struct PointLightRenderer::Impl
{
    model_t* world{};std::size_t stride{};std::vector<Surface> surfaces;std::vector<std::array<float,7>> vertices;
    std::vector<DrawSurface> draws;std::size_t drawCount{};
    HGLRC context{};DWORD thread{};GLFunctions gl{};std::array<GLuint,5> textures{};GLint units{},alphaBits{};bool nv{},ready{};
    core_gl::Api coreApi{};bool core{};GLuint program{},vao{},vbo{};
    GLint worldUniform{},projectionUniform{},modelUniform{},basisUniform{},planeUniform{},colorUniform{},passUniform{};
    bool InitializeCore() noexcept;
    void CoreBatch(const PointLight&,const PointLightFrame&,const std::array<float,16>&,bool) noexcept;
    PointLightDrawResult DrawCore(const PointLightFrame&,const PointLightState&) noexcept;
    const msurface_t* SurfaceAt(std::size_t i) const noexcept {return reinterpret_cast<const msurface_t*>(reinterpret_cast<const char*>(world->surfaces)+i*stride);}
    bool Owner() const noexcept {return ready&&context==wglGetCurrentContext()&&thread==GetCurrentThreadId();}
    bool Brushes(const PointLightFrame& f) const noexcept {
        if(!f.drawnBrushes.empty()) {
            if(!f.brushLifecycleEstablished||!f.visibleBrushes.empty())return false;
            for(const auto& brush:f.drawnBrushes) {
                if(!brush.model||brush.model->type!=mod_brush||brush.model->surfaces!=world->surfaces||
                   PortalBrush(brush.renderfx))return false;
                for(const float value:brush.modelToWorld)if(!std::isfinite(value))return false;
            }
            return true;
        }
        if(f.visibleBrushes.empty())return true;if(!f.brushLifecycleEstablished)return false;
        for(const auto* entity:f.visibleBrushes)if(entity&&entity->model&&entity->model->type==mod_brush&&!MainBrush(*entity))return false;return true;
    }
    bool FixedArrays() const noexcept {if(core)return true;GLint vao=0;if(gl.bindVertexArray)glGetIntegerv(VertexArrayBinding,&vao);return vao==0;}
    bool Scene(const PointLightFrame& f) const noexcept {return f.stage==PointLightStage::OpaqueBeforeTransparency&&f.space==PointLightSpace::OriginalGamma&&f.matchingOpaqueDepth&&f.visibilityEstablished;}
    void Arrays(unsigned unit,int mode) noexcept {
        gl.clientActive(Texture0+unit);
        if(!mode){glDisableClientState(GL_TEXTURE_COORD_ARRAY);return;}
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        const auto* start=reinterpret_cast<const char*>(vertices.data());
        glTexCoordPointer(mode==3?3:2,GL_FLOAT,28,start+(mode==1?12:0));
    }
    void DrawOne(std::size_t index,const PointLight& light) noexcept {
        const auto& s=surfaces[index];gl.active(Texture0+1);glBindTexture(GL_TEXTURE_2D,s.normal?s.normal:textures[3]);
        const auto matrix=PointLightMatrix(s.basis,light.origin);gl.active(Texture0);glMatrixMode(GL_TEXTURE);glLoadMatrixf(matrix.data());
        glDrawArrays(GL_POLYGON,s.basis.first,s.count);
    }
    bool Add(std::size_t index) noexcept {if(drawCount==draws.size())return false;draws[drawCount++]={std::uint32_t(index)};return true;}
    bool Outside(const float* mins,const float* maxs,const PointLight& l) const noexcept {
        for(unsigned i=0;i<3;++i)if(maxs[i]<l.origin[i]-l.radius||mins[i]>l.origin[i]+l.radius)return true;return false;
    }
    static bool Mask(std::span<const std::uint64_t> words,std::size_t index) noexcept {return index/64<words.size() && (words[index/64]&(std::uint64_t(1)<<(index&63)))!=0;}
    bool SurfaceVisible(const PointLightFrame& frame,std::size_t index) const noexcept {return frame.visibleSurfaceWords.empty()?surfaces[index].host->visframe==frame.surfaceVisframe:Mask(frame.visibleSurfaceWords,index);}
    bool WorldNode(const mnode_t* node,const PointLightFrame& frame,const PointLight& light) noexcept {
        while(node&&node->contents>=0) {
            const auto index=std::size_t(node-world->nodes);
            if((frame.visibleNodeWords.empty()?node->visframe!=frame.nodeVisframe:!Mask(frame.visibleNodeWords,index))||Outside(node->minmaxs,node->minmaxs+3,light))return true;
            const auto* p=node->plane;const Vec n{p->normal[0],p->normal[1],p->normal[2]};
            const float delta=(p->type<3?light.origin[p->type]:Dot(n,light.origin))-p->dist;
            const double distance=double(delta);int side=distance<0.0?1:0;
            if(!WorldNode(node->children[side],frame,light))return false;
            if(distance<-.01)side=2;else if(distance>.01)side=0;
            const std::size_t first=node->firstsurface,count=node->numsurfaces;
            if(first>surfaces.size()||count>surfaces.size()-first)return false;
            for(std::size_t i=first;i<first+count;++i) {
                const auto* s=surfaces[i].host;
                if(SurfaceVisible(frame,i)&&surfaces[i].count&&
                   ((s->flags&0x80)||bool(s->flags&2)==(distance<0.0)))if(!Add(i))return false;
            }
            node=node->children[side==0?1:0];
        }
        return true;
    }
    bool Brush(const model_t* model,const PointLightFrame& frame,const PointLight& light,
               const Vec* localView=nullptr) noexcept {
        if(!model||model->type!=mod_brush||model->surfaces!=world->surfaces)return false;
        const int first=model->firstmodelsurface,count=model->nummodelsurfaces;
        if(first<0||count<0||std::size_t(first)>surfaces.size()||std::size_t(count)>surfaces.size()-std::size_t(first))return false;
        for(int i=first;i<first+count;++i) {
            const auto& surface=surfaces[std::size_t(i)];const auto* s=surface.host;
            if(!surface.count||(!localView&&!SurfaceVisible(frame,std::size_t(i))))continue;
            const auto* p=s->plane;const Vec normal{p->normal[0],p->normal[1],p->normal[2]};
            if(localView) {
                const double viewDistance=double(Dot(normal,*localView)-p->dist);
                if((s->flags&2)?!(viewDistance<-.01):!(viewDistance>.01))continue;
            }
            const double distance=double(Dot(normal,light.origin)-p->dist);
            if((s->flags&2)?distance<-.01:distance>.01)if(!Add(std::size_t(i)))return false;
        }
        return true;
    }
    void Replay() noexcept {for(std::size_t i=drawCount;i>0;--i){const auto& s=surfaces[draws[i-1].index];glDrawArrays(GL_POLYGON,s.basis.first,s.count);}}
    void Finish(const PointLight& light,bool fallback,const Vec& color,bool disableNv) noexcept;
    void DrawBatch(const PointLight&,const PointLightFrame&,bool fallback) noexcept;
};
PointLightRenderer::PointLightRenderer():impl_(std::make_unique<Impl>()){}
PointLightRenderer::~PointLightRenderer(){ReleaseGL();}
bool PointLightRenderer::PrepareMap(model_s& model,bool hl25,std::size_t maximumSurfaceDraws)
{
    static_assert(sizeof(msurface_t)==92&&sizeof(msurface_hl25_t)==108);
    static_assert(offsetof(model_t,numsurfaces)==176&&offsetof(model_t,surfaces)==180);
    if(model.type!=mod_brush||model.numsurfaces<0||!model.surfaces||!maximumSurfaceDraws)return false;
    auto& r=*impl_;r.world=&model;r.stride=hl25?sizeof(msurface_hl25_t):sizeof(msurface_t);
    r.surfaces.clear();r.vertices.clear();r.surfaces.resize(std::size_t(model.numsurfaces));
    std::size_t total=0;
    for(std::size_t i=0;i<r.surfaces.size();++i){const auto* s=r.SurfaceAt(i);if(!(s->flags&0x14)&&s->polys&&s->polys->numverts>0)total+=std::size_t(s->polys->numverts);}
    if(total>std::size_t(std::numeric_limits<int>::max())){ClearMap();return false;}
    r.vertices.reserve(total);r.draws.resize(maximumSurfaceDraws);
    for(std::size_t i=0;i<r.surfaces.size();++i) {
        auto& dst=r.surfaces[i];const auto* s=dst.host=r.SurfaceAt(i);
        if((s->flags&0x14)||!s->polys||s->polys->numverts<=0)continue;
        if(!s->plane||!s->texinfo){ClearMap();return false;}
        dst.basis.first=int(r.vertices.size());dst.count=s->polys->numverts;
        for(unsigned c=0;c<3;++c){dst.basis.normal[c]=(s->flags&2)?Neg(s->plane->normal[c]):s->plane->normal[c];dst.basis.tangent[c]=s->texinfo->vecs[0][c];dst.basis.bitangent[c]=s->texinfo->vecs[1][c];}
        dst.basis.tangent=Unit(dst.basis.tangent);dst.basis.bitangent=Unit(dst.basis.bitangent);
        // Original private material sentinel. No interpretation of other texture
        // fields or alias layout is attempted for ordinary engine textures.
        const auto* texture=s->texinfo->texture;
        if(texture&&texture->anim_min==-666&&texture->anim_next)std::memcpy(&dst.normal,reinterpret_cast<const char*>(texture->anim_next)+16,sizeof(dst.normal));
        for(int j=0;j<dst.count;++j){std::array<float,7> v;std::memcpy(v.data(),s->polys->verts[j],sizeof(v));r.vertices.push_back(v);}
    }
    if(r.core&&r.Owner()){
        GLint previous{};glGetIntegerv(ArrayBinding,&previous);r.coreApi.bindBuffer(ArrayBuffer,r.vbo);
        r.coreApi.bufferData(ArrayBuffer,std::ptrdiff_t(r.vertices.size()*sizeof(r.vertices[0])),r.vertices.data(),core_gl::StaticDraw);
        r.coreApi.bindBuffer(ArrayBuffer,GLuint(previous));
    }
    return true;
}
void PointLightRenderer::ClearMap() noexcept {auto& r=*impl_;r.world=nullptr;r.surfaces.clear();r.vertices.clear();r.drawCount=0;}
bool PointLightRenderer::Impl::InitializeCore() noexcept
{
    if(!coreApi.Initialize()||vertices.empty())return false;
    const auto image3D=core_gl::Proc<decltype(gl.image3D)>("glTexImage3D");if(!image3D)return false;
    constexpr auto vertex=R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec2 texcoord;
uniform mat4 world;uniform mat4 projection;uniform mat4 model;uniform mat4 basis;uniform vec4 planes[3];
out vec2 uv;out vec3 direction;out vec3 attenuation;
void main(){vec4 p=vec4(position,1.0);uv=texcoord;direction=(basis*p).xyz;attenuation=vec3(dot(planes[0],p),dot(planes[1],p),dot(planes[2],p));gl_Position=projection*(world*(model*p));}
)";
    constexpr auto fragment=R"(#version 330 core
uniform samplerCube directions;uniform sampler2D normals;uniform sampler3D volume;
uniform sampler1D axis;uniform sampler2D plane;uniform vec3 lightColor;uniform int pass;
in vec2 uv;in vec3 direction;in vec3 attenuation;out vec4 color;
void main(){
 if(pass==2){float alpha=clamp(texture(axis,attenuation.z).r+texture(plane,attenuation.xy).r,0.0,1.0);color=vec4(texture(plane,attenuation.xy).rrr,alpha);return;}
 if(pass==3){color=vec4(lightColor,1.0);return;}
 float diffuse=clamp(4.0*dot(texture(directions,direction).rgb-vec3(0.5),texture(normals,uv).rgb-vec3(0.5)),0.0,1.0);
 color=pass==1?vec4(diffuse):vec4(clamp(vec3(diffuse)*texture(volume,attenuation).rgb,0.0,1.0)*lightColor,diffuse);
}
)";
    core_gl::State guard(coreApi);program=coreApi.Program(vertex,fragment);if(!program)return false;
    worldUniform=coreApi.uniform(program,"world");projectionUniform=coreApi.uniform(program,"projection");modelUniform=coreApi.uniform(program,"model");basisUniform=coreApi.uniform(program,"basis");planeUniform=coreApi.uniform(program,"planes");colorUniform=coreApi.uniform(program,"lightColor");passUniform=coreApi.uniform(program,"pass");
    coreApi.use(program);for(const auto& binding:std::array<std::pair<const char*,int>,5>{{{"directions",0},{"normals",1},{"volume",2},{"axis",3},{"plane",1}}})coreApi.integer(coreApi.uniform(program,binding.first),binding.second);
    coreApi.genArrays(1,&vao);coreApi.genBuffers(1,&vbo);glGenTextures(GLsizei(textures.size()),textures.data());
    const auto cleanup=[&]{if(vbo)coreApi.deleteBuffers(1,&vbo);if(vao)coreApi.deleteArrays(1,&vao);coreApi.deleteProgram(program);glDeleteTextures(GLsizei(textures.size()),textures.data());program=vao=vbo=0;textures={};};
    if(!vao||!vbo||std::ranges::any_of(textures,[](GLuint id){return !id;})){cleanup();return false;}
    coreApi.bindArray(vao);coreApi.bindBuffer(core_gl::ArrayBuffer,vbo);coreApi.bufferData(core_gl::ArrayBuffer,std::ptrdiff_t(vertices.size()*sizeof(vertices[0])),vertices.data(),core_gl::StaticDraw);
    coreApi.enableAttribute(0);coreApi.attribute(0,3,GL_FLOAT,GL_FALSE,28,nullptr);coreApi.enableAttribute(1);coreApi.attribute(1,2,GL_FLOAT,GL_FALSE,28,reinterpret_cast<void*>(12));
    std::array<std::uint8_t,128> a1;BuildPointLightAttenuation1D(a1);glBindTexture(GL_TEXTURE_1D,textures[0]);glTexImage1D(GL_TEXTURE_1D,0,0x8229,128,0,0x1903,GL_UNSIGNED_BYTE,a1.data());Parameters(GL_TEXTURE_1D,GL_NEAREST);
    std::array<std::uint8_t,128*128> a2;BuildPointLightAttenuation2D(a2);glBindTexture(GL_TEXTURE_2D,textures[1]);glTexImage2D(GL_TEXTURE_2D,0,0x8229,128,128,0,0x1903,GL_UNSIGNED_BYTE,a2.data());Parameters(GL_TEXTURE_2D,GL_NEAREST);
    std::array<std::uint8_t,32*32*32*3> a3;BuildPointLightAttenuation3D(a3);glBindTexture(Texture3D,textures[2]);image3D(Texture3D,0,0x8051,32,32,32,0,GL_RGB,GL_UNSIGNED_BYTE,a3.data());Parameters(Texture3D,GL_LINEAR,true);
    std::array<std::uint8_t,16*16*3> normal;for(unsigned i=0;i<16*16;++i){normal[i*3]=127;normal[i*3+1]=127;normal[i*3+2]=255;}glBindTexture(GL_TEXTURE_2D,textures[3]);glTexImage2D(GL_TEXTURE_2D,0,0x8051,16,16,0,GL_RGB,GL_UNSIGNED_BYTE,normal.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    std::array<std::uint8_t,6*32*32*3> cube;BuildPointLightCube(cube);glBindTexture(Cube,textures[4]);for(unsigned face=0;face<6;++face)glTexImage2D(0x8515+face,0,0x8051,32,32,0,GL_RGB,GL_UNSIGNED_BYTE,cube.data()+face*32*32*3);Parameters(Cube,GL_LINEAR);
    if(glGetError()!=GL_NO_ERROR){cleanup();return false;}
    GLint framebuffer{},drawBuffer{};glGetIntegerv(0x8CA6,&framebuffer);glGetIntegerv(0x8825,&drawBuffer);
    const GLenum attachment=framebuffer?GLenum(drawBuffer):(drawBuffer==GL_BACK?0x0402:0x0400);
    coreApi.attachment(0x8CA9,attachment,0x8215,&alphaBits);
    if(glGetError()!=GL_NO_ERROR){cleanup();return false;}
    units=4;context=wglGetCurrentContext();thread=GetCurrentThreadId();ready=true;return true;
}
void PointLightRenderer::Impl::CoreBatch(const PointLight& light,const PointLightFrame& frame,const std::array<float,16>& model,bool fallback) noexcept
{
    const Vec color{PointLightColor(light.color[0],frame.gamma),PointLightColor(light.color[1],frame.gamma),PointLightColor(light.color[2],frame.gamma)};
    std::array<float,12> planes{};for(unsigned axis=0;axis<3;++axis){const auto p=PointLightPlane(axis,light);std::copy(p.begin(),p.end(),planes.begin()+axis*4);}
    coreApi.vector3(colorUniform,1,color.data());coreApi.vector4(planeUniform,3,planes.data());coreApi.matrix(modelUniform,1,GL_FALSE,model.data());
    coreApi.active(Texture0);glBindTexture(Cube,textures[4]);coreApi.active(Texture0+2);glBindTexture(Texture3D,textures[2]);
    coreApi.integer(passUniform,fallback?1:0);glEnable(GL_BLEND);glDepthMask(GL_FALSE);
    if(fallback){glBlendFunc(GL_ONE,GL_ZERO);glColorMask(0,0,0,1);}else glBlendFunc(GL_ONE,GL_ONE);
    for(std::size_t i=0;i<drawCount;++i){const auto& s=surfaces[draws[i].index];coreApi.active(Texture0+1);glBindTexture(GL_TEXTURE_2D,s.normal?s.normal:textures[3]);const auto basis=PointLightMatrix(s.basis,light.origin);coreApi.matrix(basisUniform,1,GL_FALSE,basis.data());glDrawArrays(GL_TRIANGLE_FAN,s.basis.first,s.count);}
    if(!fallback)return;
    coreApi.integer(passUniform,2);coreApi.active(Texture0+3);glBindTexture(GL_TEXTURE_1D,textures[0]);coreApi.active(Texture0+1);glBindTexture(GL_TEXTURE_2D,textures[1]);glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_ALPHA);
    for(std::size_t i=drawCount;i>0;--i){const auto& s=surfaces[draws[i-1].index];glDrawArrays(GL_TRIANGLE_FAN,s.basis.first,s.count);}
    coreApi.integer(passUniform,3);glBlendFunc(GL_DST_ALPHA,GL_ONE);glColorMask(1,1,1,0);
    for(std::size_t i=drawCount;i>0;--i){const auto& s=surfaces[draws[i-1].index];glDrawArrays(GL_TRIANGLE_FAN,s.basis.first,s.count);}
}
PointLightDrawResult PointLightRenderer::Impl::DrawCore(const PointLightFrame& frame,const PointLightState& state) noexcept
{
    if(!frame.authenticatedMatrices)return PointLightDrawResult::UnsupportedScene;
    bool any=false;for(const auto& light:state.lights)if(!(frame.time>light.die)&&light.radius!=0.0F){any=true;if(light.rendererFields[20]!=std::byte{})return PointLightDrawResult::UnsupportedLight;}
    if(!any)return PointLightDrawResult::NoLights;
    core_gl::State guard(coreApi);coreApi.use(program);coreApi.bindArray(vao);coreApi.equation(0x8006,0x8006);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_EQUAL);glStencilMask(0);
    coreApi.matrix(worldUniform,1,GL_FALSE,frame.worldMatrix.data());coreApi.matrix(projectionUniform,1,GL_FALSE,frame.projectionMatrix.data());
    const bool fallback=frame.forceTwoPass!=0.0F;
    for(const auto& light:state.lights){if(frame.time>light.die||light.radius==0.0F)continue;drawCount=0;
        if(!WorldNode(world->nodes,frame,light))return PointLightDrawResult::CapacityExceeded;
        if(frame.glBmodels!=0.0F)for(const auto* entity:frame.visibleBrushes){if(!entity||!entity->model||entity->model->type!=mod_brush||!MainBrush(*entity))continue;float mins[3],maxs[3];for(unsigned i=0;i<3;++i){mins[i]=entity->model->mins[i]+entity->origin[i];maxs[i]=entity->model->maxs[i]+entity->origin[i];}if(!Outside(mins,maxs,light)&&!Brush(entity->model,frame,light))return PointLightDrawResult::CapacityExceeded;}
        if(frame.glBmodels!=0.0F)for(const auto& brush:frame.drawnBrushes){if(!MainBrush(brush))continue;if(!Outside(brush.model->mins,brush.model->maxs,light)&&!Brush(brush.model,frame,light,&brush.localView))return PointLightDrawResult::CapacityExceeded;}
        CoreBatch(light,frame,core_gl::Identity,fallback);
    }
    if(frame.glBmodels!=0.0F)for(const auto& brush:frame.drawnBrushes){if(MainBrush(brush))continue;for(const auto& light:state.lights){if(frame.time>light.die||light.radius==0.0F)continue;float mins[3],maxs[3];for(unsigned i=0;i<3;++i){mins[i]=brush.origin[i]+(brush.rotated?-brush.model->radius:brush.model->mins[i]);maxs[i]=brush.origin[i]+(brush.rotated?brush.model->radius:brush.model->maxs[i]);}if(Outside(mins,maxs,light))continue;PointLight local=light;local.origin=PointLightBrushOrigin(brush,light.origin);drawCount=0;if(!Brush(brush.model,frame,local,&brush.localView))return PointLightDrawResult::CapacityExceeded;CoreBatch(local,frame,brush.modelToWorld,fallback);}}
    return PointLightDrawResult::Drawn;
}
bool PointLightRenderer::InitializeGL() noexcept
{
    auto& r=*impl_;if(r.Owner())return true;if(r.ready||!wglGetCurrentContext())return false;
    r.core=core_gl::IsCore();if(r.core)return r.InitializeCore();
    r.gl.active=Proc<decltype(r.gl.active)>("glActiveTexture");r.gl.clientActive=Proc<decltype(r.gl.clientActive)>("glClientActiveTexture");
    r.gl.image3D=Proc<decltype(r.gl.image3D)>("glTexImage3D");r.gl.bindBuffer=Proc<decltype(r.gl.bindBuffer)>("glBindBuffer");
    r.gl.useProgram=Proc<decltype(r.gl.useProgram)>("glUseProgram");r.gl.bindVertexArray=Proc<decltype(r.gl.bindVertexArray)>("glBindVertexArray");
    if(!r.gl.active||!r.gl.clientActive||!r.gl.bindBuffer||!r.gl.useProgram)return false;
    glGetIntegerv(0x84E2,&r.units);glGetIntegerv(GL_ALPHA_BITS,&r.alphaBits);if(r.units<4)return false;
    const auto* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if(!Extension(extensions,"GL_ARB_texture_cube_map")||!Extension(extensions,"GL_ARB_texture_env_dot3"))return false;
    r.nv=Extension(extensions,"GL_NV_register_combiners");
    r.gl.combinerParameteri=Proc<decltype(r.gl.combinerParameteri)>("glCombinerParameteriNV");r.gl.combinerParameterfv=Proc<decltype(r.gl.combinerParameterfv)>("glCombinerParameterfvNV");
    r.gl.combinerInput=Proc<decltype(r.gl.combinerInput)>("glCombinerInputNV");r.gl.combinerOutput=Proc<decltype(r.gl.combinerOutput)>("glCombinerOutputNV");r.gl.finalCombinerInput=Proc<decltype(r.gl.finalCombinerInput)>("glFinalCombinerInputNV");
    r.gl.getCombinerInput=Proc<decltype(r.gl.getCombinerInput)>("glGetCombinerInputParameterivNV");r.gl.getCombinerOutput=Proc<decltype(r.gl.getCombinerOutput)>("glGetCombinerOutputParameterivNV");r.gl.getFinalCombinerInput=Proc<decltype(r.gl.getFinalCombinerInput)>("glGetFinalCombinerInputParameterivNV");
    GLint combinerCount=0;if(r.nv)glGetIntegerv(0x854D,&combinerCount);
    r.nv=r.nv&&combinerCount>=2&&r.gl.combinerParameteri&&r.gl.combinerParameterfv&&r.gl.combinerInput&&r.gl.combinerOutput&&r.gl.finalCombinerInput&&r.gl.getCombinerInput&&r.gl.getCombinerOutput&&r.gl.getFinalCombinerInput;
    GLGuard guard(r.gl);glGenTextures(GLsizei(r.textures.size()),r.textures.data());
    if(std::ranges::any_of(r.textures,[](GLuint id){return id==0;})){glDeleteTextures(GLsizei(r.textures.size()),r.textures.data());r.textures={};return false;}
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);r.gl.active(Texture0);
    std::array<std::uint8_t,128> a1;BuildPointLightAttenuation1D(a1);glBindTexture(GL_TEXTURE_1D,r.textures[0]);glTexImage1D(GL_TEXTURE_1D,0,0x803C,128,0,GL_ALPHA,GL_UNSIGNED_BYTE,a1.data());Parameters(GL_TEXTURE_1D,GL_NEAREST);
    std::array<std::uint8_t,128*128> a2;BuildPointLightAttenuation2D(a2);glBindTexture(GL_TEXTURE_2D,r.textures[1]);glTexImage2D(GL_TEXTURE_2D,0,0x803C,128,128,0,GL_ALPHA,GL_UNSIGNED_BYTE,a2.data());Parameters(GL_TEXTURE_2D,GL_NEAREST);
    if(r.gl.image3D){std::array<std::uint8_t,32*32*32*3> a3;BuildPointLightAttenuation3D(a3);glBindTexture(Texture3D,r.textures[2]);r.gl.image3D(Texture3D,0,3,32,32,32,0,GL_RGB,GL_UNSIGNED_BYTE,a3.data());Parameters(Texture3D,GL_LINEAR,true);}
    else {glDeleteTextures(1,&r.textures[2]);r.textures[2]=0;}
    std::array<std::uint8_t,16*16*3> normal;for(unsigned i=0;i<16*16;++i){normal[i*3]=127;normal[i*3+1]=127;normal[i*3+2]=255;}
    glBindTexture(GL_TEXTURE_2D,r.textures[3]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,16,16,0,GL_RGB,GL_UNSIGNED_BYTE,normal.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    std::array<std::uint8_t,6*32*32*3> cube;BuildPointLightCube(cube);glBindTexture(Cube,r.textures[4]);for(unsigned face=0;face<6;++face)glTexImage2D(0x8515+face,0,GL_RGB,32,32,0,GL_RGB,GL_UNSIGNED_BYTE,cube.data()+face*32*32*3);Parameters(Cube,GL_LINEAR);
    if(glGetError()!=GL_NO_ERROR){glDeleteTextures(GLsizei(r.textures.size()),r.textures.data());r.textures={};return false;}
    r.context=wglGetCurrentContext();r.thread=GetCurrentThreadId();r.ready=true;return true;
}
bool PointLightRenderer::ReleaseGL() noexcept {auto& r=*impl_;if(!r.ready)return true;if(!r.Owner())return false;glDeleteTextures(GLsizei(r.textures.size()),r.textures.data());if(r.core){r.coreApi.deleteBuffers(1,&r.vbo);r.coreApi.deleteArrays(1,&r.vao);r.coreApi.deleteProgram(r.program);}AbandonGL();return true;}
void PointLightRenderer::AbandonGL() noexcept {auto& r=*impl_;r.textures={};r.context=nullptr;r.thread=0;r.ready=false;r.program=r.vao=r.vbo=0;r.core=false;}
bool PointLightRenderer::Ready(const PointLightFrame& f) const noexcept {const auto& r=*impl_;return r.Owner()&&r.world&&!r.vertices.empty()&&r.Scene(f)&&r.Brushes(f)&&r.FixedArrays()&&(!r.core||f.authenticatedMatrices)&&((f.forceTwoPass==0.0F&&r.textures[2])||r.alphaBits>0);}
void PointLightRenderer::Impl::Finish(const PointLight& light,bool fallback,const Vec& color,bool disableNv) noexcept
{
    gl.active(Texture0);glDisable(Cube);glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_MODELVIEW);
    if(!fallback){gl.active(Texture0+2);glDisable(Texture3D);NoTexgen(3);return;}
    gl.active(Texture0+1);glBindTexture(GL_TEXTURE_2D,textures[1]);Texgen(light,0,0);Texgen(light,1,1);
    gl.active(Texture0);glDisable(GL_TEXTURE_2D);glEnable(GL_TEXTURE_1D);glBindTexture(GL_TEXTURE_1D,textures[0]);Texgen(light,0,2);
    Arrays(0,0);Arrays(1,0);
    if(nv&&!disableNv) {
        NvGuard nvState(gl);
        glBlendFunc(GL_DST_ALPHA,GL_ONE);glColorMask(1,1,1,0);glEnable(0x8522);
        gl.combinerParameteri(0x854E,1);const GLfloat constant[]{color[0],color[1],color[2],0};gl.combinerParameterfv(0x852A,constant);
        gl.combinerInput(0x8550,GL_RGB,0x8523,Texture0,0x8536,GL_ALPHA);
        gl.combinerInput(0x8550,GL_RGB,0x8524,GL_ZERO,0x8537,GL_RGB);
        gl.combinerInput(0x8550,GL_RGB,0x8525,Texture0+1,0x8536,GL_ALPHA);
        gl.combinerInput(0x8550,GL_RGB,0x8526,GL_ZERO,0x8537,GL_RGB);
        gl.combinerOutput(0x8550,GL_RGB,0x8530,0x8530,0x852E,GL_ZERO,GL_ZERO,0,0,0);
        gl.finalCombinerInput(0x8523,0x852E,0x8536,GL_RGB);gl.finalCombinerInput(0x8524,GL_ZERO,0x8536,GL_RGB);
        gl.finalCombinerInput(0x8525,0x852A,0x8536,GL_RGB);gl.finalCombinerInput(0x8526,GL_ZERO,0x8536,GL_RGB);
        gl.finalCombinerInput(0x8529,GL_ZERO,0x8537,GL_ALPHA);Replay();glDisable(0x8522);
        gl.active(Texture0);glDisable(GL_TEXTURE_1D);glEnable(GL_TEXTURE_2D);NoTexgen(1);gl.active(Texture0+1);NoTexgen(2);return;
    }
    glBlendFunc(GL_ZERO,GL_ONE_MINUS_SRC_ALPHA);Envs(gl,1,1,0,0);
    // Original 62F10 caches mode1 for unit0, so this transition does not
    // re-enable 2D over its 1D attenuation target. Keep that effective state.
    gl.active(Texture0);glDisable(GL_TEXTURE_2D);
    for(unsigned unit=0;unit<2;++unit) {
        gl.active(Texture0+unit);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,Combine);
        glTexEnvi(GL_TEXTURE_ENV,CombineRgb,GL_REPLACE);glTexEnvi(GL_TEXTURE_ENV,Source0Rgb,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,RgbScale,1);
        glTexEnvi(GL_TEXTURE_ENV,CombineAlpha,unit?GL_ADD:GL_REPLACE);glTexEnvi(GL_TEXTURE_ENV,Source0Alpha,unit?Previous:GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,Operand0Alpha,GL_SRC_ALPHA);
        if(unit){glTexEnvi(GL_TEXTURE_ENV,Source1Alpha,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,Operand1Alpha,GL_SRC_ALPHA);}
        glTexEnvi(GL_TEXTURE_ENV,AlphaScale,1);
    }
    Replay();gl.active(Texture0);glDisable(GL_TEXTURE_1D);glEnable(GL_TEXTURE_2D);NoTexgen(1);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    gl.active(Texture0+1);NoTexgen(2);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBlendFunc(GL_DST_ALPHA,GL_ONE);glColorMask(1,1,1,0);Envs(gl,0,0,0,0);glColor3fv(color.data());Replay();
}
PointLightDrawResult PointLightRenderer::Draw(const PointLightFrame& frame,const PointLightState& state) noexcept
{
    auto& r=*impl_;
    if(!r.Owner()||!r.world||r.vertices.empty())return PointLightDrawResult::MissingResources;
    if(!r.Scene(frame)||!r.FixedArrays())return PointLightDrawResult::UnsupportedScene;
    if(!r.Brushes(frame))return PointLightDrawResult::UnsupportedBrushLifecycle;
    if(r.core)return r.DrawCore(frame,state);
    bool any=false;for(const auto& l:state.lights)if(!(frame.time>l.die)&&l.radius!=0.0F){any=true;if(l.rendererFields[20]!=std::byte{})return PointLightDrawResult::UnsupportedLight;}
    if(!any)return PointLightDrawResult::NoLights;
    const bool fallback=frame.forceTwoPass!=0.0F||!r.textures[2]||r.units<4;
    if(fallback&&r.alphaBits<=0)return PointLightDrawResult::MissingResources;
    GLGuard guard(r.gl);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_EQUAL);glStencilMask(0);
    glVertexPointer(3,GL_FLOAT,28,r.vertices.data());glEnableClientState(GL_VERTEX_ARRAY);
    for(const auto& light:state.lights) {
        if(frame.time>light.die||light.radius==0.0F)continue;
        r.drawCount=0;
        if(!r.WorldNode(r.world->nodes,frame,light))return PointLightDrawResult::CapacityExceeded;
        if(frame.glBmodels!=0.0F)for(const auto* entity:frame.visibleBrushes) {
            if(!entity||!entity->model||entity->model->type!=mod_brush)continue;
            if(!MainBrush(*entity))continue;
            float mins[3],maxs[3];for(unsigned i=0;i<3;++i){mins[i]=entity->model->mins[i]+entity->origin[i];maxs[i]=entity->model->maxs[i]+entity->origin[i];}
            if(!r.Outside(mins,maxs,light)&&!r.Brush(entity->model,frame,light))return PointLightDrawResult::CapacityExceeded;
        }
        if(frame.glBmodels!=0.0F)for(const auto& brush:frame.drawnBrushes) {
            if(!MainBrush(brush))continue;
            float mins[3],maxs[3];for(unsigned i=0;i<3;++i){mins[i]=brush.model->mins[i];maxs[i]=brush.model->maxs[i];}
            if(!r.Outside(mins,maxs,light)&&!r.Brush(brush.model,frame,light,&brush.localView))
                return PointLightDrawResult::CapacityExceeded;
        }
        r.DrawBatch(light,frame,fallback);
    }
    // Original main world/static-brush stage precedes each transformed brush's
    // per-light pass. Keep the order, including alpha-buffer fallback replay.
    if(frame.glBmodels!=0.0F)for(const auto& brush:frame.drawnBrushes) {
        if(MainBrush(brush))continue;
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glMultMatrixf(brush.modelToWorld.data());
        struct MatrixTail {~MatrixTail(){glMatrixMode(GL_MODELVIEW);glPopMatrix();}} matrixTail;
        for(const auto& light:state.lights) {
            if(frame.time>light.die||light.radius==0.0F)continue;
            float mins[3],maxs[3];
            for(unsigned i=0;i<3;++i) {
                mins[i]=brush.origin[i]+(brush.rotated?-brush.model->radius:brush.model->mins[i]);
                maxs[i]=brush.origin[i]+(brush.rotated?brush.model->radius:brush.model->maxs[i]);
            }
            if(r.Outside(mins,maxs,light))continue;
            PointLight local=light;local.origin=PointLightBrushOrigin(brush,light.origin);
            r.drawCount=0;
            if(!r.Brush(brush.model,frame,local,&brush.localView))return PointLightDrawResult::CapacityExceeded;
            r.DrawBatch(local,frame,fallback);
        }
    }
    return PointLightDrawResult::Drawn;
}
void PointLightRenderer::Impl::DrawBatch(const PointLight& light,const PointLightFrame& frame,bool fallback) noexcept
{
    const Vec color{PointLightColor(light.color[0],frame.gamma),PointLightColor(light.color[1],frame.gamma),PointLightColor(light.color[2],frame.gamma)};
    glEnable(GL_BLEND);glDepthMask(GL_FALSE);
    Arrays(0,3);Arrays(1,1);Arrays(2,0);Arrays(3,0);
    if(!fallback) {
        glBlendFunc(GL_ONE,GL_ONE);Envs(gl,1,7,4,3);glColor3fv(color.data());
        gl.active(Texture0);glEnable(Cube);glBindTexture(Cube,textures[4]);
        gl.active(Texture0+2);glEnable(Texture3D);glBindTexture(Texture3D,textures[2]);Texgen(light,0,0);Texgen(light,1,1);Texgen(light,2,2);
        gl.active(Texture0+3);glBindTexture(GL_TEXTURE_2D,textures[3]);
    } else {
        glBlendFunc(GL_ONE,GL_ZERO);glColorMask(0,0,0,1);Envs(gl,1,7,0,0);
        gl.active(Texture0);glEnable(Cube);glBindTexture(Cube,textures[4]);
    }
    for(std::size_t i=0;i<drawCount;++i)DrawOne(draws[i].index,light);
    Finish(light,fallback,color,frame.disableNvCombiners!=0.0F);
}
}
