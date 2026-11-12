// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/core_gl.hpp"
#include "../../native/guest/cof/client/custom_decal.hpp"
#include "../../native/guest/cof/client/point_light_render.hpp"
#include <metahook.h>
#include <com_model.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace guest::cof::client;
namespace
{
unsigned checks{};
void Check(bool value,const char* name){++checks;if(!value){std::fprintf(stderr,"FAIL %s error=%x\n",name,glGetError());std::exit(1);}}
struct World
{
    model_t model{};msurface_t surface{};mplane_t plane{};mtexinfo_t texinfo{};mnode_t node{},leaves[2]{};
    std::vector<std::byte> storage{offsetof(glpoly_t,verts)+4*7*sizeof(float)};std::uint8_t sample{};
    World()
    {
        auto* poly=reinterpret_cast<glpoly_t*>(storage.data());poly->numverts=4;
        const float points[4][3]{{-.75F,-.75F,0},{.75F,-.75F,0},{.75F,.75F,0},{-.75F,.75F,0}};
        for(unsigned i=0;i<4;++i){std::memcpy(poly->verts[i],points[i],12);poly->verts[i][3]=(points[i][0]+1)*.5F;poly->verts[i][4]=(points[i][1]+1)*.5F;}
        plane.normal[2]=1;plane.type=2;texinfo.vecs[0][0]=texinfo.vecs[1][1]=1;
        surface.plane=&plane;surface.texinfo=&texinfo;surface.polys=poly;surface.samples=&sample;surface.visframe=42;
        leaves[0].contents=leaves[1].contents=-1;node.plane=&plane;node.children[0]=&leaves[0];node.children[1]=&leaves[1];node.numsurfaces=1;node.visframe=77;
        for(unsigned i=0;i<3;++i){node.minmaxs[i]=-1;node.minmaxs[i+3]=1;}
        model.type=mod_brush;model.surfaces=&surface;model.numsurfaces=model.nummodelsurfaces=1;model.nodes=&node;
    }
};
using Pixel=std::array<unsigned char,4>;
std::array<Pixel,7> Run(bool core)
{
    const auto module=GetModuleHandleA(nullptr);WNDCLASSA wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcA;wc.hInstance=module;wc.lpszClassName=core?"CoFCoreRender":"CoFCompatRender";Check(RegisterClassA(&wc)!=0,"class");
    auto window=CreateWindowA(wc.lpszClassName,"",WS_POPUP,0,0,64,64,nullptr,nullptr,module,nullptr);auto dc=GetDC(window);
    PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cAlphaBits=8;pfd.cDepthBits=24;pfd.cStencilBits=8;
    int format=ChoosePixelFormat(dc,&pfd);Check(format&&SetPixelFormat(dc,format,&pfd),"format");auto context=wglCreateContext(dc);Check(context&&wglMakeCurrent(dc,context),"context");
    if(core){using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);auto create=core_gl::Proc<Create>("wglCreateContextAttribsARB");Check(create!=nullptr,"core creation");const int attrs[]{0x2091,4,0x2092,4,0x9126,1,0};auto next=create(dc,nullptr,attrs);Check(next!=nullptr,"core4.4");wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);context=next;Check(wglMakeCurrent(dc,context)!=0,"make core current");}
    core_gl::Api gl;Check(gl.Initialize(),"core GL API");Check(core_gl::IsCore()==core,"context profile");for(unsigned i=0;i<32&&glGetError()!=GL_NO_ERROR;++i){}
    const auto background=gl.Program("#version 330 core\nlayout(location=0) in vec3 p;uniform mat4 transform;void main(){gl_Position=transform*vec4(p,1);}","#version 330 core\nout vec4 c;void main(){c=vec4(.1,.1,.1,.7);}");Check(background!=0,"background shader");
    auto backgroundModel=core_gl::Identity;const auto transform=gl.uniform(background,"transform");
    GLuint vao{},buffer{};gl.genArrays(1,&vao);gl.genBuffers(1,&buffer);gl.bindArray(vao);gl.bindBuffer(core_gl::ArrayBuffer,buffer);const float quad[]{-.75F,-.75F,0,.75F,-.75F,0,.75F,.75F,0,-.75F,.75F,0};gl.bufferData(core_gl::ArrayBuffer,sizeof(quad),quad,core_gl::StaticDraw);gl.enableAttribute(0);gl.attribute(0,3,GL_FLOAT,GL_FALSE,12,nullptr);gl.bindArray(0);
    if(!core){glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();}
    World world;PointLightRenderer lights;Check(lights.PrepareMap(world.model,false,2)&&lights.InitializeGL(),"light preparation");
    PointLightFrame frame{};frame.stage=PointLightStage::OpaqueBeforeTransparency;frame.space=PointLightSpace::OriginalGamma;frame.matchingOpaqueDepth=frame.visibilityEstablished=true;frame.nodeVisframe=77;frame.surfaceVisframe=42;frame.time=1;frame.gamma={1,0,1};frame.authenticatedMatrices=true;frame.worldMatrix=frame.projectionMatrix=core_gl::Identity;
    PointLightState state{};state.lights[0].origin={0,0,.5F};state.lights[0].radius=1.5F;state.lights[0].color={1,.5F,.25F};state.lights[0].die=2;
    auto backgroundDraw=[&]{glViewport(0,0,64,64);glDisable(GL_DITHER);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDepthMask(GL_TRUE);glColorMask(1,1,1,1);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glClearColor(.1F,.1F,.1F,.7F);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);gl.use(background);gl.matrix(transform,1,GL_FALSE,backgroundModel.data());gl.bindArray(vao);glDrawArrays(GL_TRIANGLE_FAN,0,4);gl.bindArray(0);gl.use(0);};
    std::array<Pixel,7> pixels{};
    for(unsigned pass=0;pass<2;++pass){backgroundDraw();float depth{};glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);frame.forceTwoPass=pass?1.0F:0;frame.disableNvCombiners=1;Check(lights.Ready(frame),"light readiness");Check(lights.Draw(frame,state)==PointLightDrawResult::Drawn,"light draw");float after{};glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&after);Check(depth==after,"depth retained");glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[pass].data());Check(pixels[pass][0]>30&&pixels[pass][0]>pixels[pass][1]&&pixels[pass][1]>pixels[pass][2],"light RGB");Check(glGetError()==GL_NO_ERROR,"light GL error");}
    PointLightBrush brush{};brush.model=&world.model;brush.origin={.2F,-.1F,0};brush.localView={0,0,2};brush.up={0,0,1};brush.modelToWorld=core_gl::Identity;brush.modelToWorld[12]=.2F;brush.modelToWorld[13]=-.1F;world.model.radius=2;world.node.numsurfaces=0;frame.drawnBrushes={&brush,1};frame.glBmodels=1;frame.brushLifecycleEstablished=true;state.lights[0].origin={.2F,-.1F,.5F};
    for(unsigned rotation=0;rotation<2;++rotation)for(unsigned pass=0;pass<2;++pass){brush.rotated=rotation!=0;brush.forward=rotation?DecalVector{0,1,0}:DecalVector{1,0,0};brush.right=rotation?DecalVector{1,0,0}:DecalVector{0,-1,0};if(rotation){brush.modelToWorld[0]=brush.modelToWorld[5]=0;brush.modelToWorld[1]=1;brush.modelToWorld[4]=-1;}backgroundModel=brush.modelToWorld;backgroundDraw();if(!core){glMatrixMode(GL_MODELVIEW);glLoadIdentity();}frame.forceTwoPass=pass?1.0F:0;Check(lights.Ready(frame),"brush readiness");Check(lights.Draw(frame,state)==PointLightDrawResult::Drawn,"brush draw");const auto index=2+rotation*2+pass;glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[index].data());Check(pixels[index][0]>30&&pixels[index][0]>pixels[index][1]&&pixels[index][1]>pixels[index][2],"brush RGB");Check(glGetError()==GL_NO_ERROR,"brush GL error");}
    backgroundModel=core_gl::Identity;world.node.numsurfaces=1;frame.drawnBrushes={};frame.glBmodels=0;
    CustomDecals decals;std::vector<std::uint8_t> image(18+64*64*3,64);std::fill_n(image.begin(),18,0);image[2]=2;image[12]=image[14]=64;image[16]=24;image[17]=32;std::array<std::span<const std::uint8_t>,5> images;images.fill(image);Check(decals.InitializeGL(images),"decal textures");Check(decals.PrepareMap(world.model,false),"decal map");std::array<std::uint8_t,18> message{};message[10]=8;std::memcpy(message.data()+13,"shot",5);CustomDecalRandom random{nullptr,[](void*,int,int){return 0;}};Check(decals.Receive(message,1,random)==CustomDecalResult::Added,"decal message");
    CustomDecalFrame decalFrame{true,true,true,42};decalFrame.authenticatedMatrices=true;decalFrame.worldMatrix=decalFrame.projectionMatrix=core_gl::Identity;backgroundDraw();Check(decals.PrepareFrameGL(),"decal geometry");Check(decals.Draw(decalFrame,1)==CustomDecalDrawResult::Drawn,"decal draw");glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[6].data());Check(glGetError()==GL_NO_ERROR,"decal GL error");
    Check(decals.ReleaseGL()&&lights.ReleaseGL(),"release");gl.deleteBuffers(1,&buffer);gl.deleteArrays(1,&vao);gl.deleteProgram(background);Check(glGetError()==GL_NO_ERROR,"release GL error");wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassA(wc.lpszClassName,module);return pixels;
}
}
int main()
{
    const auto compatibility=Run(false),core=Run(true);
    for(unsigned pass=0;pass<7;++pass){for(unsigned channel=0;channel<3;++channel)Check(std::abs(int(compatibility[pass][channel])-int(core[pass][channel]))<=1,"core/compat RGB equivalence");std::printf("pass%u compat=%u,%u,%u core=%u,%u,%u\n",pass,compatibility[pass][0],compatibility[pass][1],compatibility[pass][2],core[pass][0],core[pass][1],core[pass][2]);}
    std::printf("checks=%u actualCoreAndCompatibility=yes gameplay=no performance=no\n",checks);
}
