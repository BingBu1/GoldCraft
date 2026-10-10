// Execute the client backend on a hidden driver context. Only the three
// Renderer services are adapters: texture binding, shader compilation and the
// gamma flag. Scene shaders are small fixtures; the HUD shader is production.
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <metahook.h>
#include <IMetaRenderer.h>
#include <cmath>
#include <mathlib2.h>
#include <gl_common.h>
#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace probe {
bool count_allocations{};
std::size_t allocations{}, queries{}, locations{}, uniforms{}, texture_parameters{}, bindings{};
void reset() { allocations = queries = locations = uniforms = texture_parameters = bindings = 0; }
void APIENTRY integer(GLenum p, GLint *v) {
    ++queries;
    glGetIntegerv(p, v);
}
void APIENTRY floating(GLenum p, GLfloat *v) {
    ++queries;
    glGetFloatv(p, v);
}
void APIENTRY boolean(GLenum p, GLboolean *v) {
    ++queries;
    glGetBooleanv(p, v);
}
GLboolean APIENTRY enabled(GLenum p) {
    ++queries;
    return glIsEnabled(p);
}
void APIENTRY integer_i(GLenum p, GLuint i, GLint *v) {
    ++queries;
    glGetIntegeri_v(p, i, v);
}
void APIENTRY boolean_i(GLenum p, GLuint i, GLboolean *v) {
    ++queries;
    glGetBooleani_v(p, i, v);
}
GLboolean APIENTRY enabled_i(GLenum p, GLuint i) {
    ++queries;
    return glIsEnabledi(p, i);
}
GLint APIENTRY location(GLuint p, const GLchar *name) {
    ++locations;
    return glGetUniformLocation(p, name);
}
void APIENTRY uniform(GLint p, GLint v) {
    ++uniforms;
    glUniform1i(p, v);
}
void APIENTRY parameter(GLenum p, GLenum name, GLint v) {
    ++texture_parameters;
    glTexParameteri(p, name, v);
}
} // namespace probe
void *operator new(std::size_t n) {
    if (probe::count_allocations)
        ++probe::allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

// Count the production call sites while forwarding every operation to GL.
#undef glGetIntegeri_v
#undef glGetBooleani_v
#undef glIsEnabledi
#undef glGetUniformLocation
#undef glUniform1i
#define glGetIntegerv probe::integer
#define glGetFloatv probe::floating
#define glGetBooleanv probe::boolean
#define glIsEnabled probe::enabled
#define glGetIntegeri_v probe::integer_i
#define glGetBooleani_v probe::boolean_i
#define glIsEnabledi probe::enabled_i
#define glGetUniformLocation probe::location
#define glUniform1i probe::uniform
#define glTexParameteri probe::parameter
#ifndef GOLDCRAFT_RENDER_SOURCE
#define GOLDCRAFT_RENDER_SOURCE "../../native/client/render_backend.cpp"
#endif
#include GOLDCRAFT_RENDER_SOURCE
#undef glGetIntegerv
#undef glGetFloatv
#undef glGetBooleanv
#undef glIsEnabled
#undef glGetIntegeri_v
#undef glGetBooleani_v
#undef glIsEnabledi
#undef glGetUniformLocation
#undef glUniform1i
#undef glTexParameteri
#define glGetIntegeri_v GLEW_GET_FUN(__glewGetIntegeri_v)
#define glGetBooleani_v GLEW_GET_FUN(__glewGetBooleani_v)
#define glIsEnabledi GLEW_GET_FUN(__glewIsEnabledi)
#define glGetUniformLocation GLEW_GET_FUN(__glewGetUniformLocation)
#define glUniform1i GLEW_GET_FUN(__glewUniform1i)

#include "renderer_unused.inc"

namespace {
constexpr int Size = 64;
using Pixels = std::array<std::uint8_t, Size * Size * 4>;
using namespace goldcraft::render;
std::uint64_t pixel_hash = 14695981039346656037ull;

GLuint compile(GLenum stage, const std::string &text) {
    const auto id = glCreateShader(stage);
    const auto *source = text.c_str();
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok{};
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s\n", log);
    }
    assert(ok);
    return id;
}
GLuint make_program(const char *defines) {
    const auto vs = compile(GL_VERTEX_SHADER, R"(#version 440
layout(location=0) in vec3 p;layout(location=1) in vec2 uv;
out vec2 t;void main(){gl_Position=vec4(p,1);t=uv;}
)");
    const auto fs =
        compile(GL_FRAGMENT_SHADER, std::string("#version 440\n") + (defines ? defines : "") + R"(
in vec2 t;layout(binding=0) uniform sampler2D image;
uniform int u_emissive;uniform int u_feedback;
layout(location=0) out vec4 color;
#ifdef GBUFFER_ENABLED
layout(location=1) out vec4 light;layout(location=2) out vec4 normal;layout(location=3) out vec4 spec;
#endif
void main(){
color=texture(image,t)*vec4(u_emissive!=0?vec3(0,1,0):vec3(1,0,0),1);
if(u_feedback==1)color=vec4(0,0,1,1);if(u_feedback==2)color=vec4(1);
#ifdef GBUFFER_ENABLED
light=vec4(.25,.5,.75,1);normal=vec4(.5,.5,1,1);spec=vec4(0,0,0,1);
#endif
})");
    const auto id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);
    GLint ok{};
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    assert(ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return id;
}
struct Renderer : UnusedRenderer {
    bool gamma = false;
    std::vector<GLuint> shaders;
    std::uint32_t CompileShaderFileEx(const CCompileShaderArgs *args) override {
        assert(args && args->vsfile && args->fsfile);
        const auto id = make_program(args->fsdefine);
        shaders.push_back(id);
        return id;
    }
    void BindTextureUnit(int unit, int target, int id) override {
        ++probe::bindings;
        // Match GL_BindTextureUnit's GL calls: unit zero relies on the caller's
        // active unit. The engine texture-cache mirror has no fixture consumer.
        if (unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(target, id);
            glActiveTexture(GL_TEXTURE0);
        } else
            glBindTexture(target, id);
    }
    bool IsDrawGammaBlendEnabled() const override { return gamma; }
    ~Renderer() {
        for (const auto id : shaders)
            glDeleteProgram(id);
    }
};
void log_message(const std::string &text) { std::fprintf(stderr, "%s\n", text.c_str()); }

std::vector<double> snapshot() {
    std::vector<double> result;
    for (const GLenum p : {GL_CURRENT_PROGRAM,
                           GL_VERTEX_ARRAY_BINDING,
                           GL_ARRAY_BUFFER_BINDING,
                           GL_ELEMENT_ARRAY_BUFFER_BINDING,
                           GL_ACTIVE_TEXTURE,
                           GL_TEXTURE_BINDING_2D,
                           GL_DEPTH_FUNC,
                           GL_STENCIL_FUNC,
                           GL_STENCIL_REF,
                           GL_STENCIL_VALUE_MASK,
                           GL_STENCIL_WRITEMASK,
                           GL_STENCIL_FAIL,
                           GL_STENCIL_PASS_DEPTH_FAIL,
                           GL_STENCIL_PASS_DEPTH_PASS,
                           GL_STENCIL_BACK_FUNC,
                           GL_STENCIL_BACK_REF,
                           GL_STENCIL_BACK_VALUE_MASK,
                           GL_STENCIL_BACK_WRITEMASK,
                           GL_STENCIL_BACK_FAIL,
                           GL_STENCIL_BACK_PASS_DEPTH_FAIL,
                           GL_STENCIL_BACK_PASS_DEPTH_PASS,
                           GL_DRAW_FRAMEBUFFER_BINDING,
                           GL_READ_FRAMEBUFFER_BINDING,
                           GL_PIXEL_UNPACK_BUFFER_BINDING,
                           GL_UNPACK_ALIGNMENT,
                           GL_UNPACK_ROW_LENGTH,
                           GL_UNPACK_SKIP_ROWS,
                           GL_UNPACK_SKIP_PIXELS}) {
        GLint value{};
        glGetIntegerv(p, &value);
        result.push_back(value);
    }
    for (const GLenum p : {GL_LINE_WIDTH, GL_POLYGON_OFFSET_FACTOR, GL_POLYGON_OFFSET_UNITS}) {
        GLfloat value{};
        glGetFloatv(p, &value);
        result.push_back(value);
    }
    for (const GLenum p :
         {GL_DEPTH_TEST, GL_CULL_FACE, GL_STENCIL_TEST, GL_POLYGON_OFFSET_FILL, GL_SCISSOR_TEST})
        result.push_back(glIsEnabled(p));
    GLboolean mask{};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    result.push_back(mask);
    GLint active{};
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    for (unsigned i = 0; i < 4; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        GLint texture{}, sampler{};
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegeri_v(GL_SAMPLER_BINDING, i, &sampler);
        result.push_back(texture);
        result.push_back(sampler);
        result.push_back(glIsEnabledi(GL_BLEND, i));
        GLboolean mask4[4]{};
        glGetBooleani_v(GL_COLOR_WRITEMASK, i, mask4);
        for (const auto b : mask4)
            result.push_back(b);
        for (const GLenum p :
             {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA,
              GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA}) {
            GLint value{};
            glGetIntegeri_v(p, i, &value);
            result.push_back(value);
        }
    }
    glActiveTexture(active);
    return result;
}
Pixels pixels(unsigned attachment = 0) {
    Pixels p{};
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
    glReadPixels(0, 0, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
    for (const auto value : p) {
        pixel_hash ^= value;
        pixel_hash *= 1099511628211ull;
    }
    return p;
}
std::size_t color_count(const Pixels &p, std::array<std::uint8_t, 4> expected, int tolerance = 0) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < p.size(); i += 4) {
        bool matched = p[i + 3] == expected[3];
        for (unsigned channel = 0; channel < 3; ++channel)
            matched = matched && std::abs(int(p[i + channel]) - expected[channel]) <= tolerance;
        if (matched)
            ++count;
    }
    return count;
}
void clear() {
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glStencilMask(0xff);
    for (unsigned i = 0; i < 4; ++i) {
        glColorMaski(i, 1, 1, 1, 1);
        glDisablei(GL_BLEND, i);
    }
    glClearColor(0, 0, 0, 0);
    glClearDepth(1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

struct Resources {
    GLuint fbo{}, colors[4]{}, depth{}, vao{}, buffer{}, ebo{}, sampler{}, sentinel_shader{},
        texture{}, hud{};
    unsigned tw{}, th{}, hw{}, hh{};
    Renderer adapter;
    Mesh mesh;
    std::array<Vertex, 4> quad{{{-.5f, -.5f, 0, 0, 0, 0xffffffff},
                                {.5f, -.5f, 0, 1, 0, 0xffffffff},
                                {.5f, .5f, 0, 1, 1, 0xffffffff},
                                {-.5f, .5f, 0, 0, 1, 0xffffffff}}};
    Resources() {
        goldcraft::render::renderer = &adapter;
        goldcraft::render::current_context = wglGetCurrentContext();
        goldcraft::render::logger = log_message;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(4, colors);
        for (unsigned i = 0; i < 4; ++i) {
            glBindTexture(GL_TEXTURE_2D, colors[i]);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, Size, Size);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D,
                                   colors[i], 0);
        }
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, Size, Size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                  depth);
        const GLenum targets[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                  GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, targets);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glViewport(0, 0, Size, Size);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, 256, nullptr, GL_STATIC_DRAW);
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, 256, nullptr, GL_STATIC_DRAW);
        glGenSamplers(1, &sampler);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        sentinel_shader = make_program(nullptr);
        const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        upload_texture(texture, tw, th, 1, 1, white);
        const std::array<std::uint8_t, 4> blue{51, 102, 153, 255};
        upload_texture(hud, hw, hh, 1, 1, blue);
        assert(glGetError() == GL_NO_ERROR);
    }
    void sentinel() {
        glUseProgram(sentinel_shader);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hud);
        glBindSampler(0, sampler);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindSampler(3, sampler);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        glEnable(GL_STENCIL_TEST);
        glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 3, 0x3f);
        glStencilFuncSeparate(GL_BACK, GL_NOTEQUAL, 6, 0x7f);
        glStencilMaskSeparate(GL_FRONT, 0x13);
        glStencilMaskSeparate(GL_BACK, 0x47);
        glStencilOpSeparate(GL_FRONT, GL_REPLACE, GL_INCR, GL_DECR);
        glStencilOpSeparate(GL_BACK, GL_INVERT, GL_KEEP, GL_ZERO);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2, 3);
        glLineWidth(1);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, Size, Size);
        for (unsigned i = 0; i < 4; ++i) {
            if (i & 1)
                glEnablei(GL_BLEND, i);
            else
                glDisablei(GL_BLEND, i);
            glBlendFuncSeparatei(i, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            glBlendEquationSeparatei(i, GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
            glColorMaski(i, 1, i & 1, 0, 1);
        }
    }
    void scene_pass(bool transparent, std::uint32_t flags, bool feedback = false) {
        Pass pass(transparent, flags);
        assert(pass.ready());
        pass.texture(texture);
        for (int i = 0; i < 24; ++i) {
            pass.texture(texture);
            pass.emissive(false);
            pass.depth_write(!transparent);
        }
        pass.emissive(true);
        pass.emissive(true);
        mesh.draw(quad, 1, false);
        if (feedback) {
            pass.feedback(1);
            mesh.draw(quad, 1, false);
            pass.feedback(2);
            mesh.draw(quad, 1, false);
        }
    }
    ~Resources() {
        shutdown();
        glUseProgram(0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteProgram(sentinel_shader);
        glDeleteSamplers(1, &sampler);
        glDeleteBuffers(1, &buffer);
        glDeleteBuffers(1, &ebo);
        glDeleteVertexArrays(1, &vao);
        glDeleteRenderbuffers(1, &depth);
        glDeleteTextures(4, colors);
        glDeleteTextures(1, &texture);
        glDeleteTextures(1, &hud);
        glDeleteFramebuffers(1, &fbo);
    }
};

template <class Draw> void measure(const char *name, Draw draw) {
    constexpr unsigned iterations = 256;
    draw();
    glFinish(); // Warm shader/resource/driver caches; never inside measured work.
    const auto initial_uploads = statistics().uploads;
    probe::reset();
    probe::count_allocations = true;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i)
        draw();
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
            .count() /
        iterations;
    probe::count_allocations = false;
#ifndef GOLDCRAFT_RENDER_BASELINE
    assert(probe::allocations == 0 && probe::locations == 0);
    assert(probe::bindings == 2u * iterations);
#endif
    assert(statistics().uploads == initial_uploads);
    std::printf("{\"renderBackendGL\":true,\"case\":\"%s\",\"iterations\":%u,\"allocations\":%zu,"
                "\"stateQueries\":%zu,\"uniformLocations\":%zu,\"uniformWrites\":%zu,"
                "\"textureBinds\":%zu,\"uploads\":0,\"submitCpuUs\":%.3f,\"passed\":true}\n",
                name, iterations, probe::allocations, probe::queries, probe::locations,
                probe::uniforms, probe::bindings, us);
}

