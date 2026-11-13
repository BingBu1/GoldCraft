// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/custom_decal.hpp"
#include <metahook.h>
#include <com_model.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <vector>
using namespace guest::cof::client;
void RunDecalAdapterTests();
namespace
{
void Check(bool yes,const char* what){if(!yes){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
template<class T>void Read(T& value){Check(std::fread(&value,sizeof(value),1,stdin)==1,"input");}
template<class T>void Write(const T& value){Check(std::fwrite(&value,sizeof(value),1,stdout)==1,"output");}
std::array<std::uint8_t,18> Message(float x=0,float y=0,float z=0,DecalVector normal={0,0,1})
{
    std::array<std::uint8_t,18> out{};const std::array<float,6> values{x,y,z,normal[0],normal[1],normal[2]};
    for(unsigned i=0;i<6;++i){const auto v=std::bit_cast<std::uint16_t>(std::int16_t(values[i]*8));out[i*2]=std::uint8_t(v);out[i*2+1]=std::uint8_t(v>>8);}std::memcpy(out.data()+13,"shot",5);return out;
}
struct Random {int calls{},choice{};static int Range(void* p,int low,int high){auto& r=*static_cast<Random*>(p);Check(low==0&&high==4,"variant range");++r.calls;return r.choice;}CustomDecalRandom Get(){return {this,Range};}};
struct World
{
    model_t model{};msurface_t surface{};mplane_t plane{};std::vector<std::byte> storage;std::uint8_t sample{};
    World():storage(offsetof(glpoly_t,verts)+4*7*sizeof(float))
    {
        auto* poly=reinterpret_cast<glpoly_t*>(storage.data());poly->numverts=4;const std::array<DecalVector,4> points{{{-2048,-2048,0},{2048,-2048,0},{2048,2048,0},{-2048,2048,0}}};for(unsigned i=0;i<4;++i)std::memcpy(poly->verts[i],points[i].data(),12);
        plane.normal[2]=1;surface.plane=&plane;surface.polys=poly;surface.samples=&sample;surface.visframe=77;model.type=mod_brush;model.surfaces=&surface;model.numsurfaces=1;model.nummodelsurfaces=1;
    }
};
void Lifecycle()
{
    CustomDecals decals;Random random;auto message=Message();
    Check(decals.Receive(message,0,random.Get())==CustomDecalResult::Disabled,"disabled");
    auto invalid=message;invalid[12]=1;Check(decals.Receive(invalid,1,random.Get())==CustomDecalResult::Unsupported,"static rejected");
    invalid=message;invalid[17]=1;Check(decals.Receive(invalid,1,random.Get())==CustomDecalResult::Malformed,"unterminated");
    Check(decals.Receive(std::span(message).first(17),1,random.Get())==CustomDecalResult::Malformed,"truncated");
    for(unsigned i=0;i<256;++i)Check(decals.Receive(message,1,random.Get())==CustomDecalResult::Queued,"queue");
    Check(decals.Receive(message,1,random.Get())==CustomDecalResult::QueueFull&&random.calls==0,"bounded queue before RNG");
    World world;Check(decals.PrepareMap(world.model,false),"prepare old SDK");decals.FlushPending(1,random.Get());
    Check(decals.Pending()==0&&decals.Count()==1&&random.calls==256,"flush overlap");
    const auto first=decals.Inspect(0);Check(first.vertices.size()==4&&first.variant==0,"mesh");
    for(const auto& vertex:first.vertices){Check(std::fabs(vertex.position[0])==3&&std::fabs(vertex.position[1])==3,"clip extent");Check(vertex.uv[0]==(vertex.position[1]/3.0F+1.0F)*.5F&&vertex.uv[1]==(1.0F-vertex.position[0]/3.0F)*.5F,"original UV axes");}
    random.choice=4;Check(decals.Receive(Message(2.875F),1,random.Get())==CustomDecalResult::Added&&decals.Count()==1&&decals.Inspect(0).variant==4,"strict overlap replaces variant");
    Check(decals.Receive(Message(5.875F),1,random.Get())==CustomDecalResult::Added&&decals.Count()==2,"exact3 is new slot");
    Check(decals.Receive(Message(0,0,5),1,random.Get())==CustomDecalResult::NoSurface,"plane radius rejection");
    Check(decals.Receive(Message(0,0,0,{0,0,-1}),1,random.Get())==CustomDecalResult::NoSurface,"normal rejection");
    decals.ClearMarks();Check(decals.Count()==0,"marks clear");
    for(unsigned i=0;i<1025;++i)Check(decals.Receive(Message(float(i%33)*6,float(i/33)*6),1,random.Get())==CustomDecalResult::Added,"ring add");
    Check(decals.Count()==1024&&decals.Inspect(0).origin==DecalVector{float(1024%33)*6,float(1024/33)*6,0},"1024 ring wraps");
    decals.ClearMap();Check(decals.Count()==0&&decals.Pending()==0,"map clears lifecycle");
    world.surface.samples=nullptr;Check(decals.PrepareMap(world.model,false),"unlit prepare");Check(decals.Receive(message,1,random.Get())==CustomDecalResult::NoSurface,"samples required");
    world.surface.samples=&world.sample;world.surface.flags=0x10;Check(decals.PrepareMap(world.model,false),"turbulent prepare");Check(decals.Receive(message,1,random.Get())==CustomDecalResult::NoSurface,"turbulent excluded");
    msurface_hl25_t modern[2]{};static_cast<msurface_t&>(modern[1])=world.surface;modern[1].flags=0;world.model.surfaces=modern;world.model.numsurfaces=2;world.model.firstmodelsurface=1;
    Check(decals.PrepareMap(world.model,true)&&decals.Receive(message,1,random.Get())==CustomDecalResult::Added,"HL25 stride and model surface range");
    std::puts("lifecycle: queue, RNG ordering, wire, UV, overlap, ring, surface eligibility, HL25, map reset passed");
}
void GLPixels()
{
    const auto module=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=module;wc.lpszClassName=L"CoFDecalTestHidden";Check(RegisterClassW(&wc)!=0,"register hidden window");
    const auto window=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,module,nullptr);Check(window!=nullptr,"hidden window");
    const auto dc=GetDC(window);PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=24;pfd.cDepthBits=24;
    const auto format=ChoosePixelFormat(dc,&pfd);Check(format&&SetPixelFormat(dc,format,&pfd),"pixel format");const auto context=wglCreateContext(dc);Check(context&&wglMakeCurrent(dc,context),"WGL context");
    std::vector<std::uint8_t> image(18+64*64*3,64);image[0]=0;image[2]=2;image[12]=64;image[13]=0;image[14]=64;image[15]=0;image[16]=24;image[17]=32;
    std::array<std::span<const std::uint8_t>,5> images;images.fill(image);CustomDecals decals;Random random;World world;Check(decals.PrepareMap(world.model,false),"GL map");Check(decals.Receive(Message(),1,random.Get())==CustomDecalResult::Added,"GL shot");
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,73);glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);Check(decals.InitializeGL(images),"actual GL textures");GLint unpack{},rowLength{},skipRows{},skipPixels{};glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);Check(unpack==8&&rowLength==73&&skipRows==2&&skipPixels==3,"upload preserves unpack");
    glViewport(0,0,64,64);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(-8,8,-8,8,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DITHER);glDisable(GL_TEXTURE_2D);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glClearColor(.4F,.4F,.4F,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glColor3f(.4F,.4F,.4F);glBegin(GL_QUADS);glVertex3f(-8,-8,0);glVertex3f(8,-8,0);glVertex3f(8,8,0);glVertex3f(-8,8,0);glEnd();
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);glBlendFunc(GL_ONE,GL_ZERO);glPolygonOffset(2,3);glDisable(GL_POLYGON_OFFSET_FILL);
    auto active=reinterpret_cast<void(APIENTRY*)(GLenum)>(wglGetProcAddress("glActiveTexture"));Check(active!=nullptr,"multitexture entry");std::array<GLuint,4> sentinels{};glGenTextures(4,sentinels.data());for(unsigned i=0;i<4;++i){active(0x84C0+i);glBindTexture(GL_TEXTURE_2D,sentinels[i]);glMatrixMode(GL_TEXTURE);glLoadIdentity();glScalef(float(i+2),float(i+3),1);}glMatrixMode(GL_MODELVIEW);
    auto genSamplers=reinterpret_cast<void(APIENTRY*)(GLsizei,GLuint*)>(wglGetProcAddress("glGenSamplers"));auto bindSampler=reinterpret_cast<void(APIENTRY*)(GLuint,GLuint)>(wglGetProcAddress("glBindSampler"));auto deleteSamplers=reinterpret_cast<void(APIENTRY*)(GLsizei,const GLuint*)>(wglGetProcAddress("glDeleteSamplers"));GLuint sampler{};if(genSamplers&&bindSampler&&deleteSamplers){genSamplers(1,&sampler);bindSampler(0,sampler);}
    const CustomDecalFrame frame{true,true,true,77};Check(decals.Draw({false,true,true,77},1)==CustomDecalDrawResult::UnsupportedScene,"scene refusal");Check(decals.Draw(frame,1)==CustomDecalDrawResult::Drawn,"GL draw");
    GLboolean depth{};GLint source{},dest{};GLfloat factor{},units{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);glGetIntegerv(GL_BLEND_SRC,&source);glGetIntegerv(GL_BLEND_DST,&dest);glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);
    Check(depth==GL_TRUE&&!glIsEnabled(GL_BLEND)&&source==GL_ONE&&dest==GL_ZERO&&factor==2&&units==3&&!glIsEnabled(GL_POLYGON_OFFSET_FILL)&&!glIsEnabled(GL_TEXTURE_2D),"draw restores GL state");
    GLint currentActive{};glGetIntegerv(0x84E0,&currentActive);Check(currentActive==0x84C3,"active unit restored");for(unsigned i=0;i<4;++i){active(0x84C0+i);GLint binding{};std::array<float,16> matrix{};glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);glGetFloatv(GL_TEXTURE_MATRIX,matrix.data());Check(GLuint(binding)==sentinels[i]&&matrix[0]==float(i+2)&&matrix[5]==float(i+3),"cached binding and texture matrix restored");if(!i&&sampler){GLint restored{};glGetIntegerv(0x8919,&restored);Check(GLuint(restored)==sampler,"sampler0 restored");}}
    std::array<std::uint8_t,3> pixel{};glReadPixels(32,32,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());for(const auto c:pixel)Check(c>=50&&c<=52,"multiplicative decal pixel");
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);Check(decals.Draw({true,true,true,78},1)==CustomDecalDrawResult::Drawn,"visibility draw");glReadPixels(32,32,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());for(const auto c:pixel)Check(c>=101&&c<=103,"invisible surface skipped");
    Check(glGetError()==GL_NO_ERROR,"GL error");Check(decals.ReleaseGL(),"GL release");if(sampler)deleteSamplers(1,&sampler);glDeleteTextures(4,sentinels.data());RunDecalAdapterTests();wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,module);
    std::puts("hidden WGL: RGB mip upload, multiply pixels, visibility, state restore, release passed");
}
void Binary()
{
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    std::uint32_t op;while(std::fread(&op,4,1,stdin)==1){
        if(op==0){DecalVector normal;Read(normal);const auto basis=MakeCustomDecalBasis(normal);Write(basis);}
        else if(op==1){std::uint32_t count;DecalVector normal,plane;Read(count);Read(normal);Read(plane);Check(count<=64,"clip count");std::array<DecalVector,64> points;for(unsigned i=0;i<count;++i)Read(points[i]);std::array<DecalVector,70> output{};const auto size=std::uint32_t(ClipCustomDecal({points.data(),count},normal,plane,output));Write(size);for(unsigned i=0;i<size;++i)Write(output[i]);}
        else if(op==2){DecalVector a,b,normal,plane;Read(a);Read(b);Read(normal);Read(plane);Write(CustomDecalIntersection(a,b,normal,plane));}
        else if(op==3){std::array<std::uint8_t,18+64*64*3> image;Read(image);ShotDecalTexture rgb;Check(BuildShotDecalTexture(image,rgb),"texture decode");Write(rgb);}
        else if(op==4){DecalVector origin,normal;Read(origin);Read(normal);World world;CustomDecals decals;Random random;Check(decals.PrepareMap(world.model,false),"mesh fixture");decals.Receive(Message(origin[0],origin[1],origin[2],normal),1,random.Get());const auto view=decals.Inspect(0);Write(std::uint32_t(view.vertices.size()));for(const auto& vertex:view.vertices)Write(vertex);}
        else Check(false,"opcode");
    }
}
}
int main(int argc,char** argv){if(argc>1&&std::strcmp(argv[1],"--binary")==0)Binary();else{Lifecycle();GLPixels();}return 0;}
