// Exact plugin scene-light functions are included below. Only the connection,
// Renderer provider and GoldSrc dynamic-light endpoint are fixtures. No game or
// GL context is launched; this checks state/transport lifecycle, not brightness.
#include "goldcraft/lights.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <span>
#include <utility>
#include <vector>

using namespace goldcraft;
namespace {
unsigned checks{};
void check(bool condition){++checks;assert(condition);}
struct Link {
    bool online=true;
    std::uint64_t current_generation=7;
    bool connected()const{return online;}
    std::uint64_t generation()const{return current_generation;}
} link;
std::uint64_t world=42,last_link_generation=7;

namespace render {
struct Statistics {bool scene_lights_api=true;};
Statistics state;
unsigned updates{},clears{};
bool accept=true;
std::vector<SceneLight> submitted;
std::vector<std::pair<unsigned,unsigned>> shadows;
Statistics statistics(){return state;}
bool update_scene_lights(std::span<const SceneLight> lights){
    ++updates;
    if(!accept)return false;
    submitted.assign(lights.begin(),lights.end());return true;
}
void clear_scene_lights(){++clears;submitted.clear();}
void set_light_shadow(unsigned key,unsigned resolution){shadows.emplace_back(key,resolution);}
}

// Field types used by the extracted algorithm are deliberately simple; this
// fixture makes no engine ABI/layout claim and never dereferences SDK memory.
struct DLight {
    std::array<float,3> origin{};
    float radius{},die{},decay{},minlight{};
    struct Color {unsigned r{},g{},b{};} color;
    int dark{};
};
std::array<DLight,12> dlights;
std::vector<unsigned> allocated_keys;
unsigned out_of_range_allocations{};
int null_slot=-1;
float client_time=20.f;
DLight* allocate_dlight(unsigned key){
    allocated_keys.push_back(key);
    if(key<0x47430000||key>=0x47430000+dlights.size()){
        ++out_of_range_allocations;return nullptr;
    }
    const unsigned slot=key-0x47430000;
    if(static_cast<int>(slot)==null_slot)return nullptr;
    return &dlights[slot];
}
float get_client_time(){return client_time;}
struct Effects {DLight*(*CL_AllocDlight)(unsigned)=allocate_dlight;} effects;
struct Engine {
    Effects* pEfxAPI=&effects;
    float(*GetClientTime)()=get_client_time;
} gEngfuncs;

#include "plugin_lights.inc"

Bytes snapshot(unsigned count,std::uint64_t epoch=42,float offset=0.f){
    Writer writer;writer.u64(epoch);writer.u32(count);
    for(unsigned i=0;i<count;++i){
        writer.f32(offset+static_cast<float>(i));writer.f32(64.f);writer.f32(-static_cast<float>(i));
        writer.f32(480.f);writer.u32(0xff9cd6ff);
    }
    return writer.data;
}
void read(const Bytes& bytes){Reader reader(bytes);ReadLights(reader);}
void fresh(){last_lamps=std::chrono::steady_clock::now()+std::chrono::hours(1);}
void expired(){last_lamps=std::chrono::steady_clock::now()-std::chrono::seconds(1);}
void reset(){
    ClearLights();world=42;last_link_generation=7;link={};
    render::state={};render::accept=true;render::updates=render::clears=0;
    render::submitted.clear();render::shadows.clear();
    dlights={};allocated_keys.clear();out_of_range_allocations=0;null_slot=-1;
    client_time=20.f;effects={};gEngfuncs={};
}
void rejects_without_replacing(const Bytes& bytes){
    const auto before=lamps;
    const auto timestamp=last_lamps;
    const bool dirty=lamps_dirty,published=lamps_published;
    bool rejected=false;
    try{read(bytes);}catch(const ProtocolError&){rejected=true;}
    check(rejected);check(lamps==before);check(last_lamps==timestamp);
    check(lamps_dirty==dirty&&lamps_published==published);
}
void full_provider_and_unchanged_snapshots(){
    reset();const auto bytes=snapshot(1025);read(bytes);fresh();
    check(lamps.size()==1025&&lamps_dirty&&!lamps_published);UpdateLights();
    check(render::updates==1&&render::submitted.size()==1025);
    check(!lamps_dirty&&lamps_published);check(allocated_keys.empty()&&render::shadows.empty());
    for(unsigned i=0;i<1025;++i){
        check(render::submitted[i].origin==std::array<float,3>{static_cast<float>(i)*32.f,static_cast<float>(i)*32.f,0.f});
        check(render::submitted[i].radius==480.f&&render::submitted[i].color==0xff9cd6ff);
    }
    for(unsigned i=0;i<20;++i)UpdateLights();
    check(render::updates==1&&render::clears==0&&allocated_keys.empty());
    read(bytes);fresh();UpdateLights();check(render::updates==1&&!lamps_dirty);
    read(snapshot(1025,42,1.f));fresh();UpdateLights();
    check(render::updates==2&&render::submitted.front().origin[0]==32.f);
    read(snapshot(0));fresh();UpdateLights();
    check(render::updates==3&&render::submitted.empty()&&lamps_published);
}
void stale_disconnected_generation_and_world(){
    for(unsigned failure=0;failure<4;++failure){
        reset();read(snapshot(40));fresh();UpdateLights();
        if(failure==0)expired();
        else if(failure==1)link.online=false;
        else if(failure==2)++link.current_generation;
        else world=0;
        check(!FreshLights());UpdateLights();
        check(render::updates==1&&render::clears==1&&render::submitted.empty()&&!lamps_published);
        UpdateLights();check(render::updates==1&&render::clears==1&&allocated_keys.empty());
    }
    reset();read(snapshot(40));fresh();UpdateLights();expired();UpdateLights();
    read(snapshot(40));fresh();UpdateLights();
    check(render::updates==2&&render::submitted.size()==40&&lamps_published);
}
void clear_resets_all_state(){
    reset();read(snapshot(40));fresh();UpdateLights();ClearLights();
    check(lamps.empty()&&!lamps_dirty&&!lamps_published&&last_lamps==std::chrono::steady_clock::time_point{});
    check(render::clears==1&&render::submitted.empty());UpdateLights();
    check(render::updates==1&&render::clears==1&&allocated_keys.empty());
    world=43;read(snapshot(40));check(lamps.empty());read(snapshot(2,43));fresh();UpdateLights();
    check(lamps.size()==2&&render::submitted.size()==2&&render::updates==2);
}
void provider_failure_remains_retryable(){
    reset();read(snapshot(65));fresh();render::accept=false;UpdateLights();
    check(render::updates==1&&lamps_dirty&&!lamps_published&&render::submitted.empty());
    UpdateLights();check(render::updates==2&&lamps_dirty&&!lamps_published);
    render::accept=true;UpdateLights();check(render::updates==3&&!lamps_dirty&&lamps_published&&render::submitted.size()==65);
    const auto accepted=render::submitted;read(snapshot(66));fresh();render::accept=false;UpdateLights();
    check(render::updates==4&&lamps_dirty&&lamps_published&&render::submitted==accepted);
    render::accept=true;UpdateLights();check(render::updates==5&&!lamps_dirty&&render::submitted.size()==66);
    check(allocated_keys.empty());
}
void old_renderer_is_bounded_and_stops_renewing(){
    reset();render::state.scene_lights_api=false;read(snapshot(65));fresh();UpdateLights();
    check(render::updates==0&&allocated_keys.size()==12&&out_of_range_allocations==0);
    check(render::shadows.size()==12);
    for(unsigned i=0;i<12;++i){
        check(allocated_keys[i]==0x47430000+i);
        check(render::shadows[i]==std::pair<unsigned,unsigned>{0x47430000+i,256});
        const auto& light=dlights[i];
        check(light.origin==std::array<float,3>{static_cast<float>(i)*32.f,static_cast<float>(i)*32.f,0.f});
        check(light.radius==480.f&&light.color.r==255&&light.color.g==214&&light.color.b==156);
        check(light.die==20.2f&&light.decay==0&&light.minlight==0&&light.dark==0);
    }
    for(unsigned failure=0;failure<3;++failure){
        link.online=true;link.current_generation=last_link_generation;fresh();
        if(failure==0)expired();else if(failure==1)link.online=false;else ++link.current_generation;
        allocated_keys.clear();render::shadows.clear();client_time+=1.f;UpdateLights();
        check(allocated_keys.empty()&&render::shadows.size()==12);
        for(const auto& shadow:render::shadows)check(shadow.second==0);
        for(const auto& light:dlights)check(light.die==20.2f);
    }
    reset();render::state.scene_lights_api=false;read(snapshot(2));fresh();UpdateLights();
    check(allocated_keys.size()==2&&render::shadows.size()==12);
    for(unsigned i=0;i<12;++i)check(render::shadows[i].second==(i<2?256u:0u));
    ClearLights();allocated_keys.clear();UpdateLights();check(allocated_keys.empty());
}
void optional_allocator_is_safe(){
    reset();render::state.scene_lights_api=false;read(snapshot(40));fresh();null_slot=4;UpdateLights();
    check(allocated_keys.size()==12&&out_of_range_allocations==0&&dlights[4].radius==0&&dlights[5].radius==480);
    allocated_keys.clear();effects.CL_AllocDlight=nullptr;UpdateLights();check(allocated_keys.empty());
    gEngfuncs.pEfxAPI=nullptr;UpdateLights();check(allocated_keys.empty());
}
void invalid_frames_keep_the_accepted_snapshot(){
    reset();read(snapshot(40));fresh();UpdateLights();const auto before=lamps;const auto timestamp=last_lamps;
    read(snapshot(99,43));check(lamps==before&&last_lamps==timestamp&&!lamps_dirty&&lamps_published);
    world=0;read(snapshot(99,0));check(lamps==before&&last_lamps==timestamp);world=42;
    const auto valid=snapshot(2);
    for(std::size_t size=0;size<valid.size();++size)rejects_without_replacing(Bytes(valid.begin(),valid.begin()+size));
    auto extra=valid;extra.push_back(0);rejects_without_replacing(extra);
    Writer excessive;excessive.u64(42);excessive.u32(max_scene_lights+1);rejects_without_replacing(excessive.data);
    Writer invalid;invalid.u64(42);invalid.u32(1);invalid.f32(0);invalid.f32(64);invalid.f32(0);invalid.f32(0);invalid.u32(0xffffffff);
    rejects_without_replacing(invalid.data);
    UpdateLights();check(render::updates==1&&render::submitted==before);
}
}
int main(){
    full_provider_and_unchanged_snapshots();stale_disconnected_generation_and_world();clear_resets_all_state();
    provider_failure_remains_retryable();old_renderer_is_bounded_and_stops_renewing();optional_allocator_is_safe();
    invalid_frames_keep_the_accepted_snapshot();
    std::printf("Production plugin scene-light lifecycle: %u checks passed\n",checks);
}
