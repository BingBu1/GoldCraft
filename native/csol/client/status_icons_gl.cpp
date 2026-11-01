// SPDX-License-Identifier: GPL-3.0-or-later
#include "status_icons.hpp"
#include "gl_state.hpp"
#include "assets.hpp"

namespace csol::client
{
namespace
{
GLuint program{}, vao{}, vbo{};
GLint viewport{};
bool attempted{};
bool compatibility{};
HGLRC context{};
struct StatusImage
{
    GLuint texture{};
    bool attempted{};
};
std::array<StatusImage, status::ImageCount> images{};
std::array<status::ImageSize, status::ImageCount> imageSizes{};

struct StatusIconVertex { float x, y, u, v, alpha; };
constexpr auto VertexCapacity = status::Capacity * 2 * 6;

GLuint Compile(GLenum kind, const char* source)
{
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok{};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(shader); throw std::runtime_error("status icon shader compilation failed"); }
    return shader;
}
}

void ResetStatusIconGraphics()
{
    // GL names belong to the context that created them, not a new context
    // that happens to reuse the same integer texture/program names.
    // ClearScene can reset the shared glewReady flag before this owner is
    // reset. Our creation context proves these GL entry points were loaded.
    if (context && wglGetCurrentContext() == context)
    {
        for (const auto& image : images)
            if (image.texture) glDeleteTextures(1, &image.texture);
        if (program) glDeleteProgram(program);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
    }
    program = vao = vbo = 0;
    images = {};
    imageSizes = {};
    context = nullptr;
    attempted = false;
    compatibility = false;
}

void PrepareStatusIconGraphics(const status::StatusIcons& icons)
{
    if (context && context != wglGetCurrentContext()) ResetStatusIconGraphics();
    if (attempted && !program) return;
    bool pending{};
    for (const auto& icon : icons.Items())
        if (icon.visible && !images[icon.type].attempted) { pending = true; break; }
    if (!pending || !EnsureGraphics()) return;
    context = wglGetCurrentContext();
    if (!context) return;
    GLState saved;
    GLuint vertex{}, fragment{};
    if (!attempted)
    {
        attempted = true;
        try
        {
            GLint profile{}; glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
            compatibility = (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
            vertex = Compile(GL_VERTEX_SHADER, R"(#version 330 core
    layout(location=0) in vec4 point; layout(location=1) in float opacity;
    uniform vec2 viewportSize; out vec2 uv; out float alpha;
    void main(){uv=point.zw;alpha=opacity;gl_Position=vec4(point.x*2/viewportSize.x-1,1-point.y*2/viewportSize.y,0,1);})");
            fragment = Compile(GL_FRAGMENT_SHADER, R"(#version 330 core
    in vec2 uv; in float alpha; uniform sampler2D image; out vec4 color;
    void main(){color=texture(image,uv);color.a*=alpha;})");
            program = glCreateProgram();
            glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
            glDeleteShader(vertex); vertex = 0;
            glDeleteShader(fragment); fragment = 0;
            GLint ok{}; glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) throw std::runtime_error("status icon shader link failed");
            viewport = glGetUniformLocation(program, "viewportSize");
            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "image"), 0);
            glGenVertexArrays(1, &vao); glBindVertexArray(vao);
            glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(StatusIconVertex) * VertexCapacity, nullptr, GL_STREAM_DRAW);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(StatusIconVertex), nullptr);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(StatusIconVertex),
                reinterpret_cast<void*>(offsetof(StatusIconVertex, alpha)));
        }
        catch (const std::exception& error)
        {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            ResetStatusIconGraphics();
            attempted = true; // Retry on the next video/session resource reset.
            engine.Con_DPrintf("CSOLSupport: status icons: %s\n", error.what());
            return;
        }
    }
    // Decode/upload only newly visible types, outside HUD_Redraw. A missing
    // image must not hide unrelated statuses or retry every frame.
    for (const auto& icon : icons.Items())
    {
        if (!icon.visible) continue;
        auto& cached = images[icon.type];
        if (cached.attempted) continue;
        cached.attempted = true;
        try
        {
            const auto path = std::string(status::ImagePrefix) + std::to_string(icon.type) + ".tga";
            const auto image = DecodeTGA(ReadResource(path.c_str()));
            cached.texture = UploadImage(image);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            imageSizes[icon.type] = {image.Width, image.Height};
        }
        catch (const std::exception& error)
        {
            if (cached.texture) glDeleteTextures(1, &cached.texture);
            cached.texture = 0;
            engine.Con_DPrintf("CSOLSupport: status icon %d: %s\n", icon.type, error.what());
        }
    }
}

void DrawStatusIconGraphics(const status::StatusIcons& icons, int width, int height)
{
    if (!program || context != wglGetCurrentContext()) return;
    std::array<status::Quad, status::Capacity * 2> quads;
    const auto count = icons.Layout(width, height, imageSizes, quads);
    if (!count) return;
    std::array<StatusIconVertex, VertexCapacity> vertices;
    std::size_t used{};
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& q = quads[i];
        const StatusIconVertex tl{q.left,q.top,0,q.v0,q.alpha}, tr{q.right,q.top,q.u1,q.v0,q.alpha};
        const StatusIconVertex bl{q.left,q.bottom,0,q.v1,q.alpha}, br{q.right,q.bottom,q.u1,q.v1,q.alpha};
        for (const auto& v : {tl,tr,br,tl,br,bl}) vertices[used++] = v;
    }
    GLState saved;
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    // Alpha test still applies after a fragment shader in a compatibility
    // context. A stock HUD threshold must not discard the alpha70 dark area.
    // The removed enum must never be queried in a core context.
    const GLboolean alphaTest = compatibility ? glIsEnabled(GL_ALPHA_TEST) : GL_FALSE;
    if (compatibility) glDisable(GL_ALPHA_TEST);
    GLboolean colorMask[4]; glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program); glUniform2f(viewport, float(width), float(height));
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glActiveTexture(GL_TEXTURE0);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(StatusIconVertex) * used, vertices.data());
    // Preserve panel order while batching adjacent slices of the same image.
    // The entire frame uses one upload and fixed-capacity stack storage.
    for (std::size_t first = 0; first < count;)
    {
        std::size_t end = first + 1;
        while (end < count && quads[end].type == quads[first].type) ++end;
        glBindTexture(GL_TEXTURE_2D, images[quads[first].type].texture);
        glDrawArrays(GL_TRIANGLES, GLint(first * 6), GLsizei((end - first) * 6));
        first = end;
    }
    GLState::Enable(GL_SCISSOR_TEST, scissor);
    if (compatibility) GLState::Enable(GL_ALPHA_TEST, alphaTest);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
}
}
