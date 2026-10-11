#version 430
#include "common.h"
layout(location=0) in vec3 in_pos;
layout(location=1) in vec2 in_uv;
layout(location=2) in vec4 in_color;
layout(location=3) in vec4 in_overlay;
uniform bool u_view_offset;
#ifdef RAW_POST_MASK
uniform mat4 u_post_world;
uniform mat4 u_post_projection;
#endif
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
#ifdef RAW_POST_MASK
    vec4 view_position=u_post_world*vec4(in_pos,1.0);
    if(u_view_offset)view_position.xyz*=0.99975586;
    gl_Position=u_post_projection*view_position;
#else
    vec4 view_position=GetCameraWorldMatrix(0)*vec4(in_pos,1.0);
#ifndef SHADOW_CASTER_ENABLED
    if(u_view_offset)view_position.xyz*=0.99975586;
#endif
    gl_Position=GetCameraProjMatrix(0)*view_position;
#endif
}
