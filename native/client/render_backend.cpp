#include "render_backend.hpp"
#include <metahook.h>
#include <IMetaRenderer.h>
#include <mathlib2.h>
#include <gl_common.h>
#include "goldcraft/wire.hpp"
#include <array>
#include <algorithm>
#include <fstream>
#include <map>
#include <utility>

namespace goldcraft::render {
namespace {
IMetaRenderer* renderer=nullptr;
IMetaRendererScene* scene=nullptr;
IMetaRendererWorldEdit* edits=nullptr;
IMetaRendererWorldEdit2* targeted_edits=nullptr;
IMetaRendererPostScene* post_scene_provider=nullptr;
IMetaRendererSceneCallbacks* callback=nullptr;
void(*logger)(const std::string&)=nullptr;
Statistics stats;
constexpr std::uint32_t raw_mask_program=1u<<31;
struct Program {GLuint id=0;GLint emissive=-1,feedback=-1,effect=-1,cutoff=-1,color_mod=-1,world=-1,projection=-1,view_offset=-1;};
std::map<std::uint32_t,Program> programs;
HGLRC current_context=nullptr;
GLuint hud_program=0,hud_vao=0;
GLuint repeat_linear_sampler=0;
void prepare_effect_sampler(){
    if(repeat_linear_sampler||!GLEW_VERSION_3_3)return;
    glGenSamplers(1,&repeat_linear_sampler);
    glSamplerParameteri(repeat_linear_sampler,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glSamplerParameteri(repeat_linear_sampler,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glSamplerParameteri(repeat_linear_sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glSamplerParameteri(repeat_linear_sampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
}
struct HudUniforms {GLint sampler=-1,menu=-1,size=-1,cursor=-1;} hud_uniforms;
GLint max_texture_size=0;
GLfloat feedback_line_width=0;

void enabled(GLenum cap,GLboolean value){if(value)glEnable(cap);else glDisable(cap);}
struct PixelState {
    bool pack;
    GLint buffer=0,alignment=0,row_length=0,skip_rows=0,skip_pixels=0;
    explicit PixelState(bool is_pack):pack(is_pack) {
        glGetIntegerv(pack?GL_PIXEL_PACK_BUFFER_BINDING:GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
        glGetIntegerv(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,&alignment);
        glGetIntegerv(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,&row_length);
        glGetIntegerv(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,&skip_rows);
        glGetIntegerv(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,&skip_pixels);
        glBindBuffer(pack?GL_PIXEL_PACK_BUFFER:GL_PIXEL_UNPACK_BUFFER,0);
        glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,pack?4:1);
        glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,0);
        glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,0);
        glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,0);
    }
    ~PixelState(){
        glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,alignment);
        glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,row_length);
        glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,skip_rows);
        glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,skip_pixels);
        glBindBuffer(pack?GL_PIXEL_PACK_BUFFER:GL_PIXEL_UNPACK_BUFFER,buffer);
    }
};
const Program& program(std::uint32_t flags) {
    if(auto it=programs.find(flags);it!=programs.end())return it->second;
    std::string defines;
    if(flags&raw_mask_program)defines+="#define RAW_POST_MASK\n";
    if(flags&META_SCENE_GBUFFER)defines+="#define GBUFFER_ENABLED\n";
    if(flags&META_SCENE_SHADOW)defines+="#define SHADOW_CASTER_ENABLED\n";
    if(flags&META_SCENE_MULTIVIEW)defines+="#define MULTIVIEW_ENABLED\n";
    if(flags&META_SCENE_LINEAR_DEPTH)defines+="#define LINEAR_DEPTH_ENABLED\n";
    if(flags&META_SCENE_GAMMA_BLEND)defines+="#define GAMMA_BLEND_ENABLED\n";
    CCompileShaderArgs args;
    args.vsfile="renderer/shader/goldcraft.vert.glsl";
    args.gsfile=(flags&META_SCENE_MULTIVIEW)?"renderer/shader/goldcraft.geom.glsl":nullptr;
    args.fsfile="renderer/shader/goldcraft.frag.glsl";
    args.vsdefine=args.gsdefine=args.fsdefine=defines.c_str();
    const GLuint id=renderer->CompileShaderFileEx(&args);
    const Program compiled{id,glGetUniformLocation(id,"u_emissive"),glGetUniformLocation(id,"u_feedback"),
        glGetUniformLocation(id,"u_effect"),glGetUniformLocation(id,"u_alpha_cutoff"),glGetUniformLocation(id,"u_color_mod"),
        glGetUniformLocation(id,"u_post_world"),glGetUniformLocation(id,"u_post_projection"),glGetUniformLocation(id,"u_view_offset")};
    return programs.emplace(flags,compiled).first->second;
}
}

