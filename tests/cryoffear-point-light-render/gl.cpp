// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/point_light_render.hpp"
#include <metahook.h>
#include <com_model.h>
#include <GL/gl.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace guest::cof::client;
namespace {unsigned checks=0;void Check(bool value){++checks;if(!value){std::fprintf(stderr,"GL check failed %u error=%x\n",checks,glGetError());std::exit(1);}}}
int main(){
 WNDCLASSA wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="CofPointLightProbe";Check(RegisterClassA(&wc)!=0);
 HWND window=CreateWindowA(wc.lpszClassName,"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);Check(window!=nullptr);HDC dc=GetDC(window);
 PIXELFORMATDESCRIPTOR pixel{};pixel.nSize=sizeof(pixel);pixel.nVersion=1;pixel.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pixel.iPixelType=PFD_TYPE_RGBA;pixel.cColorBits=32;pixel.cAlphaBits=8;pixel.cDepthBits=24;pixel.cStencilBits=8;
 const int format=ChoosePixelFormat(dc,&pixel);Check(format&&SetPixelFormat(dc,format,&pixel));HGLRC context=wglCreateContext(dc);Check(context&&wglMakeCurrent(dc,context));
 std::fprintf(stderr,"GL renderer: %s; version: %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
 struct Polygon {glpoly_t poly;float extra[3][7];} polygon{};polygon.poly.numverts=4;
 const float points[4][3]{{-.75F,-.75F,0},{.75F,-.75F,0},{.75F,.75F,0},{-.75F,.75F,0}};
 for(unsigned i=0;i<4;++i){auto* target=reinterpret_cast<float*>(reinterpret_cast<char*>(&polygon.poly)+offsetof(glpoly_t,verts))+i*7;std::memcpy(target,points[i],12);target[3]=(points[i][0]+1)*.5F;target[4]=(points[i][1]+1)*.5F;}
 mplane_t plane{};plane.normal[2]=1;plane.type=2;mtexinfo_t texinfo{};texinfo.vecs[0][0]=1;texinfo.vecs[1][1]=1;
 msurface_t surface{};surface.visframe=42;surface.plane=&plane;surface.polys=&polygon.poly;surface.texinfo=&texinfo;
 mnode_t leaves[2]{};for(auto& leaf:leaves)leaf.contents=-1;
 mnode_t node{};node.visframe=77;node.plane=&plane;node.children[0]=&leaves[0];node.children[1]=&leaves[1];node.numsurfaces=1;
 for(unsigned i=0;i<3;++i){node.minmaxs[i]=-1;node.minmaxs[i+3]=1;}
 model_t model{};model.type=mod_brush;model.numsurfaces=1;model.surfaces=&surface;model.nodes=&node;
 PointLightRenderer renderer;Check(renderer.PrepareMap(model,false,1));Check(renderer.InitializeGL());
 PointLightFrame frame{};frame.stage=PointLightStage::OpaqueBeforeTransparency;frame.space=PointLightSpace::OriginalGamma;frame.matchingOpaqueDepth=frame.visibilityEstablished=true;frame.nodeVisframe=77;frame.surfaceVisframe=42;frame.time=1;frame.gamma={1,0,1};
 Check(renderer.Ready(frame));PointLightState lights{};lights.lights[0].origin={0,0,.5F};lights.lights[0].radius=1.5F;lights.lights[0].color={1,.5F,.25F};lights.lights[0].die=2;
 glViewport(0,0,64,64);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(-1,1,-1,1,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_DITHER);
 std::array<std::array<unsigned char,4>,3> pixels{};
 const bool nv=std::strstr(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)),"GL_NV_register_combiners")!=nullptr;
 using ActiveFn=void(APIENTRY*)(GLenum);auto active=reinterpret_cast<ActiveFn>(wglGetProcAddress("glActiveTexture"));Check(active!=nullptr);
 active(0x84C2);glMatrixMode(GL_TEXTURE);glLoadIdentity();glScalef(3,5,7);std::array<float,16> retained{},restored{};glGetFloatv(GL_TEXTURE_MATRIX,retained.data());active(0x84C0);glMatrixMode(GL_MODELVIEW);
 using NvParam=void(APIENTRY*)(GLenum,GLint);auto nvParam=reinterpret_cast<NvParam>(wglGetProcAddress("glCombinerParameteriNV"));if(nv&&nvParam)nvParam(0x854E,2);
 for(unsigned pass=0;pass<3;++pass){
  glDisable(GL_STENCIL_TEST);glDepthMask(1);glColorMask(1,1,1,1);glClearColor(.1F,.1F,.1F,.7F);glClearDepth(1);glClearStencil(71);glStencilMask(255);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
  glColor3f(.1F,.1F,.1F);glBegin(GL_QUADS);for(const auto& point:points)glVertex3fv(point);glEnd();
  float depthBefore=0,depthAfter=0;unsigned char stencilBefore=0,stencilAfter=0;
  glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depthBefore);glReadPixels(32,32,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencilBefore);
  glDepthFunc(GL_LEQUAL);glDepthMask(1);glColor4f(1,1,1,1);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.01F);glEnable(GL_STENCIL_TEST);glStencilFunc(GL_ALWAYS,123,255);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);glStencilMask(255);
  frame.forceTwoPass=pass?1.0F:0.0F;frame.disableNvCombiners=pass==2?0.0F:1.0F;Check(renderer.Draw(frame,lights)==PointLightDrawResult::Drawn);
  GLint depthFunc=0,src=0,dst=0;GLboolean depthMask=0;glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetIntegerv(GL_BLEND_SRC,&src);glGetIntegerv(GL_BLEND_DST,&dst);
  Check(depthFunc==GL_LEQUAL&&depthMask&&src==GL_SRC_ALPHA&&dst==GL_ONE_MINUS_SRC_ALPHA&&glIsEnabled(GL_ALPHA_TEST));
  glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depthAfter);glReadPixels(32,32,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencilAfter);Check(depthBefore==depthAfter&&stencilBefore==stencilAfter&&stencilAfter==71);
  glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[pass].data());GLint stencilMask=0;glGetIntegerv(GL_STENCIL_WRITEMASK,&stencilMask);Check(stencilMask==255&&glIsEnabled(GL_STENCIL_TEST));std::fprintf(stderr,"pass %u rgba %u %u %u %u\n",pass,pixels[pass][0],pixels[pass][1],pixels[pass][2],pixels[pass][3]);Check(pixels[pass][0]>30&&pixels[pass][0]>pixels[pass][1]&&pixels[pass][1]>pixels[pass][2]);Check(glGetError()==GL_NO_ERROR);active(0x84C2);glGetFloatv(GL_TEXTURE_MATRIX,restored.data());Check(retained==restored);active(0x84C0);if(nv&&nvParam){GLint combiners=0;glGetIntegerv(0x854E,&combiners);Check(combiners==2);}
 }

 auto Unlit=[&](){glDepthMask(1);glColorMask(1,1,1,1);glDisable(GL_ALPHA_TEST);glClearColor(.1F,.1F,.1F,.7F);glClear(GL_COLOR_BUFFER_BIT);Check(renderer.Draw(frame,lights)==PointLightDrawResult::Drawn);std::array<unsigned char,4> pixel{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());Check(pixel[0]==pixel[1]&&pixel[1]==pixel[2]&&pixel[0]<30);};
 frame.forceTwoPass=0;frame.disableNvCombiners=1;
 frame.nodeVisframe=76;Unlit();frame.nodeVisframe=77;
 frame.surfaceVisframe=41;Unlit();frame.surfaceVisframe=42;
 surface.flags=2;Unlit();surface.flags=0;
 node.contents=-2;Unlit();node.contents=0;
 for(unsigned i=0;i<3;++i){node.minmaxs[i]=100;node.minmaxs[i+3]=101;}Unlit();for(unsigned i=0;i<3;++i){node.minmaxs[i]=-1;node.minmaxs[i+3]=1;}
 lights.lights[0].die=.5F;Check(renderer.Draw(frame,lights)==PointLightDrawResult::NoLights);lights.lights[0].die=2;
 lights.lights[0].rendererFields[20]=std::byte{1};Check(renderer.Draw(frame,lights)==PointLightDrawResult::UnsupportedLight);lights.lights[0].rendererFields[20]=std::byte{};
 cl_entity_t brush{};brush.model=&model;model.firstmodelsurface=0;model.nummodelsurfaces=1;cl_entity_t* brushes[]{&brush};frame.visibleBrushes=brushes;frame.glBmodels=1;
 Check(!renderer.Ready(frame));Check(renderer.Draw(frame,lights)==PointLightDrawResult::UnsupportedBrushLifecycle);frame.brushLifecycleEstablished=true;Check(renderer.Ready(frame));
 // The same surface appears through world and static brush: refusal occurs
 // before the first light draw when the caller's cold capacity is insufficient.
 Check(renderer.Draw(frame,lights)==PointLightDrawResult::CapacityExceeded);
 node.numsurfaces=0;glColor4f(1,1,1,1);Check(renderer.Draw(frame,lights)==PointLightDrawResult::Drawn);
 for(unsigned axis=0;axis<3;++axis){brush.origin[axis]=1;Check(!renderer.Ready(frame));Check(renderer.Draw(frame,lights)==PointLightDrawResult::UnsupportedBrushLifecycle);brush.origin[axis]=0;brush.angles[axis]=1;Check(!renderer.Ready(frame));brush.angles[axis]=0;}
 for(int fx:{70,137,184}){brush.curstate.renderfx=fx;Check(!renderer.Ready(frame));}brush.curstate.renderfx=0;
 frame.visibleBrushes={};frame.glBmodels=0;node.numsurfaces=1;
 // Actual opaque-depth replay for translated and rotated brush snapshots.
 // World PVS has no brush stamp; the exact submitted model and local view own
 // this separate branch. Both attenuation paths must preserve camera/depth.
 PointLightBrush submitted{};submitted.model=&model;submitted.origin={.2F,-.1F,.15F};
 submitted.localView={0,0,2};submitted.forward={1,0,0};submitted.right={0,-1,0};submitted.up={0,0,1};
 submitted.modelToWorld={1,0,0,0,0,1,0,0,0,0,1,0,.2F,-.1F,.15F,1};
 model.radius=2;for(unsigned i=0;i<3;++i){model.mins[i]=-1;model.maxs[i]=1;}
 frame.drawnBrushes={&submitted,1};frame.glBmodels=1;node.numsurfaces=0;surface.visframe=0;
 for(unsigned rotation=0;rotation<2;++rotation)for(unsigned pass=0;pass<3;++pass) {
  submitted.rotated=rotation!=0;
  if(rotation){submitted.forward={0,1,0};submitted.right={1,0,0};submitted.modelToWorld={0,1,0,0,-1,0,0,0,0,0,1,0,.2F,-.1F,.15F,1};}
  lights.lights[0].origin={.2F,-.1F,.65F};frame.forceTwoPass=pass?1.0F:0;frame.disableNvCombiners=pass==2?0.0F:1.0F;
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_STENCIL_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);
  glColorMask(1,1,1,1);glDepthMask(1);glDepthFunc(GL_LEQUAL);glClearColor(.1F,.1F,.1F,.7F);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  glPushMatrix();glMultMatrixf(submitted.modelToWorld.data());glColor4f(.1F,.1F,.1F,1);glBegin(GL_QUADS);for(const auto& point:points)glVertex3fv(point);glEnd();glPopMatrix();
  float beforeDepth{},afterDepth{};std::array<float,16> beforeCamera{},afterCamera{};
  glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&beforeDepth);glGetFloatv(GL_MODELVIEW_MATRIX,beforeCamera.data());
  Check(renderer.Ready(frame));Check(renderer.Draw(frame,lights)==PointLightDrawResult::Drawn);
  glMatrixMode(GL_MODELVIEW);glGetFloatv(GL_MODELVIEW_MATRIX,afterCamera.data());glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&afterDepth);
  std::array<unsigned char,4> lit{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,lit.data());
  Check(beforeCamera==afterCamera&&beforeDepth==afterDepth);Check(lit[0]>30&&lit[0]>lit[1]&&lit[1]>lit[2]);Check(glGetError()==GL_NO_ERROR);
 }
 submitted.renderfx=70;Check(!renderer.Ready(frame));submitted.renderfx=0;
 frame.brushLifecycleEstablished=false;Check(!renderer.Ready(frame));frame.brushLifecycleEstablished=true;
 submitted.localView={0,0,-2};glColorMask(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
 Check(renderer.Draw(frame,lights)==PointLightDrawResult::Drawn);std::array<unsigned char,4> hidden{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,hidden.data());Check(hidden[0]==hidden[1]&&hidden[1]==hidden[2]);
 frame.drawnBrushes={};frame.glBmodels=0;node.numsurfaces=1;surface.visframe=42;lights.lights[0].origin={0,0,.5F};
 // Real HL25 108-byte stride: surface0 is skipped, surface1 remains drawable.
 std::array<msurface_hl25_t,2> hl25{};std::memcpy(&hl25[0],&surface,sizeof(surface));std::memcpy(&hl25[1],&surface,sizeof(surface));hl25[0].flags=4;
 model.surfaces=reinterpret_cast<msurface_t*>(hl25.data());model.numsurfaces=2;node.firstsurface=1;Check(renderer.PrepareMap(model,true,1));Check(renderer.Ready(frame));Check(renderer.Draw(frame,lights)==PointLightDrawResult::Drawn);
 model.surfaces=&surface;model.numsurfaces=1;node.firstsurface=0;Check(renderer.PrepareMap(model,false,1));
 Check(surface.polys==&polygon.poly&&polygon.poly.next==nullptr&&polygon.poly.flags==0);frame.space=PointLightSpace::Unsupported;Check(!renderer.Ready(frame));Check(renderer.Draw(frame,lights)==PointLightDrawResult::UnsupportedScene);
 using GenVao=void(APIENTRY*)(GLsizei,GLuint*);using BindVao=void(APIENTRY*)(GLuint);using DelVao=void(APIENTRY*)(GLsizei,const GLuint*);auto genVao=reinterpret_cast<GenVao>(wglGetProcAddress("glGenVertexArrays"));auto bindVao=reinterpret_cast<BindVao>(wglGetProcAddress("glBindVertexArray"));auto delVao=reinterpret_cast<DelVao>(wglGetProcAddress("glDeleteVertexArrays"));if(genVao&&bindVao&&delVao){GLuint hostVao=0;genVao(1,&hostVao);bindVao(hostVao);frame.space=PointLightSpace::OriginalGamma;Check(!renderer.Ready(frame));Check(renderer.Draw(frame,lights)==PointLightDrawResult::UnsupportedScene);bindVao(0);delVao(1,&hostVao);}
 Check(renderer.ReleaseGL());wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);UnregisterClassA(wc.lpszClassName,wc.hInstance);
 std::printf("{\"checks\":%u,\"fastRgb\":[%u,%u,%u],\"multipassRgb\":[%u,%u,%u],\"nvExtension\":%s,\"nvOrFallbackRgb\":[%u,%u,%u],\"gameplay\":false,\"performance\":false}\n",checks,pixels[0][0],pixels[0][1],pixels[0][2],pixels[1][0],pixels[1][1],pixels[1][2],nv?"true":"false",pixels[2][0],pixels[2][1],pixels[2][2]);
}
