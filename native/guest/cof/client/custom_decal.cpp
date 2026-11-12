// SPDX-License-Identifier: GPL-3.0-or-later
#include "custom_decal.hpp"
#include "core_gl.hpp"
#include <metahook.h>
#include <com_model.h>
#include <GL/gl.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <vector>
#pragma clang fp contract(off)
namespace guest::cof::client
{
namespace
{
using Vec = DecalVector;
constexpr std::size_t MaxVertices=68;
float DotYX(const Vec& a,const Vec& b) noexcept {return (a[1]*b[1]+a[0]*b[0])+a[2]*b[2];}
float DotXY(const Vec& a,const Vec& b) noexcept {return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];}
Vec Cross(const Vec& a,const Vec& b) noexcept {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vec Unit(Vec a) noexcept
{
    const float length=float(std::sqrt(double(DotXY(a,a))));
    if(length!=0.0F){const float inv=1.0F/length;for(auto& v:a)v*=inv;}
    return a;
}
Vec Offset(const Vec& a,float distance,const Vec& axis) noexcept
{
    return {a[0]+distance*axis[0],a[1]+distance*axis[1],a[2]+distance*axis[2]};
}
Vec Neg(Vec a) noexcept {for(auto& v:a)v=std::bit_cast<float>(std::bit_cast<std::uint32_t>(v)^0x80000000U);return a;}
bool Finite(const Vec& v) noexcept {return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
}
CustomDecalBasis MakeCustomDecalBasis(const Vec& normal) noexcept
{
    const Vec reference=normal[0]==0.0F&&normal[1]==0.0F?Vec{1,0,0}:Vec{0,0,1};
    CustomDecalBasis out;out.t=Unit(Cross(normal,reference));out.s=Unit(Cross(normal,out.t));return out;
}
Vec CustomDecalIntersection(const Vec& a,const Vec& b,const Vec& normal,const Vec& plane) noexcept
{
    const Vec delta{b[0]-a[0],b[1]-a[1],b[2]-a[2]};
    const float denominator=DotXY(delta,normal);
    if(denominator==0.0F)return {};
    const Vec difference{plane[0]-a[0],plane[1]-a[1],plane[2]-a[2]};
    const float fraction=DotYX(difference,normal)/denominator;
    return Offset(a,fraction,delta);
}
std::size_t ClipCustomDecal(std::span<const Vec> input,const Vec& normal,const Vec& plane,std::span<Vec> output) noexcept
{
    if(input.empty()||input.size()>MaxVertices||output.size()<input.size()+2)return 0;
    std::array<float,MaxVertices> distances{};std::size_t start=input.size();
    for(std::size_t i=0;i<input.size();++i){const Vec delta{input[i][0]-plane[0],input[i][1]-plane[1],input[i][2]-plane[2]};distances[i]=DotYX(delta,normal);if(distances[i]>0.0F)start=i;}
    if(start==input.size())return 0;
    auto next=[&](std::size_t i){return i+1==input.size()?0:i+1;};
    auto previous=[&](std::size_t i){return i?i-1:input.size()-1;};
    std::size_t count=0;output[count++]=input[start];auto i=next(start);
    while(i!=start&&distances[i]>0.0F){output[count++]=input[i];i=next(i);}
    if(i==start)return count;
    output[count++]=distances[i]<0.0F?CustomDecalIntersection(input[previous(i)],input[i],normal,plane):input[i];
    i=next(i);
    while(distances[i]<0.0F)i=next(i);
    if(distances[i]>0.0F&&distances[previous(i)]<0.0F)output[count++]=CustomDecalIntersection(input[previous(i)],input[i],normal,plane);
    while(i!=start){output[count++]=input[i];i=next(i);}
    return count;
}
bool BuildShotDecalTexture(std::span<const std::uint8_t> tga,ShotDecalTexture& out) noexcept
{
    if(tga.size()<18||tga[2]!=2||tga[16]!=24||tga[12]!=64||tga[13]||tga[14]!=64||tga[15])return false;
    const std::size_t offset=18U+tga[0];if(offset>tga.size()||tga.size()-offset<64*64*3)return false;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const auto source=offset+((tga[17]&32?y:63-y)*64+x)*3;const auto dest=(y*64+x)*3;out[dest]=tga[source+2];out[dest+1]=tga[source+1];out[dest+2]=tga[source];}
    unsigned width=64;std::size_t source=0,dest=64*64*3;
    while(width>1){const auto half=width/2;for(unsigned y=0;y<half;++y)for(unsigned x=0;x<half;++x)for(unsigned c=0;c<3;++c){const auto p=source+((y*2)*width+x*2)*3+c;const unsigned sum=unsigned(out[p])+out[p+3]+out[p+width*3]+out[p+width*3+3];out[dest+(y*half+x)*3+c]=std::uint8_t(sum>>2);}source=dest;dest+=half*half*3;width=half;}
    return true;
}
namespace
{
struct Surface {const msurface_t* host{};Vec normal{};float distance{};std::size_t first{},worldIndex{};unsigned count{};};
struct Message {Vec origin{},normal{};};
struct Mark
{
    std::array<CustomDecalVertex,MaxVertices> vertices{};Message message{};
    std::size_t surface{};unsigned count{},variant{};
};
constexpr GLenum Texture0=0x84C0,ActiveTexture=0x84E0,CurrentProgram=0x8B8D;
template<class T>T Proc(const char* name) noexcept {auto p=wglGetProcAddress(name);if(p==nullptr||p==reinterpret_cast<PROC>(1)||p==reinterpret_cast<PROC>(2)||p==reinterpret_cast<PROC>(3)||p==reinterpret_cast<PROC>(-1))return nullptr;return reinterpret_cast<T>(p);}
struct GLFunctions {void(APIENTRY* active)(GLenum){};void(APIENTRY* program)(GLuint){};void(APIENTRY* equation)(GLenum){};void(APIENTRY* buffer)(GLenum,GLuint){};void(APIENTRY* sampler)(GLuint,GLuint){};};
struct GLGuard
{
    GLFunctions& gl;GLint active{},program{},mode{},unpackBuffer{},sampler{};bool upload{};
    explicit GLGuard(GLFunctions& f,bool uploading=false):gl(f),upload(uploading)
    {
        glGetIntegerv(ActiveTexture,&active);glGetIntegerv(CurrentProgram,&program);glGetIntegerv(GL_MATRIX_MODE,&mode);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        if(upload){glGetIntegerv(0x88EF,&unpackBuffer);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);gl.buffer(0x88EC,0);}
        for(unsigned unit=0;unit<4;++unit){gl.active(Texture0+unit);glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();}
        gl.active(Texture0);if(gl.sampler){glGetIntegerv(0x8919,&sampler);gl.sampler(0,0);}glMatrixMode(GLenum(mode));gl.program(0);
    }
    ~GLGuard()
    {
        for(unsigned unit=0;unit<4;++unit){gl.active(Texture0+unit);glMatrixMode(GL_TEXTURE);glPopMatrix();}
        if(upload){gl.buffer(0x88EC,GLuint(unpackBuffer));glPopClientAttrib();}
        glPopAttrib();if(gl.sampler)gl.sampler(0,GLuint(sampler));gl.program(GLuint(program));gl.active(GLenum(active));glMatrixMode(GLenum(mode));
    }
};
}
struct CustomDecals::Impl
{
    std::vector<Surface> surfaces;std::vector<Vec> vertices;bool mapReady{};
    std::array<Mark,1024> marks{};std::size_t count{},next{};
    std::array<Message,256> pending{};std::size_t pendingCount{};
    GLFunctions gl{};std::array<GLuint,5> textures{};HGLRC context{};DWORD thread{};bool ready{};
    core_gl::Api coreApi{};bool core{};GLuint program{},vao{},vbo{};GLint worldUniform{},projectionUniform{},samplerUniform{};
    std::array<bool,1024> dirty{};bool geometryPending{};
    bool Owner() const noexcept {return ready&&context==wglGetCurrentContext()&&thread==GetCurrentThreadId();}
    CustomDecalResult Add(const Message& message,CustomDecalRandom random) noexcept
    {
        if(!mapReady){if(pendingCount==pending.size())return CustomDecalResult::QueueFull;pending[pendingCount++]=message;return CustomDecalResult::Queued;}
        if(!random.range)return CustomDecalResult::MissingRandom;
        // Original chooses once for the message, before scanning any surface.
        const int choice=random.range(random.context,0,4);if(choice<0||choice>4)return CustomDecalResult::MissingRandom;
        const auto basis=MakeCustomDecalBasis(message.normal);const float radius=float(std::sqrt(18.0));bool added=false;
        std::array<Vec,MaxVertices+2> a{},b{};
        // 10058E40's second output is the width/U axis; its first is height/V.
        const std::array<Vec,4> normals{basis.t,Neg(basis.t),basis.s,Neg(basis.s)};
        const std::array<Vec,4> planes{Offset(message.origin,-3,basis.t),Offset(message.origin,3,basis.t),Offset(message.origin,-3,basis.s),Offset(message.origin,3,basis.s)};
        for(std::size_t index=0;index<surfaces.size();++index){const auto& surface=surfaces[index];if(!surface.count||double(DotYX(surface.normal,message.normal))<.7||std::fabs(DotYX(message.origin,surface.normal)-surface.distance)>radius)continue;
            auto size=std::size_t(surface.count);auto* input=&a;auto* output=&b;std::copy_n(vertices.data()+surface.first,size,input->data());
            for(unsigned side=0;side<4&&size;++side){size=ClipCustomDecal({input->data(),size},normals[side],planes[side],*output);std::swap(input,output);}
            if(!size||size>MaxVertices)continue;
            std::size_t slot=count;
            for(std::size_t i=0;i<count;++i)if(marks[i].surface==index&&std::fabs(DotYX(marks[i].message.origin,basis.s)-DotYX(message.origin,basis.s))<3.0F&&std::fabs(DotYX(marks[i].message.origin,basis.t)-DotYX(message.origin,basis.t))<3.0F){slot=i;break;}
            if(slot==count){slot=next;next=(next+1)%marks.size();count=std::min(count+1,marks.size());}
            auto& mark=marks[slot];mark.message=message;mark.surface=index;mark.variant=unsigned(choice);mark.count=unsigned(size);dirty[slot]=true;geometryPending=true;
            const float originS=DotXY(message.origin,basis.s),originT=DotXY(message.origin,basis.t);
            for(std::size_t v=0;v<size;++v){const auto& point=(*input)[v];mark.vertices[v].position=point;mark.vertices[v].uv={((DotYX(point,basis.t)-originT)/3.0F+1.0F)*.5F,((DotYX(point,basis.s)-originS)/3.0F+1.0F)*.5F};}
            added=true;
        }
        return added?CustomDecalResult::Added:CustomDecalResult::NoSurface;
    }
    bool InitializeCore(const std::array<std::span<const std::uint8_t>,5>& images)
    {
        if(!coreApi.Initialize())return false;
        std::array<ShotDecalTexture,5> rgb;for(unsigned i=0;i<5;++i)if(!BuildShotDecalTexture(images[i],rgb[i]))return false;
        constexpr auto vertex=R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec2 texcoord;
uniform mat4 world;uniform mat4 projection;out vec2 uv;
void main(){uv=texcoord;gl_Position=projection*(world*vec4(position,1.0));}
)";
        constexpr auto fragment=R"(#version 330 core
uniform sampler2D shot;in vec2 uv;out vec4 color;
void main(){color=texture(shot,uv);}
)";
        core_gl::State guard(coreApi);
        program=coreApi.Program(vertex,fragment);
        if(!program)return false;
        worldUniform=coreApi.uniform(program,"world");projectionUniform=coreApi.uniform(program,"projection");samplerUniform=coreApi.uniform(program,"shot");
        coreApi.genArrays(1,&vao);coreApi.genBuffers(1,&vbo);glGenTextures(5,textures.data());
        const auto cleanup=[&]{if(vbo)coreApi.deleteBuffers(1,&vbo);if(vao)coreApi.deleteArrays(1,&vao);coreApi.deleteProgram(program);glDeleteTextures(5,textures.data());program=vao=vbo=0;textures={};};
        if(!vao||!vbo||std::ranges::any_of(textures,[](GLuint id){return !id;})){cleanup();return false;}
        coreApi.bindArray(vao);coreApi.bindBuffer(core_gl::ArrayBuffer,vbo);
        coreApi.bufferData(core_gl::ArrayBuffer,std::ptrdiff_t(marks.size()*MaxVertices*sizeof(CustomDecalVertex)),nullptr,0x88E8);
        coreApi.enableAttribute(0);coreApi.attribute(0,3,GL_FLOAT,GL_FALSE,sizeof(CustomDecalVertex),nullptr);
        coreApi.enableAttribute(1);coreApi.attribute(1,2,GL_FLOAT,GL_FALSE,sizeof(CustomDecalVertex),reinterpret_cast<void*>(offsetof(CustomDecalVertex,uv)));
        for(unsigned i=0;i<5;++i){glBindTexture(GL_TEXTURE_2D,textures[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2701);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);unsigned width=64;std::size_t offset=0;for(int level=0;level<7;++level){glTexImage2D(GL_TEXTURE_2D,level,0x8051,GLsizei(width),GLsizei(width),0,GL_RGB,GL_UNSIGNED_BYTE,rgb[i].data()+offset);offset+=width*width*3;width=std::max(1U,width/2);}}
        if(glGetError()!=GL_NO_ERROR){cleanup();return false;}
        dirty.fill(true);geometryPending=true;context=wglGetCurrentContext();thread=GetCurrentThreadId();ready=true;return true;
    }
    CustomDecalDrawResult DrawCore(const CustomDecalFrame& frame) noexcept
    {
        if(!frame.authenticatedMatrices)return CustomDecalDrawResult::UnsupportedScene;
        core_gl::State guard(coreApi);coreApi.use(program);coreApi.bindArray(vao);coreApi.bindBuffer(core_gl::ArrayBuffer,vbo);
        coreApi.matrix(worldUniform,1,GL_FALSE,frame.worldMatrix.data());coreApi.matrix(projectionUniform,1,GL_FALSE,frame.projectionMatrix.data());coreApi.integer(samplerUniform,0);
        glEnable(GL_BLEND);coreApi.equation(0x8006,0x8006);glBlendFunc(GL_DST_COLOR,GL_SRC_COLOR);glDepthMask(GL_FALSE);glPolygonOffset(-1,-1);glEnable(GL_POLYGON_OFFSET_FILL);
        for(std::size_t i=0;i<count;++i){const auto& mark=marks[i];const auto& surface=surfaces[mark.surface];
            if(frame.visibleSurfaceWords.empty()){if(surface.host->visframe!=frame.surfaceVisframe)continue;}else{const auto word=surface.worldIndex/64;if(word>=frame.visibleSurfaceWords.size()||!(frame.visibleSurfaceWords[word]&(std::uint64_t{1}<<(surface.worldIndex%64))))continue;}
            if(dirty[i])continue;
            glBindTexture(GL_TEXTURE_2D,textures[mark.variant]);glDrawArrays(GL_TRIANGLE_FAN,GLint(i*MaxVertices),GLsizei(mark.count));
        }
        return CustomDecalDrawResult::Drawn;
    }
};
CustomDecals::CustomDecals():impl_(std::make_unique<Impl>()){}
CustomDecals::~CustomDecals(){ReleaseGL();}
bool CustomDecals::PrepareMap(model_s& model,bool hl25)
{
    static_assert(sizeof(msurface_t)==92&&sizeof(msurface_hl25_t)==108);
    ClearMarks();auto& r=*impl_;r.mapReady=false;r.surfaces.clear();r.vertices.clear();
    if(model.type!=mod_brush||model.numsurfaces<0||!model.surfaces||model.firstmodelsurface<0||model.nummodelsurfaces<0||model.firstmodelsurface>model.numsurfaces||model.nummodelsurfaces>model.numsurfaces-model.firstmodelsurface)return false;
    const std::size_t stride=hl25?sizeof(msurface_hl25_t):sizeof(msurface_t);std::size_t total=0;
    for(int i=0;i<model.nummodelsurfaces;++i){const auto* s=reinterpret_cast<const msurface_t*>(reinterpret_cast<const char*>(model.surfaces)+(std::size_t(model.firstmodelsurface)+std::size_t(i))*stride);if((s->flags&0x14)||!s->samples)continue;if(!s->plane||!s->polys||s->polys->numverts<3||s->polys->numverts>64)return false;total+=std::size_t(s->polys->numverts);}
    r.surfaces.reserve(std::size_t(model.nummodelsurfaces));r.vertices.reserve(total);
    for(int i=0;i<model.nummodelsurfaces;++i){const auto* s=reinterpret_cast<const msurface_t*>(reinterpret_cast<const char*>(model.surfaces)+(std::size_t(model.firstmodelsurface)+std::size_t(i))*stride);if((s->flags&0x14)||!s->samples)continue;
        Surface surface;surface.host=s;surface.normal={s->plane->normal[0],s->plane->normal[1],s->plane->normal[2]};surface.distance=s->plane->dist;
        if(s->flags&2){surface.normal=Neg(surface.normal);surface.distance=-surface.distance;}
        surface.first=r.vertices.size();surface.worldIndex=std::size_t(model.firstmodelsurface)+std::size_t(i);surface.count=unsigned(s->polys->numverts);
        if(!Finite(surface.normal)||!std::isfinite(surface.distance))return false;
        for(unsigned v=0;v<surface.count;++v){Vec point;std::memcpy(point.data(),s->polys->verts[v],sizeof(point));if(!Finite(point))return false;r.vertices.push_back(point);}
        r.surfaces.push_back(surface);
    }
    r.mapReady=true;return true;
}
void CustomDecals::ClearMarks() noexcept {impl_->count=0;impl_->next=0;}
void CustomDecals::ClearMap() noexcept {ClearMarks();impl_->mapReady=false;impl_->surfaces.clear();impl_->vertices.clear();impl_->pendingCount=0;}
CustomDecalResult CustomDecals::Receive(std::span<const std::uint8_t> bytes,float enabled,CustomDecalRandom random) noexcept
{
    if(bytes.size()!=18||std::memcmp(bytes.data()+13,"shot\0",5)!=0)return CustomDecalResult::Malformed;
    if(bytes[12]!=0)return CustomDecalResult::Unsupported;
    if(enabled==0.0F)return CustomDecalResult::Disabled;
    Message message;for(unsigned i=0;i<6;++i){const auto raw=std::uint16_t(unsigned(bytes[i*2])|(unsigned(bytes[i*2+1])<<8));const float value=float(std::bit_cast<std::int16_t>(raw))*(1.0F/8.0F);(i<3?message.origin[i]:message.normal[i-3])=value;}
    return impl_->Add(message,random);
}
void CustomDecals::FlushPending(float enabled,CustomDecalRandom random) noexcept
{
    auto& r=*impl_;if(!r.mapReady||!random.range)return;
    const auto count=r.pendingCount;r.pendingCount=0;if(enabled==0.0F)return;
    for(std::size_t i=0;i<count;++i)r.Add(r.pending[i],random);
}
std::size_t CustomDecals::Count() const noexcept {return impl_->count;}
std::size_t CustomDecals::Pending() const noexcept {return impl_->pendingCount;}
CustomDecalView CustomDecals::Inspect(std::size_t slot) const noexcept
{
    if(slot>=impl_->count)return {};const auto& mark=impl_->marks[slot];return {{mark.vertices.data(),mark.count},mark.message.origin,mark.message.normal,mark.surface,mark.variant};
}
bool CustomDecals::InitializeGL(const std::array<std::span<const std::uint8_t>,5>& images)
{
    auto& r=*impl_;if(r.Owner())return true;if(r.ready||!wglGetCurrentContext())return false;
    r.core=core_gl::IsCore();if(r.core)return r.InitializeCore(images);
    std::array<ShotDecalTexture,5> rgb;for(unsigned i=0;i<5;++i)if(!BuildShotDecalTexture(images[i],rgb[i]))return false;
    r.gl.active=Proc<decltype(r.gl.active)>("glActiveTexture");r.gl.program=Proc<decltype(r.gl.program)>("glUseProgram");r.gl.equation=Proc<decltype(r.gl.equation)>("glBlendEquation");r.gl.buffer=Proc<decltype(r.gl.buffer)>("glBindBuffer");r.gl.sampler=Proc<decltype(r.gl.sampler)>("glBindSampler");if(!r.gl.active||!r.gl.program||!r.gl.equation||!r.gl.buffer)return false;
    GLint units{};glGetIntegerv(0x84E2,&units);if(units<4)return false;
    GLGuard guard(r.gl,true);glGenTextures(5,r.textures.data());
    if(std::ranges::any_of(r.textures,[](GLuint id){return id==0;})){glDeleteTextures(5,r.textures.data());r.textures={};return false;}
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for(unsigned i=0;i<5;++i){glBindTexture(GL_TEXTURE_2D,r.textures[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2701);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);unsigned width=64;std::size_t offset=0;for(int level=0;level<7;++level){glTexImage2D(GL_TEXTURE_2D,level,3,GLsizei(width),GLsizei(width),0,GL_RGB,GL_UNSIGNED_BYTE,rgb[i].data()+offset);offset+=width*width*3;width=std::max(1U,width/2);}}
    if(glGetError()!=GL_NO_ERROR){glDeleteTextures(5,r.textures.data());r.textures={};return false;}
    r.context=wglGetCurrentContext();r.thread=GetCurrentThreadId();r.ready=true;return true;
}
bool CustomDecals::CoreProfile() const noexcept {return impl_->ready&&impl_->core;}
bool CustomDecals::PrepareFrameGL() noexcept
{
    auto& r=*impl_;if(!r.Owner())return false;if(!r.core||!r.geometryPending)return true;
    GLint previous{};glGetIntegerv(0x8894,&previous);r.coreApi.bindBuffer(core_gl::ArrayBuffer,r.vbo);
    for(std::size_t i=0;i<r.count;++i)if(r.dirty[i]){const auto& mark=r.marks[i];r.coreApi.bufferSubData(core_gl::ArrayBuffer,std::ptrdiff_t(i*MaxVertices*sizeof(CustomDecalVertex)),std::ptrdiff_t(mark.count*sizeof(CustomDecalVertex)),mark.vertices.data());r.dirty[i]=false;}
    r.coreApi.bindBuffer(core_gl::ArrayBuffer,GLuint(previous));r.geometryPending=false;return true;
}
bool CustomDecals::ReleaseGL() noexcept {auto& r=*impl_;if(!r.ready)return true;if(!r.Owner())return false;glDeleteTextures(5,r.textures.data());if(r.core){r.coreApi.deleteBuffers(1,&r.vbo);r.coreApi.deleteArrays(1,&r.vao);r.coreApi.deleteProgram(r.program);}AbandonGL();return true;}
void CustomDecals::AbandonGL() noexcept {impl_->ready=false;impl_->textures={};impl_->context=nullptr;impl_->thread=0;impl_->program=impl_->vao=impl_->vbo=0;impl_->core=false;}
CustomDecalDrawResult CustomDecals::Draw(const CustomDecalFrame& frame,float enabled) noexcept
{
    auto& r=*impl_;if(enabled==0.0F)return CustomDecalDrawResult::Disabled;if(!r.count)return CustomDecalDrawResult::Empty;if(!r.Owner()||!r.mapReady)return CustomDecalDrawResult::MissingResources;
    if(!frame.matchingOpaqueDepth||!frame.visibilityEstablished||!frame.originalColorSpace)return CustomDecalDrawResult::UnsupportedScene;
    if(r.core)return r.DrawCore(frame);
    GLGuard guard(r.gl);
    for(unsigned unit=0;unit<4;++unit){r.gl.active(Texture0+unit);glDisable(GL_TEXTURE_1D);glDisable(0x806F);glDisable(0x8513);if(unit)glDisable(GL_TEXTURE_2D);}
    r.gl.active(Texture0);glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_BLEND);r.gl.equation(0x8006);glBlendFunc(GL_DST_COLOR,GL_SRC_COLOR);glDepthMask(GL_FALSE);glPolygonOffset(-1,-1);glEnable(GL_POLYGON_OFFSET_FILL);
    for(std::size_t i=0;i<r.count;++i){const auto& mark=r.marks[i];const auto& surface=r.surfaces[mark.surface];if(frame.visibleSurfaceWords.empty()){if(surface.host->visframe!=frame.surfaceVisframe)continue;}else{const auto word=surface.worldIndex/64;if(word>=frame.visibleSurfaceWords.size()||!(frame.visibleSurfaceWords[word]&(std::uint64_t{1}<<(surface.worldIndex%64))))continue;}glBindTexture(GL_TEXTURE_2D,r.textures[mark.variant]);glBegin(GL_POLYGON);for(unsigned v=0;v<mark.count;++v){glTexCoord2fv(mark.vertices[v].uv.data());glVertex3fv(mark.vertices[v].position.data());}glEnd();}
    return CustomDecalDrawResult::Drawn;
}
}
