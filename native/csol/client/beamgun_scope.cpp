// SPDX-License-Identifier: GPL-3.0-or-later
#include "beamgun.hpp"
#include "gl_state.hpp"
#include "assets.hpp"
#include "../beamgun_scope.hpp"
#include "../image_codec.hpp"

namespace csol::client
{
namespace
{
std::array<std::array<GLuint,5>,2> textures{};
bool attempted[2]{};
GLuint scopeProgram{},scopeVAO{},scopeVBO{};
GLint scopeViewport{};
void PrepareScope()
{
    if(scopeProgram)return;
    const char* vertex=R"(#version 330 core
layout(location=0) in vec4 point;
uniform vec2 viewportSize;out vec2 uv;
void main(){uv=point.zw;gl_Position=vec4(point.x*2/viewportSize.x-1,1-point.y*2/viewportSize.y,0,1);})";
    const char* fragment=R"(#version 330 core
in vec2 uv;uniform sampler2D image;out vec4 color;
void main(){color=texture(image,uv);})";
    const GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,nullptr);glCompileShader(vs);
    glShaderSource(fs,1,&fragment,nullptr);glCompileShader(fs);
    const GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok{};glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){glDeleteProgram(program);throw std::runtime_error("scope shader link failed");}
    scopeProgram=program;scopeViewport=glGetUniformLocation(program,"viewportSize");
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    glGenVertexArrays(1,&scopeVAO);glBindVertexArray(scopeVAO);
    glGenBuffers(1,&scopeVBO);glBindBuffer(GL_ARRAY_BUFFER,scopeVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(float)*16,nullptr,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
}
}
void ResetBeamScope()
{
    if(glewReady && wglGetCurrentContext())
    {
        for(auto& variant:textures)glDeleteTextures(int(variant.size()),variant.data());
        if(scopeProgram)glDeleteProgram(scopeProgram);
        if(scopeVAO)glDeleteVertexArrays(1,&scopeVAO);
        if(scopeVBO)glDeleteBuffers(1,&scopeVBO);
    }
    textures={};attempted[0]=attempted[1]=false;scopeProgram=scopeVAO=scopeVBO=0;
}
void DrawBeamScope(int weapon,int width,int height,float ratio)
{
    if(width<=0 || height<=0 || !EnsureGraphics())return;
    // HUD_Redraw is outside VGUI's Begin/End frame. Draw immediately instead
    // of queuing stale polygon vertices for a later UI frame.
    GLState saved;
    try
    {
        PrepareScope();
        const int variant=weapon==BeamgunLEId;auto& ids=textures[variant];
        if(!attempted[variant])
        {
            attempted[variant]=true;
            const char* paths[]{"sprites/huntbow_aim_default1.tga","sprites/huntbow_aim_default2.tga","sprites/huntbow_aim_default3.tga",
                variant?"sprites/beamgunle_gauge_bg.tga":"sprites/beamgun_gauge_bg.tga",
                variant?"sprites/beamgunle_gauge.tga":"sprites/beamgun_gauge.tga"};
            for(int i=0;i<5;++i)try
            {
                ids[i]=UploadImage(DecodeTGA(ReadResource(paths[i])));
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            }
            catch(const std::exception& error){engine.Con_DPrintf("CSOLSupport: scope %s: %s\n",paths[i],error.what());}
        }
        std::array<BeamScopeQuad,5> quads;const int count=BeamScopeLayout(width,height,ratio,quads);
        glUseProgram(scopeProgram);glUniform2f(scopeViewport,float(width),float(height));
        glBindVertexArray(scopeVAO);glBindBuffer(GL_ARRAY_BUFFER,scopeVBO);glActiveTexture(GL_TEXTURE0);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        for(int i=0;i<count;++i)
        {
            const auto& q=quads[i];if(!ids[q.texture])continue;
            const float vertices[16]{float(q.left),float(q.top),q.u0,q.v0,float(q.right),float(q.top),q.u1,q.v0,
                float(q.right),float(q.bottom),q.u1,q.v1,float(q.left),float(q.bottom),q.u0,q.v1};
            glBindTexture(GL_TEXTURE_2D,ids[q.texture]);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(vertices),vertices);glDrawArrays(GL_TRIANGLE_FAN,0,4);
        }
    }
    catch(const std::exception& error){engine.Con_DPrintf("CSOLSupport: scope: %s\n",error.what());}
}
}
