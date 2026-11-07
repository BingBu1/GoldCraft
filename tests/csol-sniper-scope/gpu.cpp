// SPDX-License-Identifier: GPL-3.0-or-later
// Actual scope renderer, real WGL driver and original instruction draw oracle.
#include "../../native/csol/client/weapons/sniper_scope.cpp"
#include <rapidjson/document.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <bit>

namespace fixture {
std::filesystem::path root;
int checks{},failures{},reads{},logs{},cachedTexture=919;
bool missing{};
void Check(bool value,const char* label,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<label<<'\n';}}
}
namespace csol::client {
cl_enginefunc_t engine{};bool glewReady{};int* hostTextureCache=&fixture::cachedTexture;
std::vector<std::byte> ReadResource(const char* path)
{
 ++fixture::reads;if(fixture::missing)throw std::runtime_error("controlled missing scope arc");
 std::ifstream file(fixture::root/path,std::ios::binary|std::ios::ate);
 if(!file)throw std::runtime_error(path);const auto size=file.tellg();
 std::vector<std::byte> bytes(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);return bytes;
}
bool EnsureGraphics(){return glewReady&&wglGetCurrentContext();}
}
namespace {
using namespace fixture;using namespace csol::client;
void Log(const char*,...){++logs;}
struct Window {
 HWND hwnd{};HDC dc{};HGLRC rc{};
 Window(bool core){
  WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScopeHiddenFixture";RegisterClassW(&cls);
  hwnd=CreateWindowW(cls.lpszClassName,L"scope hidden fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
  if(!hwnd)throw std::runtime_error("CreateWindow");dc=GetDC(hwnd);
  PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cAlphaBits=8;p.cDepthBits=24;
  if(!SetPixelFormat(dc,ChoosePixelFormat(dc,&p),&p))throw std::runtime_error("SetPixelFormat");
  rc=wglCreateContext(dc);if(!rc||!wglMakeCurrent(dc,rc))throw std::runtime_error("WGL context");
  glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW");
  if(core){
   using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);auto create=reinterpret_cast<Create>(wglGetProcAddress("wglCreateContextAttribsARB"));
   if(!create)throw std::runtime_error("WGL core creation unavailable");const int attributes[]{0x2091,3,0x2092,3,0x9126,1,0};
   auto next=create(dc,nullptr,attributes);if(!next)throw std::runtime_error("WGL core creation failed");wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc);rc=next;wglMakeCurrent(dc,rc);glewInit();
  }
  if(!GLEW_VERSION_3_3)throw std::runtime_error("OpenGL 3.3 unavailable");glewReady=true;while(glGetError()!=GL_NO_ERROR){}
 }
 ~Window(){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc);ReleaseDC(hwnd,dc);DestroyWindow(hwnd);glewReady=false;}
};
bool Compatibility(){GLint flags{};glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&flags);return(flags&GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)!=0;}
struct State{
 std::vector<GLint> ints;std::vector<GLboolean> bools;int cached=cachedTexture;
 State(){
  for(GLenum key:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ARRAY_BUFFER_BINDING,GL_ACTIVE_TEXTURE,GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA,GL_CULL_FACE_MODE,GL_FRONT_FACE,GL_DEPTH_FUNC,GL_PIXEL_UNPACK_BUFFER_BINDING,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS}){GLint value{};glGetIntegerv(key,&value);ints.push_back(value);}
  const GLint active=ints[3];for(int unit=0;unit<2;++unit){glActiveTexture(GL_TEXTURE0+unit);for(GLenum key:{GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_CUBE_MAP,GL_SAMPLER_BINDING}){GLint value{};glGetIntegerv(key,&value);ints.push_back(value);}}glActiveTexture(active);
  for(GLenum key:{GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST})bools.push_back(glIsEnabled(key));
  GLboolean depth{},mask[4]{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);glGetBooleanv(GL_COLOR_WRITEMASK,mask);bools.push_back(depth);bools.insert(bools.end(),mask,mask+4);
  if(Compatibility()){bools.push_back(glIsEnabled(GL_ALPHA_TEST));GLint f{};GLfloat ref{};glGetIntegerv(GL_ALPHA_TEST_FUNC,&f);glGetFloatv(GL_ALPHA_TEST_REF,&ref);ints.push_back(f);ints.push_back(std::bit_cast<int>(ref));}
 }
 bool operator==(const State& other)const{return ints==other.ints&&bools==other.bools&&cached==other.cached;}
};
struct Target{
 GLuint texture{},fbo{};int w,h;
 Target(int width,int height):w(width),h(height){glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"complete framebuffer");glViewport(0,0,w,h);}
 void Clear(){glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);}
 std::array<unsigned char,4> Pixel(int x,int y){std::array<unsigned char,4> p{};glReadPixels(x,h-1-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
 ~Target(){glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);}
};
void Geometry(const std::filesystem::path& oracle){
 std::ifstream f(oracle);std::string text{std::istreambuf_iterator<char>(f),{}};rapidjson::Document doc;doc.Parse(text.c_str());if(doc.HasParseError())throw std::runtime_error("oracle JSON");
 Check(std::string(doc["inputSha256"].GetString())=="f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd","matching original module");
 int cases{};
 for(const auto& row:doc["cases"].GetArray()){
  if(row["weaponId"].GetInt()!=112||row["capability"].GetInt()!=1||row["fov"].GetInt()>=90)continue;
  ++cases;Target target(row["width"].GetInt(),row["height"].GetInt());target.Clear();
  const State before;DrawSniperScope(target.w,target.h,row["gap"].GetInt());Check(State{}==before,"all host GL state restored");
  Check(program&&vao&&vbo&&arc,"actual renderer initialized");
  std::array<float,288> actual{};glBindBuffer(GL_ARRAY_BUFFER,vbo);glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(actual),actual.data());glBindBuffer(GL_ARRAY_BUFFER,before.ints[2]);
  const auto& draws=row["draws"].GetArray();Check(draws.Size()==12,"twelve original draw calls");
  for(unsigned q=0;q<draws.Size();++q){const auto& draw=draws[q];std::array<std::array<float,4>,4> expected{};
   if(draw.HasMember("vertices")){for(unsigned k=0;k<4;++k)for(unsigned c=0;c<4;++c)expected[k][c]=draw["vertices"][k][c].GetFloat();}
   else{const auto& r=draw["rect"];expected={{{r[0].GetFloat(),r[1].GetFloat(),0,0},{r[2].GetFloat(),r[1].GetFloat(),0,0},{r[2].GetFloat(),r[3].GetFloat(),0,0},{r[0].GetFloat(),r[3].GetFloat(),0,0}}};}
   const int indices[]{0,1,2,0,2,3};for(int k=0;k<6;++k)for(int c=0;c<4;++c)Check(actual[(q*6+k)*4+c]==expected[indices[k]][c],"actual uploaded vertex matches original instruction oracle");
  }
  Check(target.Pixel(5,5)[0]==0,"outer mask visible");
  if(row["gap"].GetInt()>0)Check(target.Pixel(target.w/2,target.h/2)[0]==255,"scope center remains open for engine SPR");
  Check(glGetError()==GL_NO_ERROR,"no GL errors");
 }
 Check(cases==50,"all fifty high-capability SKULL5 cases");
}
void ContaminatedState(){
 Target target(1280,720);target.Clear();
 glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
 if(Compatibility()){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.99f);}
 const State before;DrawSniperScope(1280,720,15);Check(State{}==before,"inherited clipped/color/alpha state restored");
 const auto p=target.Pixel(5,5);Check(p[0]==0&&p[1]==0&&p[2]==0,"inherited state cannot clip scope mask");
 glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);if(Compatibility())glDisable(GL_ALPHA_TEST);
}
void FailureAndCleanup(){
 ResetSniperScope();missing=true;int old=logs;DrawSniperScope(800,600,10);Check(logs==old+1&&!program&&!vao&&!vbo&&!arc,"missing image frees partial renderer");
 const auto loaded=reads;DrawSniperScope(800,600,10);Check(reads==loaded,"failed load does not retry every frame");missing=false;ResetSniperScope();DrawSniperScope(800,600,10);
 const auto p=program,v=vao,b=vbo,t=arc;Check(p&&v&&b&&t,"reset permits recovered resource");glewReady=false;ResetSniperScope();glewReady=true;
 Check(!glIsProgram(p)&&!glIsVertexArray(v)&&!glIsBuffer(b)&&!glIsTexture(t),"owner cleanup independent of shared glewReady reset");ResetSniperScope();Check(glGetError()==GL_NO_ERROR,"idempotent cleanup");
}
void ContextOwnership(){
 const auto oldDc=wglGetCurrentDC();const auto oldRc=wglGetCurrentContext();
 DrawSniperScope(800,600,10);const auto texture=arc,shader=program,array=vao,buffer=vbo;
 {Window other(false);glBindTexture(GL_TEXTURE_2D,texture);Check(glIsTexture(texture),"new independent context owns equal numeric name");ResetSniperScope();Check(glIsTexture(texture),"reset does not delete other context resource");glDeleteTextures(1,&texture);}
 wglMakeCurrent(oldDc,oldRc);glewReady=true;Check(glIsTexture(texture)&&glIsProgram(shader),"original context resources remain untouched");
 glDeleteTextures(1,&texture);glDeleteProgram(shader);glDeleteVertexArrays(1,&array);glDeleteBuffers(1,&buffer);
}
struct ShaderFault{
 static inline ShaderFault* current{};PFNGLSHADERSOURCEPROC saved=__glewShaderSource;std::vector<GLuint> shaders;
 ShaderFault(){current=this;__glewShaderSource=Source;}
 static void GLAPIENTRY Source(GLuint shader,GLsizei count,const GLchar*const* text,const GLint* lengths){auto& f=*current;f.shaders.push_back(shader);GLint kind{};glGetShaderiv(shader,GL_SHADER_TYPE,&kind);const char* bad="#version 330 core\nthis_is_invalid";if(kind==GL_FRAGMENT_SHADER)f.saved(shader,1,&bad,nullptr);else f.saved(shader,count,text,lengths);}
 ~ShaderFault(){__glewShaderSource=saved;current=nullptr;}
};
void ShaderFailure(){ResetSniperScope();const auto old=logs;{ShaderFault fault;DrawSniperScope(800,600,10);Check(logs==old+1&&!program&&!vao&&!vbo&&!arc,"real shader compilation failure clears partial objects");Check(fault.shaders.size()==2,"shader failure reached real driver");for(auto shader:fault.shaders)Check(!glIsShader(shader),"failed shaders deleted");}ResetSniperScope();DrawSniperScope(800,600,10);Check(program&&arc,"shader failure reset recovers");ResetSniperScope();}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=4)return 2;root=argv[1];Window window(std::wstring_view(argv[3])==L"core");engine.Con_DPrintf=Log;Geometry(argv[2]);ContaminatedState();FailureAndCleanup();ShaderFailure();ContextOwnership();ResetSniperScope();std::cout<<"scope actual GL "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
