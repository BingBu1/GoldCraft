#pragma once
#include <windows.h>
#include <GL/glew.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <IMetaRendererScene.h>
#include "goldcraft/atlas.hpp"

namespace goldcraft::render {
struct Vertex { float x,y,z,u,v; std::uint32_t color,overlay=0; };
static_assert(sizeof(Vertex)==28);

class Mesh {
public:
    Mesh()=default;
    Mesh(const Mesh&)=delete;
    Mesh& operator=(const Mesh&)=delete;
    ~Mesh();
    void draw(std::span<const Vertex> vertices, std::uint64_t revision, bool dynamic, bool lines=false);
private:
    HGLRC context_=nullptr;
    GLuint vao_=0,vbo_=0,ebo_=0;
    std::uint64_t revision_=0;
    std::size_t vertices_=0,capacity_=0,index_capacity_=0;
};

struct Statistics {
    std::uint64_t uploads=0,upload_bytes=0,draws=0,opaque_passes=0,shadow_passes=0;
    GLenum error=0;
    bool core=false,renderer=false,scene_api=false;
};
const Statistics& statistics();
bool initialize(IMetaRendererSceneCallbacks* callbacks, void(*log)(const std::string&));
void shutdown();
bool scene_active();
bool owns_context();
void set_light_shadow(int key, unsigned size);
void upload_texture(GLuint& id,unsigned& width,unsigned& height,unsigned next_width,unsigned next_height,std::span<const std::uint8_t> rgba);
void upload_atlas_patches(GLuint id,std::span<const AtlasPatch> patches);
bool capture(const char* path);
bool draw_hud(GLuint texture,float mouse_x,float mouse_y,bool menu,unsigned width,unsigned height);
struct DrawState;

class Pass {
public:
    Pass(bool transparent, std::uint32_t flags);
    ~Pass();
    bool ready() const {return ready_;}
    void texture(GLuint id);
    void emissive(bool value);
    void depth_write(bool value);
    void feedback(unsigned mode);
private:
    DrawState* state_=nullptr;
    bool ready_=false;
    GLint emissive_location_=-1;
    bool shadow_=false;
};
}
