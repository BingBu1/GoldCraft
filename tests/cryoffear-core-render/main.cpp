// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/core_gl.hpp"
#include "../../native/guest/cof/client/custom_decal.hpp"
#include "../../native/guest/cof/client/point_light_render.hpp"
#include "../../native/include/goldcraft/opaque_surface_mesh.hpp"
#include <metahook.h>
#include <com_model.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
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
struct BorrowedMesh final : IMetaRendererOpaqueMesh {
    model_t* world{};
    OpaqueSurfaceBuffers buffers{};
    bool Query(model_s* requested,OpaqueSurfaceBuffers& out,OpaqueSurfaceRange* ranges,
               std::uint32_t count) const noexcept override {
        if(requested!=world || count!=(ranges?1u:0u))return false;
        if(ranges)ranges[0]={6,6};
        out=buffers;
        return true;
    }
};

void TestSlopedDepth(core_gl::Api& gl)
{
    constexpr int side=64;
    constexpr GLenum elements=0x8893;
    constexpr float cx=733.375f,cy=-924.4375f,cz=271.625f;
    constexpr float half=66,nearZ=4.1f,farZ=4096;
    constexpr float sy=.5f,cosY=.8660254f,sx=.34202015f,cosX=.9396926f;
    const std::array<float,16> view{
        cosY,sx*sy,-cosX*sy,0, 0,cosX,sx,0, sy,-sx*cosY,cosX*cosY,0,
        -(cosY*cx+sy*cz),-(sx*sy*cx+cosX*cy-sx*cosY*cz),
        -(-cosX*sy*cx+sx*cy+cosX*cosY*cz)-151.97f,1};
    const std::array<float,16> projection{1.57f,0,0,0,0,1.57f,0,0,0,0,
        -(farZ+nearZ)/(farZ-nearZ),-1,0,0,-2*farZ*nearZ/(farZ-nearZ),0};
    World world;
    std::array<std::array<float,7>,8> vertices{};
    for(unsigned i=0;i<4;++i) {
        const float x=i==1||i==2?1.f:-1.f,y=i>=2?1.f:-1.f;
        vertices[i]={cx+x*half,cy+y*half,cz,x,y,0,0};
        vertices[i+4]={cx+x*14.f,cy+y*19.f,cz+26.f,x,y,0,0};
        std::memcpy(world.surface.polys->verts[i],vertices[i].data(),sizeof(vertices[i]));
    }
    world.plane.dist=cz;
    for(int i=0;i<3;++i){world.node.minmaxs[i]=-1500;world.node.minmaxs[i+3]=1500;}
    const std::array<unsigned,12> indices{4,5,6,4,6,7, 3,0,1,1,2,3};
    BorrowedMesh mesh;mesh.world=&world.model;mesh.buffers.indexCount=unsigned(indices.size());
    GLuint hostVao{};gl.genArrays(1,&hostVao);gl.bindArray(hostVao);
    gl.genBuffers(1,&mesh.buffers.vertex);gl.bindBuffer(core_gl::ArrayBuffer,mesh.buffers.vertex);
    gl.bufferData(core_gl::ArrayBuffer,sizeof(vertices),vertices.data(),core_gl::StaticDraw);
    gl.genBuffers(1,&mesh.buffers.index);gl.bindBuffer(elements,mesh.buffers.index);
    gl.bufferData(elements,sizeof(indices),indices.data(),core_gl::StaticDraw);
    gl.enableAttribute(0);gl.attribute(0,3,GL_FLOAT,GL_FALSE,sizeof(vertices[0]),nullptr);
    constexpr auto hostVertex=R"(#version 330 core
layout(location=0) in vec3 position; uniform mat4 projection,view,model;
void main(){vec4 worldpos=model*vec4(position,1);gl_Position=projection*view*worldpos;})";
    constexpr auto oldVertex=R"(#version 330 core
layout(location=0) in vec3 position; uniform mat4 projection,view,model;
void main(){gl_Position=projection*(view*(model*vec4(position,1)));})";
    constexpr auto fragment=R"(#version 330 core
