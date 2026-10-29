// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the actual status renderer and UploadImage on a hidden driver
// context. This source is prepared only until an execution window is granted.
#include "../../native/csol/client/status_icons_gl.cpp"
#include "../../native/csol/client/characters.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <cstdarg>

namespace fixture
{
std::filesystem::path root;
int checks{},reads{},logs{};
int cachedTexture=717;
enum class Resource { Real, Missing, Malformed };
Resource resource{Resource::Real};
void Check(bool value,const std::source_location where=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("status GPU check line "+std::to_string(where.line()));}
}
namespace csol::client
{
cl_enginefunc_t engine{};bool glewReady{};int* hostTextureCache=&fixture::cachedTexture;
const Character* ViewCharacter(){return nullptr;}
bool Exists(const char* path){return std::filesystem::is_regular_file(fixture::root/path);}
std::vector<std::byte> ReadResource(const char* path)
{
    ++fixture::reads;
    if(fixture::resource==fixture::Resource::Missing)throw std::runtime_error("controlled missing resource");
    if(fixture::resource==fixture::Resource::Malformed)return {std::byte{1}};
    std::ifstream stream(fixture::root/path,std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error(path);
    const auto size=stream.tellg();if(size<0)throw std::runtime_error("resource size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("resource read");return bytes;
}
model_t* LoadEngineModel(const char*){throw std::runtime_error("unexpected model request in status test");}
bool EnsureGraphics()
{
    if(!wglGetCurrentContext())return false;
    if(!glewReady){glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK||!GLEW_VERSION_3_3)return false;glewReady=true;while(glGetError()!=GL_NO_ERROR){}}
    return true;
}
}
namespace
{
using namespace fixture;using namespace csol::client;
constexpr int Width=1280,Height=720;
void Log(const char*,...){++logs;}
bool Compatibility()
{GLint profile{};glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);return (profile&GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)!=0;}
struct State
{
    std::vector<GLint> ints;
    std::vector<GLboolean> bools;
    int cached{};
    State()
    {
        for(GLenum key:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ARRAY_BUFFER_BINDING,GL_ACTIVE_TEXTURE,
            GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA,
            GL_CULL_FACE_MODE,GL_FRONT_FACE,GL_DEPTH_FUNC,GL_PIXEL_UNPACK_BUFFER_BINDING,GL_UNPACK_ALIGNMENT,
            GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS})
        {GLint value{};glGetIntegerv(key,&value);ints.push_back(value);}
        const GLint active=ints[3];
        for(int unit=0;unit<2;++unit)
        {glActiveTexture(GL_TEXTURE0+unit);for(GLenum key:{GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_CUBE_MAP,GL_SAMPLER_BINDING})
            {GLint value{};glGetIntegerv(key,&value);ints.push_back(value);}}
        glActiveTexture(active);
        for(GLenum key:{GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST})bools.push_back(glIsEnabled(key));
        GLboolean depth{},mask[4]{};glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        bools.push_back(depth);bools.insert(bools.end(),mask,mask+4);cached=cachedTexture;
        if(Compatibility())
        {
            bools.push_back(glIsEnabled(GL_ALPHA_TEST));GLint function{};GLfloat reference{};
            glGetIntegerv(GL_ALPHA_TEST_FUNC,&function);glGetFloatv(GL_ALPHA_TEST_REF,&reference);
            ints.push_back(function);ints.push_back(std::bit_cast<int>(reference));
        }
    }
    bool operator==(const State& other)const{return ints==other.ints&&bools==other.bools&&cached==other.cached;}
};
struct SeededState
{
    GLuint vao{},buffer{},unpack{},textures[4]{},samplers[2]{};
    SeededState()
    {
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferData(GL_ARRAY_BUFFER,128,nullptr,GL_STATIC_DRAW);
        glGenBuffers(1,&unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glBufferData(GL_PIXEL_UNPACK_BUFFER,64,nullptr,GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,9);glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
        glGenTextures(4,textures);glGenSamplers(2,samplers);
        for(int i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i*2]);glBindTexture(GL_TEXTURE_CUBE_MAP,textures[i*2+1]);glBindSampler(i,samplers[i]);}
        glActiveTexture(GL_TEXTURE3);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
        glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);glDisable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE,GL_ZERO,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA);glBlendEquationSeparate(GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_NEVER);glEnable(GL_STENCIL_TEST);glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);glFrontFace(GL_CW);cachedTexture=717;
        if(Compatibility()){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.5f);}
    }
    ~SeededState()
    {
        glDeleteSamplers(2,samplers);glDeleteTextures(4,textures);glDeleteBuffers(1,&buffer);glDeleteBuffers(1,&unpack);glDeleteVertexArrays(1,&vao);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glActiveTexture(GL_TEXTURE0);
        glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        if(Compatibility())glDisable(GL_ALPHA_TEST);
    }
};
struct TargetBuffer
{
    GLuint fbo{},color{};
    TargetBuffer()
    {
        glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,Width,Height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,Width,Height);
    }
    void Clear()
    {glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);}
    std::array<unsigned char,4> Pixel(int x,int top)
    {std::array<unsigned char,4> p{};glReadPixels(x,Height-1-top,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
    ~TargetBuffer(){glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);}
};
void CheckPixels(TargetBuffer& target,const csol::Image& image,bool half)
{
    // Item 10 is wholly on screen; item 0 follows the original H-28 placement
    // and has one source row clipped by the framebuffer edge.
    constexpr int left=463,top=656;
    for(unsigned y=0;y<29;++y)for(unsigned x=0;x<29;++x)
    {
        const auto pixel=target.Pixel(left+int(x),top+int(y));
        const auto at=(y*29+x)*4;const double opacity=(half&&y<15)?70.0/255.0:1.0;
        const double alpha=image.RGBA[at+3]/255.0*opacity;
        for(int c=0;c<3;++c)Check(std::abs(double(pixel[c])-image.RGBA[at+c]*alpha)<=2);
    }
    Check(target.Pixel(left+29,top+10)==std::array<unsigned char,4>{});
    Check(target.Pixel(left+10,top+29)==std::array<unsigned char,4>{});
}
void RenderAndRestore()
{
    const auto image=csol::DecodeTGA(ReadResource(csol::status::ProtectionImage));
    Check(image.Width==29&&image.Height==29&&image.RGBA.size()==29*29*4);
    TargetBuffer target;target.Clear();
    {
        SeededState host;const State before;
        PrepareStatusIconGraphics();Check(State{}==before);
        Check(texture&&program&&vao&&vbo&&imageWidth==29&&imageHeight==29);
        const auto loaded=reads;PrepareStatusIconGraphics();Check(reads==loaded&&State{}==before);
        csol::status::ProtectionIcons icons;for(int i=0;i<11;++i)Check(icons.Add({csol::status::Operation::Add,i,7,0,-1},1));
        DrawStatusIconGraphics(icons,Width,Height);Check(State{}==before);CheckPixels(target,image,false);
        Check(glGetError()==GL_NO_ERROR);
    }
    target.Clear();
    {
        SeededState host;const State before;
        csol::status::ProtectionIcons icons;icons.Advance(10);
        for(int i=0;i<11;++i)Check(icons.Add({csol::status::Operation::Add,i,7,0,2},1));icons.Advance(11);
        DrawStatusIconGraphics(icons,Width,Height);Check(State{}==before);CheckPixels(target,image,true);
        Check(glGetError()==GL_NO_ERROR);
    }
    const GLuint oldTexture=texture,oldProgram=program,oldVao=vao,oldVbo=vbo;
    glewReady=false; // Exact ordering imposed by production ClearScene before session/video reset.
    ResetStatusIconGraphics();
    Check(!glIsTexture(oldTexture)&&!glIsProgram(oldProgram)&&!glIsVertexArray(oldVao)&&!glIsBuffer(oldVbo));
}
void ResourceFailure()
{
    for(auto mode:{Resource::Missing,Resource::Malformed})
    {
        ResetStatusIconGraphics();resource=mode;const auto oldReads=reads,oldLogs=logs;
        SeededState host;const State before;
        PrepareStatusIconGraphics();Check(!texture&&!program&&!vao&&!vbo&&logs==oldLogs+1&&State{}==before);
        PrepareStatusIconGraphics();Check(reads==oldReads+1&&logs==oldLogs+1&&State{}==before);
        resource=Resource::Real;ResetStatusIconGraphics();PrepareStatusIconGraphics();
        Check(texture&&program&&State{}==before);ResetStatusIconGraphics();
    }
}
struct ShaderFault
{
    inline static ShaderFault* current{};
    PFNGLSHADERSOURCEPROC saved{__glewShaderSource};
    bool linkFailure{};
    std::vector<GLuint> shaders;
    explicit ShaderFault(bool link):linkFailure(link)
    {
        Check(!current);current=this;__glewShaderSource=Source;
    }
    static void APIENTRY Source(GLuint shader,GLsizei count,const GLchar* const* strings,const GLint* lengths)
    {
        auto& fault=*current;fault.shaders.push_back(shader);GLint kind{};glGetShaderiv(shader,GL_SHADER_TYPE,&kind);
        const char* replacement=nullptr;
        if(!fault.linkFailure&&kind==GL_FRAGMENT_SHADER)replacement="#version 330 core\ninvalid shader source";
        if(fault.linkFailure&&kind==GL_VERTEX_SHADER)replacement=
            "#version 330 core\nlayout(location=0) in vec4 point; out vec3 uv; out float alpha;\n"
            "void main(){uv=point.xyz;alpha=1;gl_Position=vec4(point.xy,0,1);}";
        if(replacement)fault.saved(shader,1,&replacement,nullptr);
        else fault.saved(shader,count,strings,lengths);
    }
    ~ShaderFault(){__glewShaderSource=saved;current=nullptr;}
};
void ShaderFailure()
{
    for(bool link:{false,true})
    {
        ResetStatusIconGraphics();const auto oldLogs=logs;
        SeededState host;const State before;
        {
            ShaderFault fault(link);PrepareStatusIconGraphics();
            Check(logs==oldLogs+1&&!texture&&!program&&!vao&&!vbo&&State{}==before);
            Check(fault.shaders.size()==2);for(auto shader:fault.shaders)Check(!glIsShader(shader));
            // The driver produced the actual compile/link failure; no status
            // return code or cleanup implementation is replaced by the test.
        }
        ResetStatusIconGraphics();PrepareStatusIconGraphics();Check(texture&&program&&State{}==before);
        ResetStatusIconGraphics();
    }
}
void ContextOwnership(GLFWwindow* owner)
{
    PrepareStatusIconGraphics();const GLuint ownedTexture=texture,ownedProgram=program,ownedVao=vao,ownedVbo=vbo;
    Check(ownedTexture&&ownedProgram&&ownedVao&&ownedVbo);
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_COMPAT_PROFILE);
    auto* other=glfwCreateWindow(32,32,"status independent context",nullptr,nullptr);Check(other!=nullptr);
    glfwMakeContextCurrent(other);glewReady=false;Check(EnsureGraphics());
    // Compatibility context permits a fresh texture with the exact old name.
    glBindTexture(GL_TEXTURE_2D,ownedTexture);Check(glIsTexture(ownedTexture));
    ResetStatusIconGraphics();Check(glIsTexture(ownedTexture));glDeleteTextures(1,&ownedTexture);
    glfwMakeContextCurrent(owner);Check(glIsTexture(ownedTexture)&&glIsProgram(ownedProgram));
    glDeleteTextures(1,&ownedTexture);glDeleteProgram(ownedProgram);glDeleteVertexArrays(1,&ownedVao);glDeleteBuffers(1,&ownedVbo);
    glfwDestroyWindow(other);glewReady=false;
}
}
int main(int argc,char** argv)
{
    using namespace fixture;using namespace csol::client;
    try
    {
        Check(argc==2||(argc==3&&std::string_view(argv[2])=="--core"));
        const bool core=argc==3;root=std::filesystem::u8path(argv[1]);engine.Con_DPrintf=Log;
        Check(glfwInit()!=0);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,core?GLFW_OPENGL_CORE_PROFILE:GLFW_OPENGL_COMPAT_PROFILE);
        auto* window=glfwCreateWindow(Width,Height,"status icons hidden driver checks",nullptr,nullptr);Check(window!=nullptr);
        glfwMakeContextCurrent(window);Check(EnsureGraphics());Check(Compatibility()!=core);
        RenderAndRestore();ResourceFailure();ShaderFailure();ContextOwnership(window);
        ResetStatusIconGraphics();glfwDestroyWindow(window);glfwTerminate();
        std::cout<<"status actual GL checks: "<<checks<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
