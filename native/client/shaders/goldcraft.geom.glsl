#version 430
#include "common.h"
layout(triangles) in;
layout(triangle_strip,max_vertices=18) out;
in vec3 v_worldpos[3];
in vec2 v_uv[3];
in vec4 v_color[3];
out vec3 g_worldpos;
out vec2 g_uv;
out vec4 g_color;
void main(){
#ifdef MULTIVIEW_ENABLED
    int views=CameraUBO.numViews;
#else
    int views=1;
#endif
    for(int view=0;view<views;view++){
        for(int i=0;i<3;i++){
            gl_Layer=view;
            g_worldpos=v_worldpos[i];g_uv=v_uv[i];g_color=v_color[i];
            gl_Position=GetCameraProjMatrix(view)*GetCameraWorldMatrix(view)*vec4(g_worldpos,1.0);
            EmitVertex();
        }
        EndPrimitive();
    }
}
