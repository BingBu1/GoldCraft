#version 430
#include "common.h"
layout(location=0) in vec3 in_pos;
layout(location=1) in vec2 in_uv;
layout(location=2) in vec4 in_color;
#ifndef MULTIVIEW_ENABLED
#define v_worldpos g_worldpos
#define v_uv g_uv
#define v_color g_color
#endif
out vec3 v_worldpos;
out vec2 v_uv;
out vec4 v_color;
void main(){
    v_worldpos=in_pos;v_uv=in_uv;v_color=in_color;
    gl_Position=GetCameraProjMatrix(0)*GetCameraWorldMatrix(0)*vec4(in_pos,1.0);
}
