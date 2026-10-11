#pragma once
#include <windows.h>
#include <GL/glew.h>

namespace goldcraft::render {
// Snapshot only the state this scope can change. External renderers retain
// ownership between scopes; no host state is cached across callbacks.
struct DrawState {
    explicit DrawState(bool scene_pass = true, bool transparent = false, bool cached_texture = true);
    ~DrawState();
    DrawState(const DrawState &) = delete;
    DrawState &operator=(const DrawState &) = delete;
    void save_feedback();
    void save_effects();

  private:
    bool scene_pass_, full_blend_, stencil_write_, feedback_saved_ = false;
    bool effects_saved_=false;
    bool cached_texture_=true;
    GLint cull_mode=GL_BACK,front_face=GL_CCW;
    GLint shader, vao, array_buffer, active, texture, sampler, depth_func;
    GLboolean depth, cull, stencil, depth_mask, polygon_offset;
    GLfloat line_width, offset_factor, offset_units;
    struct Blend {
        GLboolean enabled, mask[4];
        GLint src_rgb, dst_rgb, src_alpha, dst_alpha, eq_rgb, eq_alpha;
    } blend[4];
    struct Stencil {
        GLint func, ref, mask, write_mask, fail, zfail, zpass;
    } front, back;
    static Stencil stencil_state(bool back);
    void blend_parameters(unsigned index);
};
} // namespace goldcraft::render