const Statistics& statistics(){return stats;}
bool scene_active(){return scene!=nullptr;}
IMetaRendererWorldEdit* world_edit(){return edits;}
IMetaRendererWorldEdit2* world_edit2(){return targeted_edits;}
IMetaRendererPostScene* post_scene(){return post_scene_provider;}
bool owns_context(){return current_context&&current_context==wglGetCurrentContext();}
void set_light_shadow(int key,unsigned size){if(scene)scene->SetDynamicLightShadowSize(key,size);}

bool initialize(IMetaRendererSceneCallbacks* callbacks,void(*log)(const std::string&)) {
    logger=log;
    // VidInit revokes the prior transaction before Renderer/GL initialization.
    // A failed initialization must not expose an earlier optional provider.
    edits=nullptr;targeted_edits=nullptr;post_scene_provider=nullptr;stats.post_scene_api=false;
    if(current_context!=wglGetCurrentContext()){
        programs.clear();hud_program=hud_vao=repeat_linear_sampler=0;hud_uniforms={};max_texture_size=0;feedback_line_width=0;current_context=wglGetCurrentContext();
    }
    if(!current_context)return false;
    glewExperimental=GL_TRUE;
    const auto result=glewInit();
    // GLEW probes the removed extension string on some Core Profile drivers.
    while(glGetError()!=GL_NO_ERROR){}
    if(result!=GLEW_OK){logger("OpenGL function loading failed");return false;}
    GLint profile=0;if(GLEW_VERSION_3_2)glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
    stats.core=(profile&GL_CONTEXT_CORE_PROFILE_BIT)!=0;
    HMODULE module=GetModuleHandleA("Renderer_AVX2.dll");
    if(!module)module=GetModuleHandleA("Renderer.dll");
    auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
    renderer=factory?static_cast<IMetaRenderer*>(factory(METARENDERER_INTERFACE_VERSION,nullptr)):nullptr;
    stats.renderer=renderer!=nullptr;
    if(!renderer){logger("MetaRenderer_API_002 unavailable; Renderer scene disabled (no ABI guessing)");return false;}
    scene=static_cast<IMetaRendererScene*>(factory(METARENDERER_SCENE_INTERFACE_VERSION,nullptr));
    post_scene_provider=static_cast<IMetaRendererPostScene*>(factory(METARENDERER_POST_SCENE_INTERFACE_VERSION,nullptr));
    stats.post_scene_api=post_scene_provider!=nullptr;
    targeted_edits=static_cast<IMetaRendererWorldEdit2*>(factory(METARENDERER_WORLD_EDIT2_INTERFACE_VERSION,nullptr));
    edits=targeted_edits?static_cast<IMetaRendererWorldEdit*>(targeted_edits):
        static_cast<IMetaRendererWorldEdit*>(factory(METARENDERER_WORLD_EDIT_INTERFACE_VERSION,nullptr));
    if(scene){
        if(callback&&callback!=callbacks)scene->UnregisterSceneCallbacks(callback);
        callback=callbacks;scene->RegisterSceneCallbacks(callback);
    }
    stats.scene_api=scene!=nullptr;
    // The verified glint layers use repeat/linear without mipmaps. A sampler
    // keeps shared texture storage unchanged and is prepared outside drawing.
    prepare_effect_sampler();
    logger(std::string("OpenGL ")+reinterpret_cast<const char*>(glGetString(GL_VERSION))+"; verified MetaRenderer_API_002; scene API="+(scene?"1":"0"));
    return true;
}
void shutdown(){
    if(edits)edits->Reset();
    if(scene&&callback)scene->UnregisterSceneCallbacks(callback);
    if(owns_context()){if(hud_program)glDeleteProgram(hud_program);if(hud_vao)glDeleteVertexArrays(1,&hud_vao);if(repeat_linear_sampler)glDeleteSamplers(1,&repeat_linear_sampler);}
    hud_program=hud_vao=repeat_linear_sampler=0;hud_uniforms={};max_texture_size=0;feedback_line_width=0;
    scene=nullptr;edits=nullptr;targeted_edits=nullptr;post_scene_provider=nullptr;stats.post_scene_api=false;
    callback=nullptr;renderer=nullptr;programs.clear();current_context=nullptr;
}

