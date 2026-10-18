#include "../../native/client/entity_outline.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <new>

namespace probe {
bool active=false;std::size_t allocations=0;unsigned storages=0,fail_storage=0;
PFNGLTEXSTORAGE2DPROC original_storage;
void APIENTRY storage(GLenum target,GLsizei levels,GLenum format,GLsizei w,GLsizei h){
    ++storages;if(fail_storage&&!--fail_storage)return;original_storage(target,levels,format,w,h);
}
}
void* operator new(std::size_t size){if(probe::active)++probe::allocations;if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

using goldcraft::render::EntityOutline;
namespace {
using Pixel=std::array<float,4>;
std::uint64_t checks=0;
void check(bool value){++checks;assert(value);}
GLuint program(const std::string& vs,const std::string& fs){
    GLuint shaders[2]{};unsigned i=0;
    for(const auto& source:{vs,fs}){
        auto s=shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);++i;
        const auto* p=source.c_str();glShaderSource(s,1,&p,nullptr);glCompileShader(s);
        GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);std::puts(log);}check(ok);
    }
    auto p=glCreateProgram();for(auto s:shaders)glAttachShader(p,s);glBindAttribLocation(p,0,"Position");
    glBindFragDataLocation(p,0,"fragColor");glLinkProgram(p);for(auto s:shaders)glDeleteShader(s);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);check(ok);return p;
}
std::string read(const std::string& name){std::ifstream f(name);check(bool(f));return {std::istreambuf_iterator<char>(f),{}};}
void clean(){
    for(auto cap:{GL_DEPTH_TEST,GL_STENCIL_TEST,GL_BLEND,GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_POLYGON_OFFSET_FILL})glDisable(cap);
    for(unsigned i=0;i<8;++i)glDisable(GL_CLIP_DISTANCE0+i);
    glColorMask(1,1,1,1);glDepthMask(1);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindSampler(1,0);
}
struct Target {
    GLuint fbo{},color{},depth{},view{},extra{};unsigned width,height,format;
    Target(unsigned w,unsigned h,unsigned fmt=GL_RGBA8,bool host=false):width(w),height(h),format(fmt){
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
        glTexStorage2D(GL_TEXTURE_2D,1,fmt,w,h);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        if(host){
            glGenTextures(1,&depth);glBindTexture(GL_TEXTURE_2D,depth);glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH24_STENCIL8,w,h);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth,0);
            glGenTextures(1,&view);glTextureView(view,GL_TEXTURE_2D,depth,GL_DEPTH24_STENCIL8,0,1,0,1);
            glGenTextures(1,&extra);glBindTexture(GL_TEXTURE_2D,extra);glTexStorage2D(GL_TEXTURE_2D,1,fmt,w,h);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,extra,0);
        }
        const GLenum buffers[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(host?2:1,buffers);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glBindTexture(GL_TEXTURE_2D,0);glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    ~Target(){glDeleteFramebuffers(1,&fbo);const GLuint textures[]={color,depth,view,extra};glDeleteTextures(4,textures);}
    std::vector<Pixel> pixels(unsigned attachment=0){glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0+attachment);std::vector<Pixel> data(width*height);glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,data.data());return data;}
    std::vector<unsigned> depths(){glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);std::vector<unsigned> data(width*height);glReadPixels(0,0,width,height,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,data.data());return data;}
    void seed(){
        clean();std::vector<Pixel> data(width*height);for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)data[y*width+x]={float(x)/width,float(y)/height,.3f,.6f};
        glBindTexture(GL_TEXTURE_2D,color);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,data.data());glBindTexture(GL_TEXTURE_2D,0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);const float extra_color[]={.2f,.4f,.8f,.5f};glClearBufferfv(GL_COLOR,1,extra_color);glStencilMask(0xff);glClearBufferfi(GL_DEPTH_STENCIL,0,.25f,77);
    }
    MetaRendererPostSceneFrame frame(){
        MetaRendererPostSceneFrame f{};f.struct_size=sizeof(f);f.flags=META_POST_SCENE_GAMMA;f.framebuffer=fbo;f.color_texture=color;f.depth_texture=depth;f.depth_sample_texture=view;
        f.width=width;f.height=height;f.color_format=format;f.depth_format=GL_DEPTH24_STENCIL8;f.samples=1;f.viewport[2]=width;f.viewport[3]=height;f.near_plane=4;f.far_plane=4096;
        for(unsigned i=0;i<16;i+=5)f.world_matrix[i]=f.projection_matrix[i]=f.inverse_world_matrix[i]=f.inverse_projection_matrix[i]=1;return f;
    }
};
// Deliberately unrelated procedural silhouettes: colored rings, holes, seams,
// fractional alpha and overlapping color regions exercise the whole graph.
struct Mask {
    GLuint shader{},vao{};bool empty=false,reject=false,error=false;unsigned calls=0;GLint target=0;
    Mask(){shader=program(R"(#version 150
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2-1,0,1);})",R"(#version 150
uniform bool Empty;uniform vec2 Size;out vec4 fragColor;
void main(){
    vec2 p=gl_FragCoord.xy/Size;float r=length(p-vec2(.35,.55));
    bool ring=r>.13&&r<.28;bool bar=p.x>.6&&p.x<.8&&p.y>.15&&p.y<.8;
    if(Empty||(!ring&&!bar)||mod(floor(p.y*Size.y),13)==0)discard;
    fragColor=bar?vec4(.7,.2,.8,.7):vec4(.2,.9,.4,1);
})");glGenVertexArrays(1,&vao);}
    ~Mask(){glDeleteProgram(shader);glDeleteVertexArrays(1,&vao);}
    static bool draw(void* user,const MetaRendererPostSceneFrame& f) noexcept {
        auto& m=*static_cast<Mask*>(user);++m.calls;GLint old_program=0,old_vao=0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&m.target);glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao);
        GLint depth_type=0;glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&depth_type);check(depth_type==GL_NONE);
        glUseProgram(m.shader);glBindVertexArray(m.vao);glUniform1i(glGetUniformLocation(m.shader,"Empty"),m.empty);glUniform2f(glGetUniformLocation(m.shader,"Size"),f.width,f.height);
        glDrawArrays(GL_TRIANGLES,0,3);glUseProgram(old_program);glBindVertexArray(old_vao);if(m.error)glEnable(0xffffffffu);return !m.reject;
    }
};
std::vector<GLint> state(){
    std::vector<GLint> r;for(auto p:{GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,GL_READ_BUFFER,GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ARRAY_BUFFER_BINDING,GL_ACTIVE_TEXTURE,GL_DEPTH_FUNC,GL_DEPTH_WRITEMASK,GL_STENCIL_WRITEMASK,GL_STENCIL_FUNC,GL_STENCIL_REF}){GLint v=0;glGetIntegerv(p,&v);r.push_back(v);}
    GLint v[4]{};glGetIntegerv(GL_VIEWPORT,v);r.insert(r.end(),v,v+4);glGetIntegerv(GL_SCISSOR_BOX,v);r.insert(r.end(),v,v+4);v[1]=0;glGetIntegerv(GL_POLYGON_MODE,v);if(!v[1])v[1]=v[0];r.insert(r.end(),v,v+2);
    for(auto c:{GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_POLYGON_OFFSET_FILL})r.push_back(glIsEnabled(c));
    for(unsigned i=0;i<8;++i)r.push_back(glIsEnabled(GL_CLIP_DISTANCE0+i));
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(unsigned i=0;i<4;++i){
        glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,v);r.push_back(v[0]);glGetIntegeri_v(GL_SAMPLER_BINDING,i,v);r.push_back(v[0]);glGetIntegerv(GL_DRAW_BUFFER0+i,v);r.push_back(v[0]);r.push_back(glIsEnabledi(GL_BLEND,i));
        GLboolean mask[4]{};glGetBooleani_v(GL_COLOR_WRITEMASK,i,mask);for(auto b:mask)r.push_back(b);
        for(auto p:{GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA}){glGetIntegeri_v(p,i,v);r.push_back(v[0]);}
    }glActiveTexture(active);return r;
}
struct Reference {
    GLuint programs[3]{},vao{},vbo{},linear{};
    explicit Reference(const std::string& dir){
        const char* vertices[]={"sobel","blur","blit"};const char* fragments[]={"entity_sobel","entity_outline_box_blur","blit"};
        for(unsigned i=0;i<3;++i)programs[i]=program(read(dir+"/program/"+vertices[i]+".vsh"),read(dir+"/program/"+fragments[i]+".fsh"));
        glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenSamplers(1,&linear);
        for(auto p:{GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER})glSamplerParameteri(linear,p,GL_LINEAR);
        for(auto p:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T})glSamplerParameteri(linear,p,GL_CLAMP_TO_EDGE);
    }
    ~Reference(){for(auto p:programs)glDeleteProgram(p);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);glDeleteSamplers(1,&linear);}
    std::vector<Pixel> image(Mask& mask,const MetaRendererPostSceneFrame& f){
        Target a(f.width,f.height),b(f.width,f.height);clean();glBindFramebuffer(GL_DRAW_FRAMEBUFFER,a.fbo);const float zero[4]{};glClearBufferfv(GL_COLOR,0,zero);
        glViewport(f.viewport[0],f.viewport[1],f.viewport[2],f.viewport[3]);check(Mask::draw(&mask,f));
        float w=float(f.width),h=float(f.height);const float vertices[]={0,0,0,1,w,0,0,1,w,h,0,1,0,0,0,1,w,h,0,1,0,h,0,1};
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,0,16,nullptr);
        const float ortho[]={2/w,0,0,0,0,2/h,0,0,0,0,-1,0,-1,-1,0,1};glViewport(0,0,f.width,f.height);
        const auto pass=[&](unsigned p,Target& src,Target& dst,bool filtered){
            glUseProgram(programs[p]);auto id=programs[p];glUniform1i(glGetUniformLocation(id,"DiffuseSampler"),0);glUniformMatrix4fv(glGetUniformLocation(id,"ProjMat"),1,0,ortho);
            glUniform2f(glGetUniformLocation(id,"InSize"),w,h);glUniform2f(glGetUniformLocation(id,"OutSize"),w,h);glUniform4f(glGetUniformLocation(id,"ColorModulate"),1,1,1,1);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,dst.fbo);glClearBufferfv(GL_COLOR,0,zero);glBindTexture(GL_TEXTURE_2D,src.color);glBindSampler(0,filtered?linear:0);glDrawArrays(GL_TRIANGLES,0,6);
        };
        glEnablei(GL_BLEND,0);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glBlendEquation(GL_FUNC_ADD);pass(0,a,b,false);
        glBlendFunc(GL_ONE,GL_ZERO);glProgramUniform2f(programs[1],glGetUniformLocation(programs[1],"BlurDir"),1,0);pass(1,b,a,true);
        glProgramUniform2f(programs[1],glGetUniformLocation(programs[1],"BlurDir"),0,1);pass(1,a,b,true);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);pass(2,b,a,false);
        return a.pixels();
    }
};
void compare(const std::vector<Pixel>& actual,const std::vector<Pixel>& expected,float epsilon){
    check(actual.size()==expected.size());for(unsigned i=0;i<actual.size();++i)for(unsigned c=0;c<4;++c){
        if(std::abs(actual[i][c]-expected[i][c])>epsilon){std::fprintf(stderr,"pixel=%u channel=%u actual=%.8f expected=%.8f\n",i,c,actual[i][c],expected[i][c]);check(false);}++checks;
    }
}
void run(const std::string& dir,bool core){
    Reference reference(dir);Mask mask;EntityOutline outline;
    probe::original_storage=__glewTexStorage2D;__glewTexStorage2D=probe::storage;
    for(auto dims:{std::array<unsigned,2>{64,48},{31,57},{1,1},{65,33}})for(auto format:{GL_RGBA8,GL_RGBA16F})for(bool linear:{false,true}){
        Target host(dims[0],dims[1],format,true);host.seed();auto f=host.frame();if(linear)f.flags=META_POST_SCENE_LINEAR|META_POST_SCENE_HDR;
        if(f.width>32){f.viewport[0]=3;f.viewport[1]=5;f.viewport[2]-=7;f.viewport[3]-=9;}
        const auto original=host.pixels(),extra=host.pixels(1);const auto depth=host.depths();
        auto effect=reference.image(mask,f);auto expected=original;
        for(int y=f.viewport[1];y<f.viewport[1]+f.viewport[3];++y)for(int x=f.viewport[0];x<f.viewport[0]+f.viewport[2];++x){
            auto i=y*f.width+x;for(unsigned c=0;c<3;++c){const auto glow=std::pow(effect[i][c],linear?2.2f:1.f);expected[i][c]=original[i][c]*(1-effect[i][3])+glow*effect[i][3];}
        }
        if(f.width>1)check(expected!=original);
        clean();glBindFramebuffer(GL_FRAMEBUFFER,host.fbo);glReadBuffer(GL_COLOR_ATTACHMENT1);glViewport(1,2,3,4);glScissor(1,2,3,4);
        for(auto cap:{GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_COLOR_LOGIC_OP,GL_POLYGON_OFFSET_FILL})glEnable(cap);glEnable(GL_CLIP_DISTANCE2);
        glDepthMask(0);glDepthFunc(GL_GREATER);glStencilFunc(GL_NOTEQUAL,5,0x3f);glStencilMask(0x55);
        if(core)glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);else{glPolygonMode(GL_FRONT,GL_LINE);glPolygonMode(GL_BACK,GL_POINT);}
        for(unsigned i=0;i<4;++i){glEnablei(GL_BLEND,i);glBlendFuncSeparatei(i,GL_ONE,GL_DST_COLOR,GL_ZERO,GL_ONE);glBlendEquationSeparatei(i,GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD);glColorMaski(i,1,i%2,0,1);}glActiveTexture(GL_TEXTURE3);
        auto before=state();check(outline.replay(f,2.2f,Mask::draw,&mask));check(glGetError()==GL_NO_ERROR);check(state()==before);
        compare(host.pixels(),expected,format==GL_RGBA8?1.1f/255:0.0011f);check(host.depths()==depth);compare(host.pixels(1),extra,0);
        clean();glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);
        mask.empty=true;auto previous=host.pixels();check(outline.replay(f,2.2f,Mask::draw,&mask));compare(host.pixels(),previous,0);mask.empty=false;
        auto allocated=outline.statistics().allocations;
        auto storages=probe::storages;probe::allocations=0;probe::active=true;
        for(unsigned i=0;i<3;++i)check(outline.replay(f,2.2f,Mask::draw,&mask));probe::active=false;
        check(allocated==outline.statistics().allocations);check(probe::storages==storages);check(probe::allocations==0);
        previous=host.pixels();mask.reject=true;check(!outline.replay(f,2.2f,Mask::draw,&mask));mask.reject=false;compare(host.pixels(),previous,0);
        mask.error=true;check(!outline.replay(f,2.2f,Mask::draw,&mask));mask.error=false;check(glGetError()==GL_NO_ERROR);compare(host.pixels(),previous,0);
        auto bad=f;bad.width=16384;bad.height=16384;check(!outline.replay(bad,2.2f,Mask::draw,&mask));bad=f;bad.world_matrix[1]=std::numeric_limits<float>::quiet_NaN();check(!outline.replay(bad,2.2f,Mask::draw,&mask));
        check(!outline.replay(f,0,Mask::draw,&mask));bad=f;bad.view_index=1;check(!outline.replay(bad,2.2f,Mask::draw,&mask));bad=f;bad.viewport[0]=-1;check(!outline.replay(bad,2.2f,Mask::draw,&mask));
        check(outline.replay({},0,nullptr,nullptr));compare(host.pixels(),previous,0);
        // Failed resize leaves the previous target generation usable, while
        // host color/depth/MRT remain intact. No giant allocation is attempted.
        clean();Target resized(f.width+1,f.height+1,format,true);resized.seed();auto resize_before=resized.pixels();auto resized_frame=resized.frame();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);probe::fail_storage=2;
        check(!outline.replay(resized_frame,2.2f,Mask::draw,&mask));check(!probe::fail_storage);compare(resized.pixels(),resize_before,0);
        mask.empty=true;check(outline.replay(f,2.2f,Mask::draw,&mask));mask.empty=false;compare(host.pixels(),previous,0);
        check(outline.replay(resized_frame,2.2f,Mask::draw,&mask));check(glGetError()==GL_NO_ERROR);
    }
    auto owner=glfwGetCurrentContext();glfwWindowHint(GLFW_VISIBLE,GL_FALSE);auto other=glfwCreateWindow(16,16,"outline-context",nullptr,nullptr);check(other!=nullptr);glfwMakeContextCurrent(other);
    check(!outline.reset());check(outline.replay({},0,nullptr,nullptr));check(!outline.replay({},1,Mask::draw,&mask));glfwMakeContextCurrent(owner);glfwDestroyWindow(other);
    check(outline.reset());check(outline.statistics().resident_bytes==0);check(glGetError()==GL_NO_ERROR);
    __glewTexStorage2D=probe::original_storage;
}
}
int main(int argc,char** argv){
    if(argc!=2||!std::ifstream(std::string(argv[1])+"/program/entity_sobel.fsh"))return 2;
    {EntityOutline unused;check(unused.replay({},0,nullptr,nullptr));check(unused.reset());}
    check(glfwInit());
    for(bool core:{false,true}){
        glfwDefaultWindowHints();glfwWindowHint(GLFW_VISIBLE,GL_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,4);
        glfwWindowHint(GLFW_OPENGL_PROFILE,core?GLFW_OPENGL_CORE_PROFILE:GLFW_OPENGL_COMPAT_PROFILE);
        auto* window=glfwCreateWindow(64,64,"outline-offline",nullptr,nullptr);check(window!=nullptr);glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;check(glewInit()==GLEW_OK);while(glGetError()!=GL_NO_ERROR){}
        std::printf("profile=%s renderer=%s\n",core?"core":"compat",glGetString(GL_RENDERER));run(argv[1],core);glfwDestroyWindow(window);
    }glfwTerminate();std::printf("checks=%llu passed\n",static_cast<unsigned long long>(checks));
}
