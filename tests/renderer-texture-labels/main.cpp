// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>
#include <cvardef.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Label = void (APIENTRY*)(GLenum, GLuint, GLsizei, const char*);
using ReadLabel = void (APIENTRY*)(GLenum, GLuint, GLsizei, GLsizei*, char*);
using GenerateMip = void (APIENTRY*)(GLenum);
Label driverLabel{};
unsigned labels{}, formats{}, checks{};
cvar_t* r_debug_texture_names{};
void APIENTRY RecordLabel(GLenum kind, GLuint id, GLsizei size, const char* name)
{
    ++labels;
    driverLabel(kind, id, size, name);
}
Label glObjectLabel{};
int Format(char* output, std::size_t size, const char* pattern, va_list args)
{
    ++formats;
    return _vsnprintf(output, size, pattern, args);
}
#define _vsnprintf Format
#include "texture_label_functions.inc"
#undef _vsnprintf

void Check(bool condition)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "texture label check %u failed\n", checks); std::exit(1); }
}
std::string Name(ReadLabel read, GLuint texture)
{
    std::array<char, 512> text{};
    GLsizei length{};
    read(GL_TEXTURE, texture, GLsizei(text.size()), &length, text.data());
    Check(glGetError() == GL_NO_ERROR && length >= 0 && std::size_t(length) < text.size());
    return {text.data(), std::size_t(length)};
}

int main()
{
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "GoldCraftTextureLabelTest";
    wc.style = CS_OWNDC;
    Check(RegisterClassA(&wc) != 0);
    const auto window = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr && !IsWindowVisible(window));
    const auto dc = GetDC(window);
    PIXELFORMATDESCRIPTOR pixel{sizeof(pixel), 1};
    pixel.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixel.iPixelType = PFD_TYPE_RGBA; pixel.cColorBits = 32; pixel.cDepthBits = 24;
    const auto format = ChoosePixelFormat(dc, &pixel);
    Check(format && SetPixelFormat(dc, format, &pixel));
    const auto context = wglCreateContext(dc);
    Check(context && wglMakeCurrent(dc, context));
    driverLabel = reinterpret_cast<Label>(wglGetProcAddress("glObjectLabel"));
    const auto readLabel = reinterpret_cast<ReadLabel>(wglGetProcAddress("glGetObjectLabel"));
    const auto mip = reinterpret_cast<GenerateMip>(wglGetProcAddress("glGenerateMipmap"));
    Check(driverLabel && readLabel && mip);
    std::printf("{\"renderer\":\"%s\",\"version\":\"%s\"}\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    std::array<unsigned char, 16 * 16 * 4> image{}, readback{};
    for (std::size_t i = 0; i < image.size(); ++i) image[i] = static_cast<unsigned char>(i * 31);
    GLuint texture{};
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    Check(glGetError() == GL_NO_ERROR);
    glObjectLabel = RecordLabel;
    // Missing cold registration and zero both avoid formatting and driver work.
    GL_SetTextureDebugName(texture, "before init");
    GL_SetTextureDebugNameFormat(texture, "%s %d", "before init", 1);
    cvar_t option{};
    r_debug_texture_names = &option;
    GL_SetTextureDebugName(texture, "disabled");
    GL_SetTextureDebugNameFormat(texture, "%s %d", "disabled", 2);
    Check(labels == 0 && formats == 0 && Name(readLabel, texture).empty());

    option.value = 1;
    GL_SetTextureDebugName(texture, "enabled");
    Check(Name(readLabel, texture) == "enabled" && labels == 1 && formats == 0);
    GL_SetTextureDebugNameFormat(texture, "%s %d", "CSOL texture", 7);
    Check(Name(readLabel, texture) == "CSOL texture 7" && labels == 2 && formats == 1);
    const std::string longName(512, 'x');
    GL_SetTextureDebugNameFormat(texture, "%s", longName.c_str());
    Check(Name(readLabel, texture) == std::string(255, 'x') && labels == 3 && formats == 2);
    glObjectLabel = nullptr;
    GL_SetTextureDebugNameFormat(texture, "%d", 42);
    Check(labels == 3 && formats == 2);
    glObjectLabel = RecordLabel; option.value = 0;
    GL_SetTextureDebugNameFormat(texture, "ignored");
    Check(labels == 3 && formats == 2);
    GLint binding{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    Check(binding == GLint(texture) && image == readback && glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &texture);

    constexpr int count = 192, size = 256;
    std::vector<unsigned char> pixels(size * size * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<unsigned char>(i * 131 >> 3);
    for (int round = -1; round < 3; ++round)
        for (int mode = 0; mode < (round < 0 ? 1 : 2); ++mode)
        {
            option.value = float(mode);
            const auto beforeLabels = labels, beforeFormats = formats;
            std::array<GLuint, count> textures{};
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < count; ++i)
            {
                glGenTextures(1, &textures[i]); glBindTexture(GL_TEXTURE_2D, textures[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                pixels[i] ^= 0x5a;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                mip(GL_TEXTURE_2D);
                GL_SetTextureDebugNameFormat(textures[i], "GL_LoadTextureEx - models/check%d.mdl", i);
            }
            const auto queued = std::chrono::steady_clock::now();
            glFinish();
            const auto done = std::chrono::steady_clock::now();
            Check(labels - beforeLabels == unsigned(mode * count) && formats - beforeFormats == unsigned(mode * count));
            Check(glGetError() == GL_NO_ERROR);
            glDeleteTextures(count, textures.data()); glFinish();
            if (round >= 0)
                std::printf("{\"round\":%d,\"enabled\":%d,\"textures\":%d,\"size\":%d,\"queueMs\":%.3f,\"finishMs\":%.3f,\"totalMs\":%.3f}\n",
                    round, mode, count, size, std::chrono::duration<double, std::milli>(queued - start).count(),
                    std::chrono::duration<double, std::milli>(done - queued).count(),
                    std::chrono::duration<double, std::milli>(done - start).count());
        }
    Check(wglMakeCurrent(nullptr, nullptr) && wglDeleteContext(context));
    ReleaseDC(window, dc); DestroyWindow(window); UnregisterClassA(wc.lpszClassName, wc.hInstance);
    std::printf("{\"checks\":%u,\"labels\":%u,\"formats\":%u,\"passed\":true,\"gameplay\":false}\n", checks, labels, formats);
}