Mesh::~Mesh(){
    if(context_&&context_==wglGetCurrentContext()){
        if(vao_)glDeleteVertexArrays(1,&vao_);
        if(vbo_)glDeleteBuffers(1,&vbo_);
        if(ebo_)glDeleteBuffers(1,&ebo_);
    }
}
void Mesh::draw(std::span<const Vertex> vertices,std::uint64_t revision,bool dynamic,bool lines) {
    if(vertices.empty())return;
    const auto context=wglGetCurrentContext();
    if(context_!=context){context_=context;vao_=vbo_=ebo_=0;revision_=0;capacity_=index_capacity_=0;}
    if(!vao_){
        glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);glGenBuffers(1,&ebo_);
        glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo_);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(2);glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),reinterpret_cast<void*>(20));
        glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),reinterpret_cast<void*>(24));
    }else glBindVertexArray(vao_);
    if(revision_!=revision||vertices_!=vertices.size()){
        glBindBuffer(GL_ARRAY_BUFFER,vbo_);
        if(vertices.size_bytes()>capacity_){
            capacity_=vertices.size_bytes();
            glBufferData(GL_ARRAY_BUFFER,capacity_,vertices.data(),dynamic?GL_STREAM_DRAW:GL_STATIC_DRAW);
        }else{
            // Orphan dynamic data to avoid waiting for a previous shadow/scene draw.
            if(dynamic)glBufferData(GL_ARRAY_BUFFER,capacity_,nullptr,GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER,0,vertices.size_bytes(),vertices.data());
        }
        const std::size_t count=vertices.size()/4*6;
        if(!lines&&count>index_capacity_){
            std::vector<std::uint32_t> indices;indices.reserve(count);
            for(std::uint32_t i=0;i<vertices.size();i+=4)indices.insert(indices.end(),{i,i+1,i+2,i,i+2,i+3});
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(std::uint32_t),indices.data(),GL_STATIC_DRAW);
            index_capacity_=count;
        }
        revision_=revision;vertices_=vertices.size();++stats.uploads;stats.upload_bytes+=vertices.size_bytes();
    }
    if(lines)glDrawArrays(GL_LINES,0,static_cast<GLsizei>(vertices.size()));
    else glDrawElements(GL_TRIANGLES,static_cast<GLsizei>(vertices.size()/4*6),GL_UNSIGNED_INT,nullptr);
    ++stats.draws;
}