uniform int object;out vec4 color;
void main(){color=object==0?vec4(.125,.125,.125,1):vec4(.1,.4,.2,1);})";
    const GLuint host=gl.Program(hostVertex,fragment),old=gl.Program(oldVertex,fragment);
    Check(host&&old,"sloped programs");
    for(GLuint program:{host,old}){
        gl.use(program);
        gl.matrix(gl.uniform(program,"view"),1,GL_FALSE,view.data());
        gl.matrix(gl.uniform(program,"projection"),1,GL_FALSE,projection.data());
        gl.matrix(gl.uniform(program,"model"),1,GL_FALSE,core_gl::Identity.data());
    }
    const auto hostObject=gl.uniform(host,"object");
    const auto drawScene=[&]{
        glViewport(0,0,side,side);glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glColorMask(1,1,1,1);
        glClearColor(0,0,0,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        gl.use(host);gl.bindArray(hostVao);gl.integer(hostObject,0);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,reinterpret_cast<const void*>(6*sizeof(unsigned)));
        gl.integer(hostObject,1);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);
    };
    std::array<Pixel,side*side> before{},after{};
    std::array<unsigned,side*side> depthBefore{},depthAfter{};
    const auto readColor=[&](auto& data){glReadPixels(0,0,side,side,GL_RGBA,GL_UNSIGNED_BYTE,data.data());};
    const auto readDepth=[&](auto& data){glReadPixels(0,0,side,side,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,data.data());};
    drawScene();readColor(before);readDepth(depthBefore);
    gl.use(old);gl.integer(gl.uniform(old,"object"),1);glDepthFunc(GL_EQUAL);glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLE_FAN,0,4);readColor(after);
    unsigned wall=0,obstruction=0,oldMissing=0;
    for(std::size_t i=0;i<before.size();++i){
        if(before[i][0]>0 && before[i][0]==before[i][1]){
            ++wall;if(after[i]==before[i])++oldMissing;
        }else if(before[i][1]>before[i][0])++obstruction;
    }
    Check(wall>500 && obstruction>50,"sloped fixture has exposed and occluded geometry");
    std::printf("sloped pixels=%u occluder=%u previousFanMissing=%u\n",wall,obstruction,oldMissing);
    PointLightRenderer renderer;
    Check(renderer.PrepareMap(world.model,false,2,&mesh)&&renderer.InitializeGL(),"sloped borrowed map");
    PointLightFrame frame{};frame.stage=PointLightStage::OpaqueBeforeTransparency;
    frame.space=PointLightSpace::OriginalGamma;frame.matchingOpaqueDepth=frame.visibilityEstablished=true;
    frame.authenticatedMatrices=true;frame.worldMatrix=view;frame.projectionMatrix=projection;
    frame.nodeVisframe=77;frame.surfaceVisframe=42;frame.time=1;frame.gamma={1,0,1};
    PointLightState state{};
    state.lights[0].origin={cx,cy,cz+44};state.lights[0].radius=220;
    state.lights[0].color={.4f,.4f,.4f};state.lights[0].die=2;
    for(int pass=0;pass<2;++pass){
        drawScene();frame.forceTwoPass=float(pass);
        Check(renderer.Ready(frame)&&renderer.Draw(frame,state)==PointLightDrawResult::Drawn,"sloped draw");
        readColor(after);readDepth(depthAfter);
        unsigned missing=0,leaks=0;
        for(std::size_t i=0;i<before.size();++i){
            if(before[i][0]>0 && before[i][0]==before[i][1]){
                if(after[i][0]<=before[i][0])++missing;
            }else if(after[i]!=before[i])++leaks;
        }
        std::printf("sloped pass=%d unlit=%u foregroundOrSkyChanged=%u depthChanged=%d\n",
                    pass,missing,leaks,depthAfter!=depthBefore);
        Check(!missing&&!leaks&&depthAfter==depthBefore,"continuous sloped light with foreground occlusion");
    }
    renderer.ClearMap();Check(renderer.ReleaseGL(),"release borrowed map");
    drawScene();readColor(after);Check(after==before && glGetError()==GL_NO_ERROR,"host retains both buffers");
    gl.bindArray(0);gl.use(0);gl.deleteArrays(1,&hostVao);gl.deleteBuffers(1,&mesh.buffers.index);
    gl.deleteBuffers(1,&mesh.buffers.vertex);gl.deleteProgram(old);gl.deleteProgram(host);
}
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
    Check(decals.ReleaseGL()&&lights.ReleaseGL(),"release");gl.deleteBuffers(1,&buffer);gl.deleteArrays(1,&vao);gl.deleteProgram(background);
    if(core)TestSlopedDepth(gl);
    Check(glGetError()==GL_NO_ERROR,"release GL error");wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassA(wc.lpszClassName,module);return pixels;
}
}
int main()
{
    const auto compatibility=Run(false),core=Run(true);
    for(unsigned pass=0;pass<7;++pass){for(unsigned channel=0;channel<3;++channel)Check(std::abs(int(compatibility[pass][channel])-int(core[pass][channel]))<=1,"core/compat RGB equivalence");std::printf("pass%u compat=%u,%u,%u core=%u,%u,%u\n",pass,compatibility[pass][0],compatibility[pass][1],compatibility[pass][2],core[pass][0],core[pass][1],core[pass][2]);}
    std::printf("checks=%u actualCoreAndCompatibility=yes gameplay=no performance=no\n",checks);
}
