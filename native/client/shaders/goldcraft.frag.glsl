#version 430
#include "common.h"
layout(binding=0) uniform sampler2D diffuseTex;
uniform int u_emissive;
uniform int u_feedback;
in vec3 g_worldpos;
in vec2 g_uv;
in vec4 g_color;
in vec4 g_overlay;
layout(location=0) out vec4 out_Diffuse;
#ifdef GBUFFER_ENABLED
layout(location=1) out vec4 out_Lightmap;
layout(location=2) out vec4 out_WorldNorm;
layout(location=3) out vec4 out_Specular;
#endif
void main(){
    vec4 texel=u_feedback==2?vec4(1.0):texture(diffuseTex,g_uv);
    if(texel.a*g_color.a<0.1)discard;
    float distanceToCamera=distance(g_worldpos,GetCameraViewPos(GetCameraViewIndex()));
#ifdef SHADOW_CASTER_ENABLED
    #ifdef LINEAR_DEPTH_ENABLED
    // An emitter is represented by a point within its one-block volume.
    // Let light leave its own emissive geometry; it still casts shadows from
    // the sun or other distant lights.
    if(u_emissive!=0&&distanceToCamera<32.0)discard;
    gl_FragDepth=distanceToCamera/GetCameraZFar(GetCameraViewIndex());
    #endif
    out_Diffuse=vec4(distanceToCamera,0.0,0.0,1.0);
#else
    if(u_feedback!=0){out_Diffuse=texel*g_color;return;}
    vec4 diffuse=ProcessDiffuseColor(texel)*ProcessOtherGammaColor(g_color);
    diffuse.rgb=mix(diffuse.rgb,ProcessOtherGammaColor(vec4(g_overlay.rgb,1.0)).rgb,g_overlay.a);
    // Exported vertex colors contain tint and face shading. Separate ambient
    // from albedo so Renderer can light and shadow the same GBuffer as the BSP.
    float brightness=u_emissive!=0?1.0:0.55;
    vec4 ambient=ProcessOtherGammaColor(vec4(brightness,brightness,brightness,1.0));
    #ifdef GBUFFER_ENABLED
    vec3 normal=normalize(cross(dFdx(g_worldpos),dFdy(g_worldpos)));
    out_Diffuse=diffuse;
    out_Lightmap=ambient;
    out_WorldNorm=vec4(UnitVectorToOctahedron(normal),distanceToCamera,diffuse.a);
    out_Specular=vec4(0.0);
    #else
    out_Diffuse=ProcessLinearBlendShift(diffuse*ambient);
    #endif
#endif
}