DrawState::Stencil DrawState::stencil_state(bool back){
    Stencil s;
    glGetIntegerv(back?GL_STENCIL_BACK_FUNC:GL_STENCIL_FUNC,&s.func);
    glGetIntegerv(back?GL_STENCIL_BACK_REF:GL_STENCIL_REF,&s.ref);
    glGetIntegerv(back?GL_STENCIL_BACK_VALUE_MASK:GL_STENCIL_VALUE_MASK,&s.mask);
    glGetIntegerv(back?GL_STENCIL_BACK_WRITEMASK:GL_STENCIL_WRITEMASK,&s.write_mask);
    glGetIntegerv(back?GL_STENCIL_BACK_FAIL:GL_STENCIL_FAIL,&s.fail);
    glGetIntegerv(back?GL_STENCIL_BACK_PASS_DEPTH_FAIL:GL_STENCIL_PASS_DEPTH_FAIL,&s.zfail);
    glGetIntegerv(back?GL_STENCIL_BACK_PASS_DEPTH_PASS:GL_STENCIL_PASS_DEPTH_PASS,&s.zpass);return s;
}
void DrawState::blend_parameters(unsigned i){
    auto& b=blend[i];
    glGetIntegeri_v(GL_BLEND_SRC_RGB,i,&b.src_rgb);glGetIntegeri_v(GL_BLEND_DST_RGB,i,&b.dst_rgb);
    glGetIntegeri_v(GL_BLEND_SRC_ALPHA,i,&b.src_alpha);glGetIntegeri_v(GL_BLEND_DST_ALPHA,i,&b.dst_alpha);
    glGetIntegeri_v(GL_BLEND_EQUATION_RGB,i,&b.eq_rgb);glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA,i,&b.eq_alpha);
}
DrawState::DrawState(bool scene_pass,bool transparent,bool cached_texture):scene_pass_(scene_pass),
    full_blend_(!scene_pass||transparent),stencil_write_(scene_pass&&!transparent),cached_texture_(cached_texture){
    glGetIntegerv(GL_CURRENT_PROGRAM,&shader);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegeri_v(GL_SAMPLER_BINDING,0,&sampler);
    depth=glIsEnabled(GL_DEPTH_TEST);cull=glIsEnabled(GL_CULL_FACE);stencil=glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    if(scene_pass_){
        polygon_offset=glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
        if(stencil_write_){front=stencil_state(false);back=stencil_state(true);}
    }
    for(unsigned i=0;i<(scene_pass_?4u:1u);i++){
        auto& b=blend[i];b.enabled=glIsEnabledi(GL_BLEND,i);glGetBooleani_v(GL_COLOR_WRITEMASK,i,b.mask);
        if(full_blend_)blend_parameters(i);
    }
}
void DrawState::save_feedback(){
    if(feedback_saved_)return;
    glGetFloatv(GL_LINE_WIDTH,&line_width);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&offset_factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&offset_units);
    if(!full_blend_)blend_parameters(0);
    feedback_saved_=true;
}
void DrawState::save_effects(){
    if(effects_saved_)return;
    glGetIntegerv(GL_CULL_FACE_MODE,&cull_mode);glGetIntegerv(GL_FRONT_FACE,&front_face);
    effects_saved_=true;
}
DrawState::~DrawState(){
    glUseProgram(shader);glBindVertexArray(vao);
    if(cached_texture_)renderer->BindTextureUnit(0,GL_TEXTURE_2D,texture);
    else {glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);}
    glBindSampler(0,sampler);glActiveTexture(active);
    enabled(GL_DEPTH_TEST,depth);glDepthMask(depth_mask);enabled(GL_CULL_FACE,cull);enabled(GL_STENCIL_TEST,stencil);
    if(effects_saved_){glCullFace(cull_mode);glFrontFace(front_face);}
    if(scene_pass_){
        enabled(GL_POLYGON_OFFSET_FILL,polygon_offset);glDepthFunc(depth_func);glBindBuffer(GL_ARRAY_BUFFER,array_buffer);
        if(feedback_saved_){glPolygonOffset(offset_factor,offset_units);glLineWidth(line_width);}
    }
    for(unsigned i=0;i<(stencil_write_?2u:0u);i++){
        const auto& s=i?back:front;const GLenum face=i?GL_BACK:GL_FRONT;
        glStencilFuncSeparate(face,s.func,s.ref,s.mask);glStencilMaskSeparate(face,s.write_mask);glStencilOpSeparate(face,s.fail,s.zfail,s.zpass);
    }
    for(unsigned i=0;i<(scene_pass_?4u:1u);i++){
        const auto& b=blend[i];if(b.enabled)glEnablei(GL_BLEND,i);else glDisablei(GL_BLEND,i);
        if(full_blend_||(feedback_saved_&&i==0)){
            glBlendFuncSeparatei(i,b.src_rgb,b.dst_rgb,b.src_alpha,b.dst_alpha);glBlendEquationSeparatei(i,b.eq_rgb,b.eq_alpha);
        }
        glColorMaski(i,b.mask[0],b.mask[1],b.mask[2],b.mask[3]);
    }
}
Pass::Pass(bool transparent,std::uint32_t flags,const MetaRendererPostSceneFrame* post_mask){
    if(!renderer||!GLEW_VERSION_4_4)return;
    effects_allowed_=transparent&&flags==0;
    raw_mask_=post_mask!=nullptr;
    if(raw_mask_)flags=raw_mask_program;
    else if(renderer->IsDrawGammaBlendEnabled())flags|=META_SCENE_GAMMA_BLEND;
    state_.emplace(true,transparent,!raw_mask_);stencil_write_=!transparent;
    const auto& shader=program(flags);glUseProgram(shader.id);glBindSampler(0,0);
    if(!shader.id){state_.reset();return;}
    if(post_mask){
        glUniformMatrix4fv(shader.world,1,GL_FALSE,post_mask->world_matrix);
        glUniformMatrix4fv(shader.projection,1,GL_FALSE,post_mask->projection_matrix);
    }
    feedback_location_=shader.feedback;emissive_location_=shader.emissive;
    effect_location_=shader.effect;cutoff_location_=shader.cutoff;color_mod_location_=shader.color_mod;
    view_offset_location_=shader.view_offset;glUniform1i(view_offset_location_,0);
    glUniform1i(effect_location_,0);
    glUniform1i(feedback_location_,0);glDisable(GL_POLYGON_OFFSET_FILL);
    shadow_=(flags&META_SCENE_SHADOW)!=0;emissive(false);
    depth_write_=!transparent;
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(depth_write_?GL_TRUE:GL_FALSE);glDisable(GL_CULL_FACE);
    if(transparent)glDisable(GL_STENCIL_TEST);
    else{glEnable(GL_STENCIL_TEST);glStencilFunc(GL_ALWAYS,0,0xff);glStencilMask(0xff);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);}
    for(unsigned i=0;i<4;i++){
        if(transparent){glEnablei(GL_BLEND,i);glBlendFuncSeparatei(i,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glBlendEquationSeparatei(i,GL_FUNC_ADD,GL_FUNC_ADD);}
        else glDisablei(GL_BLEND,i);
        glColorMaski(i,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    if(flags&META_SCENE_SHADOW)++stats.shadow_passes;else if(!transparent)++stats.opaque_passes;
    ready_=true;
}
Pass::~Pass(){
    state_.reset();
    const GLenum error=glGetError();if(error!=GL_NO_ERROR){stats.error=error;++stats.error_count;if(logger)logger("Core scene GL error="+std::to_string(error));}
}
void Pass::texture(GLuint id){
    if(texture_&&*texture_==id)return;
    if(raw_mask_){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,id);}
    else renderer->BindTextureUnit(0,GL_TEXTURE_2D,id);
    texture_=id;
}
void Pass::depth_write(bool value){if(depth_write_==value)return;glDepthMask(value?GL_TRUE:GL_FALSE);depth_write_=value;}
void Pass::view_offset(bool value){
    value=value&&!shadow_;
    if(view_offset_==value)return;
    glUniform1i(view_offset_location_,value?1:0);view_offset_=value;
}
bool Pass::effect(const ParticleMaterial& material,GLuint id){
    if(!ready_||!effects_allowed_)return false;
    if((material.flags&particle_repeat_linear)&&!repeat_linear_sampler)return false;
    const auto* old=effect_material_?&*effect_material_:nullptr;
    if(!old){
        state_->save_effects();glCullFace(GL_BACK);glFrontFace(GL_CCW);
        glUniform1i(feedback_location_,0);glDisable(GL_POLYGON_OFFSET_FILL);emissive(true);
    }
    depth_write((material.flags&particle_depth_write)!=0);
    view_offset((material.flags&particle_view_offset)!=0);
    if(!old||((old->flags^material.flags)&particle_depth_test))enabled(GL_DEPTH_TEST,(material.flags&particle_depth_test)!=0);
    if(!old||old->depth_func!=material.depth_func)glDepthFunc(material.depth_func);
    if(!old||((old->flags^material.flags)&particle_cull))enabled(GL_CULL_FACE,(material.flags&particle_cull)!=0);
    if(!old||((old->flags^material.flags)&particle_blend)){
        if(material.flags&particle_blend)glEnablei(GL_BLEND,0);else glDisablei(GL_BLEND,0);
    }
    if(!old||old->src_rgb!=material.src_rgb||old->dst_rgb!=material.dst_rgb||old->src_alpha!=material.src_alpha||old->dst_alpha!=material.dst_alpha)
        glBlendFuncSeparatei(0,material.src_rgb,material.dst_rgb,material.src_alpha,material.dst_alpha);
    if(!old||old->equation_rgb!=material.equation_rgb||old->equation_alpha!=material.equation_alpha)
        glBlendEquationSeparatei(0,material.equation_rgb,material.equation_alpha);
    if(!old||old->color_mask!=material.color_mask)glColorMaski(0,(material.color_mask&1)!=0,(material.color_mask&2)!=0,(material.color_mask&4)!=0,(material.color_mask&8)!=0);
    if(!old||((old->flags^material.flags)&particle_repeat_linear))
        glBindSampler(0,(material.flags&particle_repeat_linear)?repeat_linear_sampler:0);
    const auto shader_flags=[](const ParticleMaterial& value){
        return 1|(value.texture==particle_untextured?2:0)|
            ((value.flags&particle_cutoff_before_mod)?4:0)|((value.flags&particle_cutoff_texture_only)?8:0)|
            ((value.flags&particle_discard_zero_alpha)?16:0);
    };
    if(!old||shader_flags(*old)!=shader_flags(material))glUniform1i(effect_location_,shader_flags(material));
    if(!old||old->alpha_cutoff!=material.alpha_cutoff)glUniform1f(cutoff_location_,material.alpha_cutoff);
    if(!old||old->color_mod!=material.color_mod)glUniform4fv(color_mod_location_,1,material.color_mod.data());
    texture(material.texture==particle_untextured?0:id);
    effect_material_=material;
    return true;
}
void Pass::feedback(unsigned mode){
    state_->save_feedback();
    view_offset(false);
    glUniform1i(feedback_location_,static_cast<int>(mode));
    depth_write(false);glDisable(GL_STENCIL_TEST);emissive(true);
    if(mode==1){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-3.0f,-3.0f);glBlendFuncSeparatei(0,GL_DST_COLOR,GL_SRC_COLOR,GL_ZERO,GL_ONE);}
    else{
        glDisable(GL_POLYGON_OFFSET_FILL);glBlendFuncSeparatei(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
        if(!feedback_line_width){GLfloat range[2];glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE,range);feedback_line_width=std::clamp(2.0f,range[0],range[1]);}
        glLineWidth(feedback_line_width);
    }
}
void Pass::emissive(bool value){
    if(emissive_value_==static_cast<int>(value))return;
    glUniform1i(emissive_location_,value?1:0);
    if(stencil_write_)glStencilFunc(GL_ALWAYS,value&&!shadow_?STENCIL_MASK_NO_LIGHTING:0,0xff);
    emissive_value_=value;
}

