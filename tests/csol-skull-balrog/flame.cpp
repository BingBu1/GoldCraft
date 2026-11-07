// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production renderer TU with recording actual SDK boundaries.
#include "../../native/csol/client/weapons/balrog11_flame.cpp"
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>

namespace csol::client {
cl_enginefunc_t engine{};
ref_params_t view{};
metahook_api_t* api{};
void SynchronizeSession() {}
}
namespace {
namespace fx = csol::balrog11::flame;
using namespace csol::client;
unsigned checks{}, failures{}, forwarded{}, hookCalls{}, vertices{}, begins{}, ends{}, bindings{};
int mode{}, cull{}, randomCalls{};
double now{};
model_t model{};
cl_entity_t local{}, owner{};
triangleapi_t triangle{};
metahook_api_t meta{};
pfnUserMsgHook receiver{};
bool ownerVisible{}, textureAvailable{true};
std::function<void()> callback;
std::string trigger;
void Check(bool value, const char* what, int line) {
    ++checks; if (!value) { ++failures; std::cerr << "FAIL " << line << ": " << what << '\n'; }
}
#define CHECK(...) Check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
void Trigger(const char* where) {
    if (callback && trigger == where) { auto action = std::move(callback); callback = {}; action(); }
}
void Arm(const char* where) { trigger = where; callback = [] { ResetBalrog11Flame(); }; }
unsigned LiveParticles() { return std::count_if(particles.begin(), particles.end(), [](const auto& p) { return p.active; }); }
unsigned LiveEmitters() { return std::count_if(emitters.begin(), emitters.end(), [](const auto& p) { return p.active; }); }
void Setup(float time = 100) {
    ResetBalrog11Flame(); now = time; callback = {}; trigger.clear();
    ownerVisible = textureAvailable = true; vertices = begins = ends = bindings = randomCalls = 0;
    model = {}; std::strcpy(model.name, fx::Sprite); model.type = mod_sprite; model.numframes = 7;
    owner = {}; owner.model = &model; owner.index = 201; owner.curstate.modelindex = 22; owner.curstate.messagenum = 4;
    owner.origin[0] = 10; owner.origin[1] = 20; owner.origin[2] = 30;
    local = {}; local.curstate.messagenum = 4;
    engine.GetClientTime = [] { Trigger("time"); return static_cast<float>(now); };
    engine.GetLocalPlayer = [] { Trigger("local"); return &local; };
    engine.GetEntityByIndex = [](int index) -> cl_entity_t* { CHECK(index == 201); Trigger("owner"); return ownerVisible ? &owner : nullptr; };
    engine.CL_LoadModel = [](const char* path, int*) -> model_t* { CHECK(!std::strcmp(path, fx::Sprite)); Trigger("load"); return &model; };
    engine.pfnRandomFloat = [](float low, float high) { ++randomCalls; Trigger("random"); return (low + high) * .5f; };
    engine.pfnAngleVectors = [](const float*, float* f, float* r, float* u) {
        Trigger("angles"); f[0]=1; f[1]=f[2]=0; r[1]=1; r[0]=r[2]=0; u[2]=1; u[0]=u[1]=0;
    };
    triangle.SpriteTexture = [](model_t* m, int frame) { CHECK(m == &model && frame >= 0 && frame < 7); ++bindings; Trigger("texture"); return int(textureAvailable); };
    triangle.RenderMode = [](int value) { mode=value; Trigger("mode"); };
    triangle.CullFace = [](TRICULLSTYLE value) { cull=value; Trigger("cull"); };
    triangle.Begin = [](int type) { CHECK(type == TRI_QUADS && mode == kRenderTransAdd && cull == TRI_NONE); ++begins; Trigger("begin"); };
    triangle.End = [] { ++ends; Trigger("end"); };
    triangle.Color4f = [](float r,float g,float b,float a) { CHECK(r==1 && g==1 && b==1 && a>=0 && a<=1); Trigger("color"); };
    triangle.TexCoord2f = [](float, float) { Trigger("uv"); };
    triangle.Vertex3f = [](float x,float y,float z) { CHECK(std::isfinite(x) && std::isfinite(y) && std::isfinite(z)); ++vertices; Trigger("vertex"); };
    engine.pTriAPI=&triangle;
}
void Send(int rotation = 90) {
    auto bytes=fx::Write({201,rotation}); CHECK(bytes.has_value());
    CHECK(receiver("TempEmit", int(bytes->size()), bytes->data()) == 1);
}
void Emit() { Send(); now += .02; Balrog11FlameFrame(); CHECK(LiveParticles()==1 && LiveEmitters()==1); }
void Wire(const char* path) {
    std::ifstream file(path,std::ios::binary); std::vector<std::uint8_t> original((std::istreambuf_iterator<char>(file)),{});
    const auto expected=fx::Write({201,90}); CHECK(expected && original.size()==fx::MessageBytes);
    CHECK(expected && std::equal(original.begin(),original.end(),expected->begin(),expected->end()));
    CHECK(fx::Read(original) && fx::Read(original)->owner==201 && fx::Read(original)->rotation==90);
    for (std::size_t n=0;n<original.size();++n) CHECK(!fx::Read(std::span(original).first(n)));
    for (std::size_t n=0;n<original.size();++n) {
        if (n==sizeof(fx::Sprite) || n==sizeof(fx::Sprite)+1 || n==sizeof(fx::Sprite)+28) continue;
        auto bad=original; bad[n]^=1; CHECK(!fx::Read(bad));
    }
    for (int ownerId : {0,-1,32768}) CHECK(!fx::Write({ownerId,90}));
    for (int rotation : {0,-1,181,256}) CHECK(!fx::Write({201,rotation}));
    for (int id : {1,201,32767}) for (int roll : {1,90,180}) CHECK(fx::Read(*fx::Write({id,roll})).has_value());
}
void Lifecycle() {
    meta.HookUserMsg=[](const char* name,pfnUserMsgHook fn)->pfnUserMsgHook {
        CHECK(!std::strcmp(name,"TempEmit")); ++hookCalls; receiver=fn;
        return [](const char*,int,void*) { ++forwarded; return 7; };
    }; api=&meta; InitializeBalrog11Flame(); InitializeBalrog11Flame(); CHECK(hookCalls==1);
    CHECK(receiver("TempEmit",0,nullptr)==7 && forwarded==1);
    Setup(); Emit(); CHECK(randomCalls==4); DrawBalrog11Flame();
    CHECK(vertices==4 && begins==1 && ends==1 && bindings==1 && mode==kRenderNormal && cull==TRI_FRONT);
    Balrog11FlameFrame(); CHECK(LiveParticles()==1); // One emission per timestamp.
    now+=.001; Balrog11FlameFrame(); CHECK(LiveParticles()==1);
    now+=.1; Balrog11FlameFrame(); CHECK(LiveParticles()==2); // No missed-frame catch-up.
    ownerVisible=false; now+=.02; Balrog11FlameFrame(); CHECK(LiveEmitters()==0 && LiveParticles()==2);
    now+=.31; Balrog11FlameFrame(); CHECK(LiveParticles()==0);
    Setup(0); Emit(); CHECK(LiveParticles()==1); // Time zero is a valid map clock.
    Setup(); ownerVisible=false; Send(); now+=.2; Balrog11FlameFrame(); CHECK(LiveParticles()==0 && LiveEmitters()==1);
    ownerVisible=true; now+=.02; Balrog11FlameFrame(); CHECK(LiveParticles()==1);
    Setup(); ownerVisible=false; Send(); now+=1.01; Balrog11FlameFrame(); CHECK(LiveEmitters()==0);
    Setup(); Emit(); owner.curstate.messagenum=3; now+=.02; Balrog11FlameFrame(); CHECK(LiveEmitters()==0);
    Setup(); Emit(); owner.curstate.modelindex=23; now+=.02; Balrog11FlameFrame(); CHECK(LiveEmitters()==0);
    // Live B regression: a TempEmit parsed at packet time behind the last
    // frame sample must not clear the trails already in flight.
    Setup(); Emit(); now-=.01; Send(); CHECK(LiveParticles()==1 && LiveEmitters()==2);
    now+=.03; Balrog11FlameFrame(); CHECK(LiveParticles()==3 && LiveEmitters()==2);
    // A backward frame correction is skipped, not treated as a new session.
    Setup(); Emit(); now-=.01; Balrog11FlameFrame(); CHECK(LiveParticles()==1 && LiveEmitters()==1);
    now+=.03; Balrog11FlameFrame(); CHECK(LiveParticles()==2 && LiveEmitters()==1);
    Setup(); Emit(); now=std::numeric_limits<double>::quiet_NaN(); Balrog11FlameFrame(); CHECK(LiveParticles()==0);
    Setup(); for(int i=0;i<129;++i) Send(); CHECK(LiveEmitters()==128); ResetBalrog11Flame(); CHECK(LiveEmitters()==0);
    for (const char* point : {"time","load","local","owner","random"}) {
        Setup(); Send(); now+=.02; Arm(point); Balrog11FlameFrame(); CHECK(LiveParticles()==0 && LiveEmitters()==0);
    }
    for (const char* point : {"texture","mode","cull","begin","angles","color","uv","vertex","end"}) {
        Setup(); Emit(); Arm(point); DrawBalrog11Flame();
        CHECK(LiveParticles()==0 && LiveEmitters()==0 && begins==ends && mode==kRenderNormal && cull==TRI_FRONT);
    }
    Setup(); Emit(); textureAvailable=false; DrawBalrog11Flame(); CHECK(vertices==0 && begins==0 && ends==0);
}
void ClockCorrections() {
    // Exact binary fractions expose integrating a corrected interval twice.
    // Keep just one particle, with nonzero velocity, after its owner disappears.
    Setup(100); engine.pfnRandomFloat=[](float,float high) { return high; }; Send();
    now=100.125; Balrog11FlameFrame(); ownerVisible=false;
    now=100.1875; Balrog11FlameFrame(); const auto before=particles[0].particle;
    now=100.15625; Balrog11FlameFrame();
    CHECK(particles[0].particle.origin==before.origin);
    CHECK(particles[0].particle.alpha==before.alpha && particles[0].particle.frame==before.frame);
    now=100.1875; Balrog11FlameFrame();
    CHECK(particles[0].particle.origin==before.origin && particles[0].particle.alpha==before.alpha);
    now=100.203125; Balrog11FlameFrame();
    CHECK(particles[0].particle.origin[0]-before.origin[0]==.25f);
    CHECK(particles[0].particle.alpha<=before.alpha);

    // A correction can predate this particle's birth. Neither update nor draw
    // may submit a negative sprite frame or make the particle younger.
    Setup(100); Send(); now=100.25; Balrog11FlameFrame(); ownerVisible=false;
    const auto born=particles[0].particle;
    for (double corrected : {100.0,100.125,100.25}) {
        now=corrected; Balrog11FlameFrame();
        CHECK(LiveParticles()==1 && particles[0].particle.origin==born.origin);
        CHECK(particles[0].particle.frame==born.frame && particles[0].particle.alpha==born.alpha);
        DrawBalrog11Flame(); // Recording SDK SpriteTexture checks the frame range.
    }
    now=100.265625; Balrog11FlameFrame();
    CHECK(LiveParticles()==1 && particles[0].particle.frame>=0 && particles[0].particle.frame<model.numframes);
    // A real session reset releases the old high-water mark.
    Setup(1); Emit(); CHECK(LiveParticles()==1);
}
void Oracle() {
    float size,growth,nowValue,before; int rotation,frames; fx::Particle p;
    while(std::cin>>p.born>>p.die>>size>>growth>>p.alpha>>p.origin[0]>>p.origin[1]>>p.origin[2]
        >>p.velocity[0]>>p.velocity[1]>>p.velocity[2]>>nowValue>>before>>rotation>>frames) {
        p.roll=1; p.rotation=rotation*.1f; const bool alive=fx::Tick(p,nowValue,before,frames);
        std::cout<<std::setprecision(9)<<'['<<int(alive)<<','<<size<<','<<p.alpha<<','<<p.roll<<','<<p.die;
        for(float x:p.origin)std::cout<<','<<x; for(float x:p.velocity)std::cout<<','<<x;
        std::cout<<','<<p.frame<<"]\n";
    }
}
}
int main(int argc,char**argv) {
    if(argc==2 && !std::strcmp(argv[1],"--oracle")) { Oracle(); return 0; }
    if(argc!=2)return 2; Wire(argv[1]); Lifecycle(); ClockCorrections();
    std::cout<<"Balrog11 flame "<<checks<<" checks, "<<failures<<" failures\n"; return failures ? 1 : 0;
}
