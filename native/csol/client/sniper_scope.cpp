// SPDX-License-Identifier: GPL-3.0-or-later
#include "sniper_scope.hpp"
#include "assets.hpp"
#include "gl_state.hpp"

namespace csol::client {
namespace {
GLuint program{},vao{},vbo{},arc{};
HGLRC context{};
GLint viewport{},textured{};
bool attempted{};
bool compatibility{};
int layoutWidth{},layoutHeight{},layoutGap{};
struct Vertex {float x,y,u,v;};
std::array<Vertex,72> vertices{};

void Layout(int width,int height,int gap)
{
    if(width==layoutWidth && height==layoutHeight && gap==layoutGap)return;
    int count{};
    const auto quad=[&](int l,int t,int r,int b,float u0,float v0,float u1,float v1)
    {
        const Vertex a{float(l),float(t),u0,v0},c{float(r),float(b),u1,v1};
        vertices[count++]=a;vertices[count++]={float(r),float(t),u1,v0};vertices[count++]=c;
        vertices[count++]=a;vertices[count++]=c;vertices[count++]={float(l),float(b),u0,v1};
    };
    // Original 02AB83A0 default high-capability scope, with its one-pixel
    // overlaps. The scheme owns gap; it is not a fixed screen-pixel value.
    const int cx=width/2,cy=height/2,t=height/16,b=height-t,l=(width-height)/2+t,r=width-l;
    constexpr float lo=1.f/512,hi=511.f/512;
    quad(cx,cy,r,b,lo,lo,hi,hi);
    quad(cx-1,t,r,cy+1,lo,hi,hi,lo);
    quad(l,cy,cx,b,hi,lo,lo,hi);
    quad(l,t,cx,cy,hi,hi,lo,lo);
    quad(cx,0,cx+1,cy-gap,0,0,0,0);
    quad(cx,cy+gap,cx+1,height,0,0,0,0);
    quad(0,cy+1,cx-gap,cy+2,0,0,0,0);
    quad(cx+gap,cy+1,width,cy+2,0,0,0,0);
    quad(0,0,width,t+1,0,0,0,0);
    quad(0,b-1,width,height,0,0,0,0);
    quad(0,t,l+1,height,0,0,0,0);
    quad(r-1,t,width,height,0,0,0,0);
    glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(vertices),vertices.data());
    layoutWidth=width;layoutHeight=height;layoutGap=gap;
}
void Prepare()
{
    context=wglGetCurrentContext();
    GLint profile{};glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
    compatibility=(profile&GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)!=0;
    const char* vertex=R"(#version 330 core
layout(location=0) in vec4 point;
uniform vec2 viewportSize;out vec2 uv;
void main(){uv=point.zw;gl_Position=vec4(point.x*2/viewportSize.x-1,1-point.y*2/viewportSize.y,0,1);})";
    const char* fragment=R"(#version 330 core
in vec2 uv;uniform sampler2D image;uniform int textured;out vec4 color;
void main(){color=vec4(0,0,0,textured!=0?texture(image,uv).a:1);})";
    const GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,nullptr);glCompileShader(vs);
    glShaderSource(fs,1,&fragment,nullptr);glCompileShader(fs);
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok{};glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok)throw std::runtime_error("sniper scope shader link failed");
    viewport=glGetUniformLocation(program,"viewportSize");textured=glGetUniformLocation(program,"textured");
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),nullptr,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    arc=UploadImage(DecodeTGA(ReadResource("sprites/scope_arc.tga")));
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
}
void ResetSniperScope()
{
    if(context && wglGetCurrentContext()==context)
    {
        if(program)glDeleteProgram(program);
        if(vao)glDeleteVertexArrays(1,&vao);
        if(vbo)glDeleteBuffers(1,&vbo);
        if(arc)glDeleteTextures(1,&arc);
    }
    program=vao=vbo=arc=0;context=nullptr;attempted=false;compatibility=false;
    layoutWidth=layoutHeight=layoutGap=0;
}
void DrawSniperScope(int width,int height,int gap)
{
    if(width<=0 || height<=0 || gap<0 || !EnsureGraphics())return;
    if(context && context!=wglGetCurrentContext())ResetSniperScope();
    GLState saved;
    if(!attempted)
    {
        attempted=true;
        try {Prepare();}
        catch(const std::exception& error)
        {
            ResetSniperScope();attempted=true;
            engine.Con_DPrintf("CSOLSupport: sniper scope: %s\n",error.what());
        }
    }
    if(!program || !arc)return;
    const GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean alphaTest=compatibility?glIsEnabled(GL_ALPHA_TEST):GL_FALSE;
    GLboolean colorMask[4];glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(compatibility)glDisable(GL_ALPHA_TEST);
    glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    Layout(width,height,gap);
    glUniform2f(viewport,float(width),float(height));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,arc);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUniform1i(textured,1);glDrawArrays(GL_TRIANGLES,0,24);
    glUniform1i(textured,0);glDrawArrays(GL_TRIANGLES,24,48);
    GLState::Enable(GL_SCISSOR_TEST,scissor);
    if(compatibility)GLState::Enable(GL_ALPHA_TEST,alphaTest);
    glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
}
}
