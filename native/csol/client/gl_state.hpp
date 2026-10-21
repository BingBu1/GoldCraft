// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
namespace csol::client
{
// Restore actual bindings and the engine texture cache together. Nested model
// and sprite loading may update the cache while temporary bindings are active.
struct GLState
{
    int cachedTexture=hostTextureCache?*hostTextureCache:-1;
    GLint program{},vao{},array{},active{},unpack{},alignment{},row{},skipRows{},skipPixels{};
    GLint texture[2]{},cube[2]{},sampler[2]{},srcRGB{},dstRGB{},srcAlpha{},dstAlpha{},equationRGB{},equationAlpha{},cullMode{},front{};
    GLboolean blend{},cull{},depth{},depthMask{};
    GLState()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        for(int i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture[i]);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cube[i]);glGetIntegerv(GL_SAMPLER_BINDING,&sampler[i]);glBindSampler(i,0);}
        glActiveTexture(GL_TEXTURE0);
        blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);depth=glIsEnabled(GL_DEPTH_TEST);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRGB);glGetIntegerv(GL_BLEND_DST_RGB,&dstRGB);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&equationRGB);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&equationAlpha);
        glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);glGetIntegerv(GL_FRONT_FACE,&front);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipPixels);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    }
    static void Enable(GLenum cap,bool on){if(on)glEnable(cap);else glDisable(cap);}
    ~GLState()
    {
        glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,array);
        for(int i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,texture[i]);glBindTexture(GL_TEXTURE_CUBE_MAP,cube[i]);glBindSampler(i,sampler[i]);}glActiveTexture(active);
        Enable(GL_BLEND,blend);Enable(GL_CULL_FACE,cull);Enable(GL_DEPTH_TEST,depth);glDepthMask(depthMask);
        glBlendFuncSeparate(srcRGB,dstRGB,srcAlpha,dstAlpha);glBlendEquationSeparate(equationRGB,equationAlpha);glCullFace(cullMode);glFrontFace(front);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_SKIP_ROWS,skipRows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipPixels);
        if(hostTextureCache)*hostTextureCache=cachedTexture;
    }
};
}