bool draw_hud(GLuint texture,float mouse_x,float mouse_y,bool menu,unsigned width,unsigned height){
    if(!renderer||!owns_context()||!texture)return false;
    const bool initialize_sampler=!hud_program;
    if(!hud_program){
        const char* vertex=R"(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}
)";
        const char* fragment=R"(#version 330 core
in vec2 uv;out vec4 color;
uniform sampler2D hud;uniform vec2 size;uniform vec2 cursor;uniform bool menu;
void main(){
    color=texture(hud,uv);
    if(menu){
        ivec2 p=ivec2(floor(vec2(uv.x,1.0-uv.y)*size-cursor));
        if(p.x>=0&&p.x<8&&p.y>=0&&p.y<12){
            const int rows[12]=int[12](1,5,25,105,425,1705,6825,5465,409,1637,1633,1280);
            int pixel=(rows[p.y]>>(2*p.x))&3;
            if(pixel!=0)color=vec4(vec3(pixel==2?1.0:0.0),1.0);
        }
    }
}
)";
        GLuint shaders[2]{glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};
        const char* sources[2]{vertex,fragment};bool ok=true;
        for(int i=0;i<2;i++){
            glShaderSource(shaders[i],1,&sources[i],nullptr);glCompileShader(shaders[i]);GLint status=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&status);
            if(!status){char error[2048]{};glGetShaderInfoLog(shaders[i],sizeof(error),nullptr,error);logger(std::string("HUD shader: ")+error);ok=false;}
        }
        if(ok){hud_program=glCreateProgram();for(auto shader:shaders)glAttachShader(hud_program,shader);glLinkProgram(hud_program);GLint status=0;glGetProgramiv(hud_program,GL_LINK_STATUS,&status);ok=status!=0;}
        for(auto shader:shaders)glDeleteShader(shader);
        if(!ok){if(hud_program)glDeleteProgram(hud_program);hud_program=0;return false;}
        hud_uniforms={glGetUniformLocation(hud_program,"hud"),glGetUniformLocation(hud_program,"menu"),
            glGetUniformLocation(hud_program,"size"),glGetUniformLocation(hud_program,"cursor")};
        glGenVertexArrays(1,&hud_vao);
    }
    {
        DrawState state(false);const GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_SCISSOR_TEST);
        glUseProgram(hud_program);glBindVertexArray(hud_vao);glBindSampler(0,0);renderer->BindTextureUnit(0,GL_TEXTURE_2D,texture);
        if(initialize_sampler)glUniform1i(hud_uniforms.sampler,0);
        glUniform1i(hud_uniforms.menu,menu?1:0);
        glUniform2f(hud_uniforms.size,static_cast<float>(width),static_cast<float>(height));
        glUniform2f(hud_uniforms.cursor,mouse_x*width,mouse_y*height);
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);
        glEnablei(GL_BLEND,0);glBlendEquationSeparatei(0,GL_FUNC_ADD,GL_FUNC_ADD);
        glBlendFuncSeparatei(0,GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDrawArrays(GL_TRIANGLES,0,3);
        enabled(GL_SCISSOR_TEST,scissor);
    }
    const GLenum error=glGetError();if(error!=GL_NO_ERROR){stats.error=error;logger("HUD GL error="+std::to_string(error));return false;}
    return true;
}

