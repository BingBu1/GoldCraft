// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Windows.h>
#include <GL/gl.h>
#include <array>
#include <cstddef>

namespace guest::cof::client::core_gl
{
inline constexpr GLenum Texture0=0x84C0, ArrayBuffer=0x8892, StaticDraw=0x88E4;
inline constexpr std::array<float,16> Identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
template<class T> T Proc(const char* name) noexcept
{
    const auto p=wglGetProcAddress(name);
    if(!p||p==PROC(1)||p==PROC(2)||p==PROC(3)||p==PROC(-1))return nullptr;
    return reinterpret_cast<T>(p);
}
inline bool IsCore() noexcept
{
    GLint profile{};glGetIntegerv(0x9126,&profile);return (profile&1)!=0;
}
struct Api
{
    void(APIENTRY* active)(GLenum){};
    void(APIENTRY* bindSampler)(GLuint,GLuint){};
    void(APIENTRY* genBuffers)(GLsizei,GLuint*){};
    void(APIENTRY* deleteBuffers)(GLsizei,const GLuint*){};
    void(APIENTRY* bindBuffer)(GLenum,GLuint){};
    void(APIENTRY* bufferData)(GLenum,std::ptrdiff_t,const void*,GLenum){};
    void(APIENTRY* bufferSubData)(GLenum,std::ptrdiff_t,std::ptrdiff_t,const void*){};
    void(APIENTRY* genArrays)(GLsizei,GLuint*){};
    void(APIENTRY* deleteArrays)(GLsizei,const GLuint*){};
    void(APIENTRY* bindArray)(GLuint){};
    void(APIENTRY* attribute)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*){};
    void(APIENTRY* enableAttribute)(GLuint){};
    GLuint(APIENTRY* createShader)(GLenum){};
    void(APIENTRY* shaderSource)(GLuint,GLsizei,const char* const*,const GLint*){};
    void(APIENTRY* compileShader)(GLuint){};
    void(APIENTRY* shaderInfo)(GLuint,GLenum,GLint*){};
    void(APIENTRY* deleteShader)(GLuint){};
    GLuint(APIENTRY* createProgram)(){};
    void(APIENTRY* attach)(GLuint,GLuint){};
    void(APIENTRY* link)(GLuint){};
    void(APIENTRY* programInfo)(GLuint,GLenum,GLint*){};
    void(APIENTRY* deleteProgram)(GLuint){};
    void(APIENTRY* use)(GLuint){};
    GLint(APIENTRY* uniform)(GLuint,const char*){};
    void(APIENTRY* integer)(GLint,GLint){};
    void(APIENTRY* vector3)(GLint,GLsizei,const GLfloat*){};
    void(APIENTRY* vector4)(GLint,GLsizei,const GLfloat*){};
    void(APIENTRY* matrix)(GLint,GLsizei,GLboolean,const GLfloat*){};
    void(APIENTRY* blend)(GLenum,GLenum,GLenum,GLenum){};
    void(APIENTRY* equation)(GLenum,GLenum){};
    void(APIENTRY* stencilMask)(GLenum,GLuint){};
    void(APIENTRY* attachment)(GLenum,GLenum,GLenum,GLint*){};
    bool Initialize() noexcept
    {
#define COF_GL(member,name) member=Proc<decltype(member)>(name);if(!member)return false
        COF_GL(active,"glActiveTexture");COF_GL(bindSampler,"glBindSampler");
        COF_GL(genBuffers,"glGenBuffers");COF_GL(deleteBuffers,"glDeleteBuffers");
        COF_GL(bindBuffer,"glBindBuffer");COF_GL(bufferData,"glBufferData");COF_GL(bufferSubData,"glBufferSubData");
        COF_GL(genArrays,"glGenVertexArrays");COF_GL(deleteArrays,"glDeleteVertexArrays");COF_GL(bindArray,"glBindVertexArray");
        COF_GL(attribute,"glVertexAttribPointer");COF_GL(enableAttribute,"glEnableVertexAttribArray");
        COF_GL(createShader,"glCreateShader");COF_GL(shaderSource,"glShaderSource");COF_GL(compileShader,"glCompileShader");
        COF_GL(shaderInfo,"glGetShaderiv");COF_GL(deleteShader,"glDeleteShader");COF_GL(createProgram,"glCreateProgram");
        COF_GL(attach,"glAttachShader");COF_GL(link,"glLinkProgram");COF_GL(programInfo,"glGetProgramiv");
        COF_GL(deleteProgram,"glDeleteProgram");COF_GL(use,"glUseProgram");COF_GL(uniform,"glGetUniformLocation");
        COF_GL(integer,"glUniform1i");COF_GL(vector3,"glUniform3fv");COF_GL(vector4,"glUniform4fv");
        COF_GL(matrix,"glUniformMatrix4fv");COF_GL(blend,"glBlendFuncSeparate");COF_GL(equation,"glBlendEquationSeparate");
        COF_GL(stencilMask,"glStencilMaskSeparate");
        COF_GL(attachment,"glGetFramebufferAttachmentParameteriv");
#undef COF_GL
        return true;
    }
    GLuint Program(const char* vertex,const char* fragment) const noexcept
    {
        const std::array<GLuint,2> shaders{createShader(0x8B31),createShader(0x8B30)};
        const char* sources[]{vertex,fragment};bool valid=true;
        for(unsigned i=0;i<2;++i){if(!shaders[i]){valid=false;continue;}shaderSource(shaders[i],1,&sources[i],nullptr);compileShader(shaders[i]);GLint status{};shaderInfo(shaders[i],0x8B81,&status);valid=valid&&status!=0;}
        GLuint program{};
        if(valid){program=createProgram();if(program){for(auto shader:shaders)attach(program,shader);link(program);GLint status{};programInfo(program,0x8B82,&status);if(!status){deleteProgram(program);program=0;}}}
        for(auto shader:shaders)if(shader)deleteShader(shader);
        return program;
    }
};
// Capture once for the complete pass. There are no fixed-function calls,
// allocations, FBO changes or viewport changes in this core-profile guard.
struct State
{
    const Api& gl;
    GLint program{},array{},buffer{},active{},unpack{},alignment{},row{},rows{},pixels{};
    std::array<std::array<GLint,5>,4> textures{};
    GLint srcRgb{},dstRgb{},srcAlpha{},dstAlpha{},eqRgb{},eqAlpha{},depthFunction{},frontStencil{},backStencil{};
    GLboolean blend{},depth{},cull{},stencil{},offset{},depthWrite{},color[4]{};
    GLfloat offsetFactor{},offsetUnits{};
    explicit State(const Api& api):gl(api)
    {
        glGetIntegerv(0x8B8D,&program);glGetIntegerv(0x85B5,&array);glGetIntegerv(0x8894,&buffer);glGetIntegerv(0x84E0,&active);
        for(unsigned i=0;i<textures.size();++i){gl.active(Texture0+i);glGetIntegerv(0x8068,&textures[i][0]);glGetIntegerv(0x8069,&textures[i][1]);glGetIntegerv(0x806A,&textures[i][2]);glGetIntegerv(0x8514,&textures[i][3]);glGetIntegerv(0x8919,&textures[i][4]);gl.bindSampler(i,0);}
        gl.active(Texture0);
        glGetIntegerv(0x88EF,&unpack);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&rows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&pixels);
        glGetIntegerv(0x80C9,&srcRgb);glGetIntegerv(0x80C8,&dstRgb);glGetIntegerv(0x80CB,&srcAlpha);glGetIntegerv(0x80CA,&dstAlpha);glGetIntegerv(0x8009,&eqRgb);glGetIntegerv(0x883D,&eqAlpha);
        blend=glIsEnabled(GL_BLEND);depth=glIsEnabled(GL_DEPTH_TEST);cull=glIsEnabled(GL_CULL_FACE);stencil=glIsEnabled(GL_STENCIL_TEST);offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetIntegerv(GL_DEPTH_FUNC,&depthFunction);glGetIntegerv(GL_STENCIL_WRITEMASK,&frontStencil);glGetIntegerv(0x8CA5,&backStencil);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);glGetBooleanv(GL_COLOR_WRITEMASK,color);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&offsetFactor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&offsetUnits);
        gl.bindBuffer(0x88EC,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    }
    static void Enable(GLenum capability,GLboolean enabled) noexcept {if(enabled)glEnable(capability);else glDisable(capability);}
    ~State()
    {
        gl.use(GLuint(program));gl.bindArray(GLuint(array));gl.bindBuffer(ArrayBuffer,GLuint(buffer));
        for(unsigned i=0;i<textures.size();++i){gl.active(Texture0+i);glBindTexture(GL_TEXTURE_1D,GLuint(textures[i][0]));glBindTexture(GL_TEXTURE_2D,GLuint(textures[i][1]));glBindTexture(0x806F,GLuint(textures[i][2]));glBindTexture(0x8513,GLuint(textures[i][3]));gl.bindSampler(i,GLuint(textures[i][4]));}gl.active(GLenum(active));
        gl.bindBuffer(0x88EC,GLuint(unpack));glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_SKIP_ROWS,rows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,pixels);
        Enable(GL_BLEND,blend);Enable(GL_DEPTH_TEST,depth);Enable(GL_CULL_FACE,cull);Enable(GL_STENCIL_TEST,stencil);Enable(GL_POLYGON_OFFSET_FILL,offset);
        gl.blend(GLenum(srcRgb),GLenum(dstRgb),GLenum(srcAlpha),GLenum(dstAlpha));gl.equation(GLenum(eqRgb),GLenum(eqAlpha));glDepthFunc(GLenum(depthFunction));glDepthMask(depthWrite);glColorMask(color[0],color[1],color[2],color[3]);glPolygonOffset(offsetFactor,offsetUnits);gl.stencilMask(GL_FRONT,GLuint(frontStencil));gl.stencilMask(GL_BACK,GLuint(backStencil));
    }
};
}
