#version 430
#include "common.h"
layout(location=0) in vec3 in_pos;
layout(location=1) in vec2 in_uv;
layout(location=2) in vec4 in_color;
layout(location=3) in vec4 in_overlay;
#ifndef MULTIVIEW_ENABLED
#define v_worldpos g_worldpos
#define v_uv g_uv
#define v_color g_color
#define v_overlay g_overlay
#endif
out vec3 v_worldpos;
out vec2 v_uv;
out vec4 v_color;
out vec4 v_overlay;
void main(){
    v_worldpos=in_pos;v_uv=in_uv;v_color=in_color;v_overlay=in_overlay;
    gl_Position=GetCameraProjMatrix(0)*GetCameraWorldMatrix(0)*vec4(in_pos,1.0);
}
