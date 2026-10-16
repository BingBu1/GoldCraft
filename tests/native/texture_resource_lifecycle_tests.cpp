#include "goldcraft/particle_texture_chunks.hpp"
#include "goldcraft/particles.hpp"
#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace goldcraft;
namespace {
struct Texture {unsigned id{},width{},height{};};
std::unordered_map<unsigned,Texture> particle_textures;
ParticleTextureAssembler particle_texture_upload;
std::uint64_t world=11,particle_generation=2,particle_texture_transfer=0,particle_texture_chunk_uploads=0;
unsigned server_player_life=3,camera_revision=0;
double minecraft_transition_until=0,last_camera=0;
bool server_minecraft_form=true,minecraft_control=true,minecraft_menu=false,ui_active=false,compatible=true;
struct Clearable {void clear(){}} camera_interpolator,view_interpolator,native_motion,native_vision;
struct Engine {void(*pfnClientCmd)(char*)=+[](char*){};} gEngfuncs;
void ClearPostEffects(){} void ClearEntityFrame(){} void ReleaseMinecraftKeys(){}
void ReleaseHostUse(){} void ClearHud(){} void UpdateViewModel(){} void Log(const std::string&){}
struct Trace {void observe(int){}} control_trace;
int SampleControl(){return 0;} double Seconds(){return 20;}
bool MatchingHostProtocol(){return compatible;}
enum class WorkStage {chunk_first_append,chunk_continuation_append,chunk_gpu_upload,chunk_publish_release,chunk_failed_release};
struct WorkScope {explicit WorkScope(WorkStage){}};
constexpr int GL_NO_ERROR=0;
int glGetError(){return GL_NO_ERROR;} void glDeleteTextures(int,const unsigned*){}
Bytes gpu_image;unsigned gpu_uploads=0;
namespace render {
void upload_texture(unsigned& id,unsigned& width,unsigned& height,unsigned w,unsigned h,std::span<const std::uint8_t> image){
    id=++gpu_uploads;width=w;height=h;gpu_image.assign(image.begin(),image.end());
}
}
void ClearParticles(){particle_textures.clear();particle_texture_upload.reset();particle_texture_transfer=0;particle_generation=0;}
#include "texture_resource_lifecycle.inc"
Bytes chunk(unsigned offset,unsigned life=3,std::uint64_t epoch=11,std::uint64_t generation=2,std::uint64_t revision=7){
    Writer w;w.u64(epoch);w.u64(generation);w.u32(life);w.u32(1);w.u64(revision);
    w.u32(4096);w.u32(128);w.u32(offset);w.u32(2*1024*1024);
    for(unsigned i=0;i<262144;++i)w.u8(static_cast<std::uint8_t>((offset+i)*31+(offset+i)/1024));
    return w.data;
}
void receive(const Bytes& bytes){Reader r(bytes);ReadParticleTextureChunk(r);}
void form(unsigned life,unsigned value){Writer w;w.u64(world);w.u32(life);w.u32(value);FormMessage("GCForm",int(w.data.size()),w.data.data());}
}
int main(){
    receive(chunk(0));assert(particle_texture_upload.received_bytes()==262144&&gpu_uploads==0);
    form(4,1);assert(server_player_life==4&&!minecraft_control);
    assert(particle_texture_upload.received_bytes()==262144); // Exact production GCForm path.
    for(unsigned offset=262144;offset<2*1024*1024;offset+=262144)receive(chunk(offset));
    assert(gpu_uploads==1&&particle_texture_transfer==7&&gpu_image.size()==2*1024*1024);
    for(unsigned i=0;i<gpu_image.size();++i)assert(gpu_image[i]==static_cast<std::uint8_t>(i*31+i/1024));
    ParticleSnapshot old{world,particle_generation,1,3,0,{}};
    assert(!current_particles(old,world,particle_generation,0,4));
    old.life=4;assert(current_particles(old,world,particle_generation,0,4));
    form(5,0);assert(particle_textures.size()==1&&particle_texture_transfer==7);
    receive(chunk(0,3,12));receive(chunk(0,3,11,1));assert(gpu_uploads==1);
    compatible=false;receive(chunk(0,3,11,2,8));compatible=true;
    assert(particle_texture_upload.received_bytes()==0);
    bool rejected=false;try{receive(chunk(0));}catch(const ProtocolError&){rejected=true;}assert(rejected);
    // A true resource generation change retires old images/assembly and old tails.
    receive(chunk(0,5,11,3,8));assert(particle_textures.empty()&&particle_texture_upload.received_bytes()==262144);
    receive(chunk(262144,3,11,2,7));assert(particle_texture_upload.received_bytes()==262144);
    std::cout<<"PASS exact GCForm/chunk paths retain immutable resources across life, full bytes preserved, stale effect life/map/generation/transfer rejected\n";
}