void run(bool benchmark) {
    Resources r;
    for (const auto flags :
         {0u, std::uint32_t(META_SCENE_GBUFFER), std::uint32_t(META_SCENE_SHADOW),
          std::uint32_t(META_SCENE_SHADOW | META_SCENE_LINEAR_DEPTH),
          std::uint32_t(META_SCENE_MULTIVIEW | META_SCENE_SHADOW)}) {
        clear();
        r.sentinel();
        const auto before = snapshot();
        r.scene_pass(false, flags);
        assert(snapshot() == before);
        assert(color_count(pixels(), {0, 255, 0, 255}) == 1024);
        if (flags & META_SCENE_GBUFFER) {
            // UNORM conversion can choose the adjacent code at a half step.
            // The comparison report also checks the full byte readback hash.
            assert(color_count(pixels(1), {64, 128, 191, 255}, 1) == 1024);
            assert(color_count(pixels(2), {128, 128, 255, 255}, 1) == 1024);
            assert(color_count(pixels(3), {0, 0, 0, 255}) == 1024);
        }
        assert(glGetError() == GL_NO_ERROR);
    }
    clear();
    r.sentinel();
    auto before = snapshot();
    r.scene_pass(true, 0, true);
    assert(snapshot() == before);
    assert(color_count(pixels(), {255, 255, 255, 255}) == 1024);
    assert(glGetError() == GL_NO_ERROR);
    // Opaque scopes normally preserve blend parameters without reading them;
    // an unusual feedback draw still has to restore the lazily captured state.
    r.scene_pass(false, 0, true);
    assert(snapshot() == before);
    clear();
    r.sentinel();
    before = snapshot();
    assert(draw_hud(r.hud, .25f, .25f, false, Size, Size));
    assert(snapshot() == before);
    assert(color_count(pixels(), {51, 102, 153, 255}) == Size * Size);
    assert(draw_hud(r.hud, .25f, .25f, true, Size, Size));
    assert(snapshot() == before);
    assert(color_count(pixels(), {51, 102, 153, 255}) < Size * Size);
    // Force different host state between scopes. No across-callback state cache.
    glDepthFunc(GL_GEQUAL);
    glStencilFuncSeparate(GL_FRONT, GL_NEVER, 9, 0x55);
    glLineWidth(2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, r.hud);
    glDisable(GL_POLYGON_OFFSET_FILL);
    before = snapshot();
    r.scene_pass(false, 0);
    assert(snapshot() == before);
    r.scene_pass(true, 0, true);
    assert(snapshot() == before);
    assert(draw_hud(r.hud, 0, 0, false, Size, Size));
    assert(snapshot() == before);
    // Non-default pixel-store state and a bound PBO must survive client uploads.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, r.buffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 19);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    before = snapshot();
    probe::reset();
    std::array<std::uint8_t, 16> white{};
    white.fill(255);
    upload_texture(r.texture, r.tw, r.th, 1, 1, {white.data(), 4});
    upload_texture(r.texture, r.tw, r.th, 2, 2, white);
    assert(snapshot() == before);
#ifndef GOLDCRAFT_RENDER_BASELINE
    assert(probe::texture_parameters == 0 && probe::queries == 12);
#endif
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    assert(glGetError() == GL_NO_ERROR);
    if (benchmark) {
        r.sentinel();
        const auto initial = snapshot();
        measure("opaque", [&] { r.scene_pass(false, 0); });
        assert(snapshot() == initial);
        measure("gbuffer", [&] { r.scene_pass(false, META_SCENE_GBUFFER); });
        assert(snapshot() == initial);
        measure("feedback", [&] { r.scene_pass(true, 0, true); });
        assert(snapshot() == initial);
        measure("hud", [&] { assert(draw_hud(r.hud, .25f, .25f, true, Size, Size)); });
        assert(snapshot() == initial);
    }
    assert(glGetError() == GL_NO_ERROR);
}
} // namespace

int main() {
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    for (int i = 0; i < 2; ++i) {
        auto *window = glfwCreateWindow(Size, Size, "GoldCraft GL regression", nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            return 2;
        }
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        const auto loaded = glewInit();
        while (glGetError() != GL_NO_ERROR) {
        }
        assert(loaded == GLEW_OK && GLEW_VERSION_4_4);
        if (i == 0)
            std::printf("{\"glVendor\":\"%s\",\"glRenderer\":\"%s\",\"glVersion\":\"%s\"}\n",
                        glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
        run(i == 0);
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    std::printf(
        "{\"renderBackendStateRestored\":true,\"contexts\":2,\"quadPixels\":1024,\"hudPixels\":"
        "4096,\"feedbackPixels\":1024,\"pixelStoreRestored\":true,\"pixelHash\":\"%016llx\","
        "\"passed\":true}\n",
        pixel_hash);
}