void upload_texture(GLuint& id,unsigned& width,unsigned& height,unsigned next_width,unsigned next_height,std::span<const std::uint8_t> rgba){
    if(!glBindBuffer)throw ProtocolError("OpenGL not initialized for texture upload");
    GLint old=0;if(!max_texture_size)glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture_size);
    if(next_width>static_cast<unsigned>(max_texture_size)||next_height>static_cast<unsigned>(max_texture_size))throw ProtocolError("texture exceeds GPU size limit");
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);
    const bool reuse=id&&width==next_width&&height==next_height;
    const bool created=!id;
    if(created)glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
    if(created){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    {
        PixelState pixels(false);
        if(reuse)glTexSubImage2D(GL_TEXTURE_2D,0,0,0,next_width,next_height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,next_width,next_height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    }
    width=next_width;height=next_height;glBindTexture(GL_TEXTURE_2D,old);
}

void upload_atlas_patches(GLuint id,std::span<const AtlasPatch> patches){
    GLint old=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);glBindTexture(GL_TEXTURE_2D,id);
    {
        PixelState pixels(false);
        for(const auto& patch:patches)
            glTexSubImage2D(GL_TEXTURE_2D,0,patch.x,patch.y,patch.width,patch.height,GL_RGBA,GL_UNSIGNED_BYTE,patch.rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D,old);
}

bool capture(const char* path){
    if(!glBindFramebuffer)return false;
    GLint viewport[4]{},read_fbo=0,draw_fbo=0,read_buffer=0;
    glGetIntegerv(GL_VIEWPORT,viewport);const int width=viewport[2],height=viewport[3];
    if(width<1||height<1||width>4096||height>4096)return false;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fbo);glGetIntegerv(GL_READ_BUFFER,&read_buffer);
    const unsigned stride=(width*3+3)&~3u;std::vector<std::uint8_t> pixels(stride*height);
    GLenum error=GL_NO_ERROR;
    {
        PixelState store(true);glBindFramebuffer(GL_READ_FRAMEBUFFER,draw_fbo);
        GLint target_buffer=0;glGetIntegerv(GL_READ_BUFFER,&target_buffer);
        glReadBuffer(draw_fbo?GL_COLOR_ATTACHMENT0:GL_BACK);
        glReadPixels(viewport[0],viewport[1],width,height,GL_BGR,GL_UNSIGNED_BYTE,pixels.data());error=glGetError();
        glReadBuffer(target_buffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);glReadBuffer(read_buffer);
    }
    if(error!=GL_NO_ERROR){logger("Frame capture failed, GL error="+std::to_string(error));return false;}
    Writer header;header.u16(0x4d42);header.u32(static_cast<unsigned>(54+pixels.size()));header.u32(0);header.u32(54);
    header.u32(40);header.i32(width);header.i32(height);header.u16(1);header.u16(24);header.u32(0);header.u32(static_cast<unsigned>(pixels.size()));
    header.u32(0);header.u32(0);header.u32(0);header.u32(0);
    const std::string temporary=std::string(path)+".tmp";
    {std::ofstream out(temporary,std::ios::binary);out.write(reinterpret_cast<const char*>(header.data.data()),header.data.size());out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());if(!out)return false;}
    if(!MoveFileExA(temporary.c_str(),path,MOVEFILE_REPLACE_EXISTING))return false;
    logger("Captured active framebuffer "+std::to_string(draw_fbo)+", "+std::to_string(width)+"x"+std::to_string(height));return true;
}
}
