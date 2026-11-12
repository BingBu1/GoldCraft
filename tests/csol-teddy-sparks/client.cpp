// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/particle_effects.cpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <limits>

namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
void SynchronizeSession() {}
}

namespace test
{
using namespace csol::client;
namespace spark = csol::teddy_sparks;
namespace particles = csol::particles;
unsigned checks{}, timeCalls{}, loadCalls{}, modelCalls{}, rngCalls{}, chainCalls{}, hookCalls{}, begins{}, ends{}, vertices{}, pvsCalls{}, localCalls{};
unsigned rngAxis{};
float clockNow{10};
std::string resetAt;
bool reenterRandom{}, resetRandom{}, noModel{}, inPvs{true};
model_t models[7]{};
int expectedOwner{44},meteorChoice{};
unsigned longRngCalls{};
std::vector<float> selectedRandom;std::size_t selectedRandomAt{};
int currentParseCount{77};
cl_entity_t emitterOwner{};
bool ownerNull{};
unsigned ownerCalls{};
event_api_t events{};
unsigned hullCalls{}, traceCalls{}, ringRngCalls{}, viewAngleCalls{}, symbolCalls{};
int observerState[3]{};
float renderCamera[3]{}, chaseCamera[3]{};
float traceFraction{1};
std::array<float,3> traceNormal{0,0,1};
cl_entity_t local{};
movevars_t movement{};
triangleapi_t triangles{};
metahook_api_t hookApi{};
std::vector<std::array<float, 2>> randomBounds, uv;
std::vector<std::array<float, 4>> colors;
std::vector<std::array<int, 2>> binds;
std::vector<std::array<float, 3>> positions, angleInputs;
std::vector<int> modes, culls;
std::vector<float> samples;
csol::particles::Matrix projectionMatrix{}, modelMatrix{};
std::vector<int> matrixQueries;
std::size_t sampleAt{};
std::vector<std::uint8_t> reentryPacket;

void Check(bool good, const char* why)
{
    ++checks;
    if (!good) throw std::runtime_error(why);
}
void ResetCallback(const char* stage)
{
    if (resetAt == stage) { resetAt.clear(); ResetParticleEffects(); }
}
int Previous(const char*, int, void*) { ++chainCalls; return 73; }
std::vector<std::uint8_t> Packet(unsigned sprite = 0)
{
    std::vector<std::uint8_t> b;
    const auto text = spark::Sprites[sprite];
    b.insert(b.end(), text.begin(), text.end()); b.push_back(0);
    const auto word = [&b](unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); };
    const auto real = [&word](float v) { const auto n = std::bit_cast<std::uint32_t>(v); word(n & 65535); word(n >> 16); };
    word(0); for (unsigned n : {80u, 160u, 240u, 0u, 0u, 1600u}) word(n);
    word(3); real(.1f); real(1.8f); word(0x50); word(0);
    for (unsigned n : {1u + sprite * 4, 16u + sprite, 5u, 240u, 250u, 123u, 70u}) b.push_back(n);
    for (float n : {30.f, 180.f, .1f, .1f, 2.f, 0.f, 0.f}) real(n);
    Check(b.size() == 82, "fixture packet length");
    return b;
}
int Send(std::vector<std::uint8_t>& p) { return Receive(spark::Message, static_cast<int>(p.size()), p.data()); }
unsigned Pending()
{
    unsigned count{};
    for (const auto& r : emitters.Records()) count += r.remaining > 0 || r.total == -1;
    return count;
}
void ClearDraw()
{
    begins = ends = vertices = 0;
    uv.clear(); colors.clear(); binds.clear(); positions.clear(); modes.clear(); culls.clear(); angleInputs.clear();
}
void Fresh()
{
    ResetParticleEffects(); emitters.Initialize();
    resetAt.clear(); reenterRandom = resetRandom = noModel = false; inPvs = true;
    models[0] = {}; models[1] = {}; models[2] = {}; models[3] = {}; models[4]={};models[5]={};models[6]={};local = {};emitterOwner={};
    for (auto& model : models) model.type = mod_sprite;
    models[0].numframes = 29; models[1].numframes = 17; models[2].numframes = 12; models[3].numframes=15;models[4].numframes=models[5].numframes=models[6].numframes=1;
    movement = {}; movement.gravity = 800;
    view = {}; view.movevars = &movement;
    view.viewangles[0] = 27; view.viewangles[1] = -49;
    engine = {}; triangles = {}; hookApi = {}; api = &hookApi; previous = previousSmoke = Previous;
    samples.clear(); sampleAt = 0;
    engine.GetClientTime = []()
    {
        ++timeCalls; ResetCallback("time");
        return sampleAt < samples.size() ? samples[sampleAt++] : clockNow;
    };
    engine.pfnSPR_Load = [](const char* path)
    {
        ++loadCalls; ResetCallback("load");
        const unsigned n = std::string_view(path) == meteor::Sprites[0] ? 4 : std::string_view(path) == meteor::Sprites[1] ? 5 : std::string_view(path) == meteor::Sprites[2] ? 6 : std::string_view(path) == sliding::Sprite ? 3 : std::string_view(path) == smoke::Sprite ? 2 : std::string_view(path) == spark::Sprites[1] ? 1 : 0;
        return int(n + 2);
    };
    engine.GetSpritePointer = [](int handle) -> const model_t*
    {
        ++modelCalls; ResetCallback("model");
        return noModel ? nullptr : &models[handle>=2 && handle<=8 ? handle-2 : 0];
    };
    engine.pfnRandomFloat = [](float lo, float hi)
    {
        if(selectedRandomAt<selectedRandom.size()){const float value=selectedRandom[selectedRandomAt++];++rngCalls;Check(lo<=value && value<=hi,"explicit Meteor RNG bounds");return value;}
        if ((lo == -32 && hi == 32) || (lo == 50 && hi == 70) || (lo == 2 && hi == 5))
        { ++rngCalls; ++ringRngCalls; if (resetRandom) { resetRandom=false; ResetParticleEffects(); } return lo == -32 ? 0.f : lo == 50 ? 60.f : 3.5f; }
        static constexpr float xyz[]{-1, .25f, 1};
        const float value = lo == 1 ? 1.25f : xyz[rngAxis++ % 3]; ++rngCalls;
        randomBounds.push_back({lo, hi});
        if (reenterRandom) { reenterRandom = false; Send(reentryPacket); UpdateParticleEmitters(); DrawParticleEffects(); }
        if (resetRandom) { resetRandom = false; ResetParticleEffects(); }
        Check(lo <= value && value <= hi, "RNG boundary/order");
        return value;
    };
    engine.pfnRandomLong=[](int low,int high){++longRngCalls;Check(low==0 && high==2 && meteorChoice>=0 && meteorChoice<=2,"original Meteor integer RNG");ResetCallback("integer");return meteorChoice;};
    selectedRandom.clear();selectedRandomAt=0;longRngCalls=0;meteorChoice=0;expectedOwner=44;
    engine.GetViewAngles=[](float* out){ ++viewAngleCalls; for(unsigned i=0;i<3;++i)out[i]=view.viewangles[i];ResetCallback("camera"); };
    observerState[0]=observerState[1]=observerState[2]=0;
    for(unsigned i=0;i<3;++i){renderCamera[i]=float(i+40);chaseCamera[i]=float(i+60);}
    cameraFields={observerState,renderCamera,chaseCamera};
    client_data_s initialData{};for(unsigned i=0;i<3;++i)initialData.viewangles[i]=view.viewangles[i];CacheParticleClientData(initialData);
    viewAngleCalls=symbolCalls=0;
    currentParseCount=77;parseCount=&currentParseCount;ownerNull=false;ownerCalls=0;
    emitterOwner.index=44;emitterOwner.curstate.messagenum=77;emitterOwner.origin[0]=100;emitterOwner.origin[1]=200;emitterOwner.origin[2]=300;
    const float hand[2][3]{{110,220,330},{440,550,660}};for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)emitterOwner.attachment[i][j]=hand[i][j];
    engine.GetEntityByIndex=[](int index)->cl_entity_t*{ ++ownerCalls;Check(index==expectedOwner,"actual owner index getter");ResetCallback("owner");return ownerNull?nullptr:&emitterOwner;};
    hookApi.GetEngineBase=[]()->void* { return reinterpret_cast<void*>(0x20000000); };
    hookApi.GetClientBase=[]()->void* { return reinterpret_cast<void*>(0x10000000); };
    hookApi.ResolveGameSymbol=[](void* base,const char* name,mh_gamesymbol_kind_t kind,void** out)
    { Check(base==reinterpret_cast<void*>(std::string_view(name)=="cl_parsecount"?0x20000000:0x10000000) && kind==MH_GAMESYMBOL_KIND_GLOBAL,"matched camera/owner symbol API");++symbolCalls;
      const auto key=std::string_view(name);*out=key=="cl_parsecount"?static_cast<void*>(&currentParseCount):key=="GoldCraftParticle_Observer"?static_cast<void*>(observerState):key=="GoldCraftParticle_RenderAngles"?static_cast<void*>(renderCamera):key=="GoldCraftParticle_ChaseAngles"?static_cast<void*>(chaseCamera):nullptr;
      return *out?MH_GAMESYMBOL_OK:MH_GAMESYMBOL_SYMBOL_NOT_FOUND; };
    engine.GetLocalPlayer = []() -> cl_entity_t* { ++localCalls; ResetCallback("local"); return &local; };
    engine.pfnAngleVectors = [](const float* angles, float* forward, float* right, float* up)
    {
        angleInputs.push_back({angles[0], angles[1], angles[2]});
        for (unsigned i = 0; i < 3; ++i) { forward[i] = i == 2; right[i] = i == 0; up[i] = i == 1; }
        ResetCallback("angles");
    };
    projectionMatrix={};modelMatrix={};for(unsigned i=0;i<16;i+=5){projectionMatrix[i]=i==15?1.f:.0001f;modelMatrix[i]=1;}
    matrixQueries.clear();
    triangles.GetMatrix=[](const int name,float* out){ Check(name==0xba7 || name==0xba6,"SDK original matrix query enum");matrixQueries.push_back(name);
      const auto& m=name==0xba7?projectionMatrix:modelMatrix;for(unsigned i=0;i<16;++i)out[i]=m[i];ResetCallback(name==0xba7?"projection":"modelMatrix"); };
    triangles.RenderMode = [](int mode) { modes.push_back(mode); ResetCallback("mode"); };
    triangles.CullFace = [](TRICULLSTYLE style) { culls.push_back(style); ResetCallback("cull"); };
    triangles.SpriteTexture = [](model_t* model, int frame) { binds.push_back({int(model-models), frame}); ResetCallback("sprite"); return 1; };
    triangles.Begin = [](int primitive) { Check(primitive == TRI_QUADS, "quad primitive"); ++begins; ResetCallback("begin"); };
    triangles.End = []() { ++ends; ResetCallback("end"); };
    triangles.Color4f = [](float r, float g, float b, float a) { colors.push_back({r,g,b,a}); ResetCallback("color"); };
    triangles.TexCoord2f = [](float u, float v) { uv.push_back({u,v}); ResetCallback("tex"); };
    triangles.Vertex3f = [](float x, float y, float z) { ++vertices; positions.push_back({x,y,z}); ResetCallback("vertex"); };
    triangles.BoxInPVS = [](float*, float*) { ++pvsCalls; ResetCallback("pvs"); return int(inPvs); };
    engine.pTriAPI = &triangles;
    events={}; hullCalls=traceCalls=ringRngCalls=0; traceFraction=1; traceNormal={0,0,1};
    events.EV_SetTraceHull=[](int hull) { Check(hull==2,"SDK trace hull2"); ++hullCalls; ResetCallback("hull"); };
    events.EV_PlayerTrace=[](float* a,float* b,int flags,int ignore,pmtrace_t* out)
    { Check(flags==10 && ignore==-1,"SDK trace flags10 ignore-1"); ++traceCalls; *out={}; out->fraction=traceFraction; out->ent=0;
      for(unsigned i=0;i<3;++i){out->endpos[i]=a[i]+(b[i]-a[i])*traceFraction;out->plane.normal[i]=traceNormal[i];} ResetCallback("trace"); };
    engine.pEventAPI=&events;
    Check(preciseMath.Initialize(),"actual original precise-math exports");
    hookApi.HookUserMsg = [](const char* name, pfnUserMsgHook fn) { ++hookCalls; Check((std::string_view(name) == "TempEmit" && fn == Receive) || (std::string_view(name) == "MPToCL" && fn == ReceiveSmoke), "actual hook installation"); return Previous; };
    message_chain::installed.clear();
    // Empty transparent passes establish the independent prior-time clock.
    // Clear/VidInit intentionally do not reset that original clock.
    clockNow = 9; DrawParticleEffects(); clockNow = 10; DrawParticleEffects();
    received = emitted = invalid = exhausted = draws = smokeMessages = smokeEmitted = slidingEmitted = ownerMismatches = meteorEmitted = emitterStops = 0;
    timeCalls = loadCalls = modelCalls = rngCalls = chainCalls = hookCalls = pvsCalls = localCalls = 0;
    rngAxis = 0; randomBounds.clear(); ClearDraw(); matrixQueries.clear();CacheParticleHudTime(10);
}
void Frame(float now) { clockNow = now; UpdateParticleEmitters(); }
Slot* Seed(float x, unsigned sprite = 0, float die = 20)
{
    auto* s = particlePool.Allocate(true);
    Check(s != nullptr, "fixture allocation");
    *s = {}; s->particle = spark::Particle{}; s->angles={27,-49,1};
    s->model = &models[sprite]; s->nextPvsCheck = 100; s->inPvs = true;
    std::get<spark::Particle>(s->particle).sprite = static_cast<std::uint8_t>(sprite); std::get<spark::Particle>(s->particle).frame = 2;
    std::get<spark::Particle>(s->particle).color = {250,123,70}; std::get<spark::Particle>(s->particle).alpha = 240;
    std::get<spark::Particle>(s->particle).origin = {x,20,30}; std::get<spark::Particle>(s->particle).size = 180;
    std::get<spark::Particle>(s->particle).born = 9; std::get<spark::Particle>(s->particle).die = die;
    return s;
}
void DrawSeed()
{
    Fresh(); Seed(10); Seed(30,1); Seed(20);
}
void Cases()
{
    Fresh(); auto p0 = Packet(), p1 = Packet(1);
    InitializeParticleEffects(); InitializeParticleEffects();
    Check(hookCalls == 2 && previous == Previous && previousSmoke == Previous, "message chain once");
    std::vector<std::uint8_t> other{1,2,3};
    Check(Send(other) == 73 && chainCalls == 1, "non-owned message preserved");
    auto bad = p0; bad.pop_back(); Check(Send(bad) == 1 && invalid == 1 && !Pending() && !loadCalls, "owned malformed rejected");
    for (unsigned at : {19u,33u,39u,43u,47u,48u,49u,50u,54u,58u,62u,66u,70u,74u,78u})
    { bad = p0; bad[at] ^= 0x40; Check(Send(bad) == 1 && !Pending(), "malformed field rejected"); }
    bad = p0; bad[51] = 0; Check(Send(bad) == 1 && !Pending(), "out-of-range RGB rejected");
    Check(Send(p0) == 1 && Send(p1) == 1 && Pending() == 2 && loadCalls == 2 && modelCalls == 2, "dual packet actual Receive");
    Check(!timeCalls && !rngCalls && !particlePool.ActiveCount(), "message uses HUD cache, not live time or simulation");
    Frame(10); Check(!emitted && !rngCalls, "strict initial equal");
    Frame(10.5f); Check(emitted == 2 && rngCalls == 8 && emitters.Records()[0].remaining == 2, "one emission per due emitter");
    Check(particlePool.ActiveCount() == 2 && particlePool.PreviousTime() == 10 && !pvsCalls, "UpdateClientData does not advance particles");
    const auto strict = emitters.Records()[0].next; Frame(strict); Check(emitted == 2, "strict next equal");
    Frame(100); Check(emitted == 4 && emitters.Records()[0].remaining == 1, "no catchup");
    Frame(101); Check(emitted == 6 && !Pending(), "three emissions total");
    Check(loadCalls == 2 && modelCalls == 2, "no frame resource IO");

    Fresh(); clockNow = 200; Send(p0);
    Check(emitters.Records()[0].next == 10 && !timeCalls, "Add time independent from live engine clock");
    Fresh(); engine.GetSpritePointer = [](int) -> const model_t* { ++modelCalls; CacheParticleHudTime(30); return &models[0]; };
    Send(p0); Check(emitters.Records()[0].next == 30, "Add reads HUD cache after model callback");
    Fresh(); CacheParticleHudTime(0); Send(p0); Frame(1);
    Check(!emitted && emitters.Records()[0].remaining == 3, "zero-next disabled");
    Fresh(); noModel = true; Send(p0); Frame(10.5f);
    Check(received == 1 && emitters.Records()[0].remaining == 2 && invalid == 1 && !rngCalls, "missing model retains original consumed emitter");
    Fresh(); for (unsigned i = 0; i < 128; ++i) Send(p0);
    Check(Pending() == 128 && received == 128 && loadCalls == 128, "128 shared records load through engine cache");
    Send(p1); Check(exhausted == 1 && loadCalls == 128 && modelCalls == 128, "full pool does not load");
    ResetParticleEffects(); Check(!Pending() && !particlePool.ActiveCount(), "host reset clears particle then emitter records");
    Check(particlePool.PreviousTime() == 10, "clear preserves previous clock");

    Fresh(); Send(p0); Send(p1);
    for (unsigned i = 0; i < 1200; ++i) Seed(float(i));
    Frame(10.5f);
    Check(exhausted == 1 && emitters.Records()[0].remaining == 2 && emitters.Records()[1].remaining == 3 && !rngCalls, "factory full consumes and stops pass");
    Fresh(); DrawParticleEffects(); Check(!rngCalls && !loadCalls && !pvsCalls && timeCalls == 2, "empty transparent pass advances independent clock");
    Send(p0); Frame(10.01f);
    float bornX{};
    auto readOrigin = [&](const Slot& s) { bornX = std::get<spark::Particle>(s.particle).origin[0]; };
    particlePool.VisitActive(readOrigin);
    Check(bornX == 10, "emission has not integrated before transparent phase");
    DrawParticleEffects(); particlePool.VisitActive(readOrigin);
    Check(bornX == 10 + -30.f * (clockNow - 10), "transparent phase integrates original delta");
    const auto oldDraws = draws; const auto oldRng = rngCalls; DrawParticleEffects(); particlePool.VisitActive(readOrigin);
    Check(draws == oldDraws + 1 && rngCalls == oldRng && bornX == 10 + -30.f * (clockNow - 10), "same-clock redraw without update or emission");

    Fresh(); Send(p0); resetRandom = true; Frame(10.5f);
    Check(!Pending() && !particlePool.ActiveCount() && rngCalls == 1 && !emitted, "RNG reset invalidates allocation");
    Fresh(); Send(p0); reentryPacket = p1; reenterRandom = true; Frame(10.5f);
    Check(invalid == 1 && received == 1 && emitted == 1 && rngCalls == 4, "RNG reentry rejected");
    for (const char* stage : {"load","model"})
    { Fresh(); resetAt = stage; Send(p0); Check(!received && !Pending() && !particlePool.ActiveCount(), "resource callback invalidates Add"); }

    DrawSeed(); DrawParticleEffects();
    Check(begins == 3 && ends == 3 && vertices == 12 && binds.size() == 3, "all sorted particles drawn once");
    Check(binds[0][0] == 1 && binds[1][0] == 0 && binds[2][0] == 0 &&
        positions[0][0] == -60 && positions[4][0] == -70 && positions[8][0] == -80, "distance order preserved across sprite groups");
    Check(colors[0] == std::array<float,4>{250.f/255,123.f/255,70.f/255,240.f/255}, "additive RGB alpha");
    Check(modes.front() == kRenderTransAdd && modes.back() == kRenderNormal && culls.back() == TRI_FRONT, "draw API state restore");
    Check(uv[0] == std::array<float,2>{0,0} && uv[1] == std::array<float,2>{0,1} && uv[2] == std::array<float,2>{1,1} && uv[3] == std::array<float,2>{1,0}, "quad UV order");
    view.viewangles[0] = 45; DrawParticleEffects();
    Check(angleInputs.back()[0] == 27, "equal time keeps original camera cache");
    Check(!loadCalls && !rngCalls, "same-clock draw has no resource IO or RNG");
    for (const char* stage : {"time","local","angles","sprite","mode","cull","begin","color","tex","vertex","end"})
    {
        DrawSeed(); resetAt = stage; DrawParticleEffects();
        Check(begins == ends && !particlePool.ActiveCount() && !Pending(), "callback reset balances draws and invalidates traversal");
    }
    Fresh(); auto* s = Seed(10); s->nextPvsCheck = 0; resetAt = "pvs"; DrawParticleEffects();
    Check(!particlePool.ActiveCount() && !vertices, "PVS reset aborts particle traversal");
    Fresh(); s = Seed(10,0,10); DrawParticleEffects();
    Check(!particlePool.ActiveCount() && !vertices, "queued dying particle skips exact-die Draw");
    Fresh(); s = Seed(10,0,9); DrawParticleEffects();
    Check(!particlePool.ActiveCount() && vertices == 4, "queued already-expired particle preserves original equality-only Draw gate");
    Fresh(); s = Seed(10); std::get<spark::Particle>(s->particle).frame = models[0].numframes; DrawParticleEffects();
    Check(binds.size() == 1 && binds[0][1] == 29, "draw preserves frame==frames");

    Fresh(); clockNow = 9; DrawParticleEffects(); s = Seed(10); std::get<spark::Particle>(s->particle).velocity = {5,0,0};
    samples = {10,10,10,9,10}; sampleAt = 0; clockNow = 10; DrawParticleEffects();
    Check(std::get<spark::Particle>(s->particle).origin[0] == 10 && particlePool.PreviousTime() == 10, "per-particle equal sample skips update using original previous time");
    samples.clear(); sampleAt = 0; clockNow = 9.5f; DrawParticleEffects();
    Check(std::get<spark::Particle>(s->particle).origin[0] == 7.5f, "rewind still updates with initial snapshot");
    Fresh(); s = Seed(10); s->nextPvsCheck = 0; inPvs = false; clockNow = 10.1f; DrawParticleEffects();
    Check(!vertices && std::get<spark::Particle>(s->particle).size > 180, "invisible particle still updates");

    spark::Particle boundary; boundary.born = 10; boundary.die = 20;
    Check(spark::Tick(boundary, 11, 10, 800, 17, 17) && boundary.frame == 17, "strict finite animation equality");
    Check(!spark::Tick(boundary, std::nextafter(11.f, 12.f), 11, 800, 17, 17), "strict finite animation after bound");
}

std::vector<std::uint8_t> SmokePacket()
{
    const std::string raw =
        "58737072697465732f736d6f6b655f69612e737072000000c842c0b8323e0000204"
        "205000000ec51b83d0000803f32000000460000000ad7a33c0000003f0200000005"
        "0000000a000000010000003400000024000000000048429696962103bff96009";
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < raw.size(); i += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(raw.substr(i, 2), nullptr, 16)));
    Check(bytes.size() == smoke::MessageBytes, "smoke fixture length");
    return bytes;
}
int SendSmoke(std::vector<std::uint8_t>& packet)
{ return ReceiveSmoke("MPToCL", static_cast<int>(packet.size()), packet.data()); }
Slot* SmokeSeed(const std::array<float,3>& velocity)
{
    auto* slot = particlePool.Allocate(true);
    Check(slot != nullptr, "smoke fixture slot");
    smoke::Particle p;
    p.born = 10; p.die = 11; p.size = p.initialSize = 60; p.growth = 3.5f; p.angles = {0,0,1};
    p.velocity = velocity;
    *slot = {p, &models[2], {}, 100, true};
    return slot;
}
void SharedCases()
{
    Fresh();
    for (double value : {0., .17453289031982422, -.5, 1., 1.5707963267948966,
                         3.141592653589793, 6.283185307179586, -2., 1000000.})
    {
        // ABI smoke test against scalar CRT, not a cross-version CRT proof.
        const double cosine = preciseMath.cosine(value), sine = preciseMath.sine(value);
        Check(std::isfinite(cosine) && std::isfinite(sine) &&
            std::abs(cosine - std::cos(value)) < 2e-15 &&
            std::abs(sine - std::sin(value)) < 2e-15, "real precise exports use double XMM0 input/result");
    }
    auto packet = SmokePacket(), sparkPacket = Packet();
    auto bad = packet; bad.pop_back();
    Check(SendSmoke(bad) == 1 && invalid == 1 && !Pending() && !loadCalls, "owned bad smoke rejected");
    bad = packet; bad[1] = 'x';
    Check(SendSmoke(bad) == 73 && chainCalls == 1, "other subtype88 consumer retains chain");
    Check(Send(sparkPacket) == 1 && SendSmoke(packet) == 1 && Pending() == 37 &&
        loadCalls == 37 && modelCalls == 37 && ringRngCalls == 108 && !timeCalls, "spark and ring share one emitter pool");
    Check(std::holds_alternative<spark::Emitter>(emitters.Records()[0].payload) &&
        std::holds_alternative<smoke::Emitter>(emitters.Records()[1].payload) &&
        emitters.Records()[1].next == 10 && emitters.Records()[36].remaining == 5, "ring record order/cache/count");
    Frame(10.01f);
    Check(emitted == 37 && smokeEmitted == 36 && rngCalls == 108 + 37 * 4 &&
        particlePool.ActiveCount() == 37 && !traceCalls, "one shared allocation list and no premature trace");
    DrawParticleEffects();
    Check(traceCalls == 36 && hullCalls == 36 && vertices == 37 * 4 &&
        std::count(modes.begin(),modes.end(),kRenderTransAlpha) == 36 &&
        std::count(modes.begin(),modes.end(),kRenderTransAdd) == 1, "smoke trace and mode4 coexist with additive sparks");
    Check(loadCalls == 37 && modelCalls == 37 && ringRngCalls == 108, "draw does not load or rebuild rings");
    Check(matrixQueries==std::vector<int>{0xba7,0xba6},"two matrix reads per view, never per particle");
    for (float now : {10.2f,10.4f,10.6f,10.8f}) Frame(now);
    Check(smokeEmitted == 180 && emitted == 183 && !Pending(), "ring five emissions with spark three emissions");

    Fresh(); for(unsigned i=0;i<100;++i) Send(sparkPacket); SendSmoke(packet);
    Check(received == 128 && Pending() == 128 && exhausted == 8 && ringRngCalls == 108 &&
        loadCalls == 128 && std::holds_alternative<smoke::Emitter>(emitters.Records()[100].payload), "overflow shares128 and consumes all ring RNG");
    Fresh(); for(unsigned i=0;i<128;++i) Send(sparkPacket); SendSmoke(packet);
    Check(received == 128 && exhausted == 36 && ringRngCalls == 108 && loadCalls == 128, "full shared pool never loads discarded ring sprites");
    Fresh(); Send(sparkPacket); SendSmoke(packet);
    for(unsigned i=0;i<1199;++i) Seed(float(i));
    Frame(10.01f);
    Check(emitted == 1 && !smokeEmitted && particlePool.ActiveCount() == 1200 &&
        emitters.Records()[0].remaining == 2 && emitters.Records()[1].remaining == 4 &&
        emitters.Records()[2].remaining == 5 && rngCalls == 112, "shared factory failure stops at first smoke after spark consumes final slot");

    Fresh(); resetRandom = true; SendSmoke(packet);
    Check(!Pending() && !received && !smokeMessages && !loadCalls, "ring RNG reset aborts without future records");
    for(const char* stage : {"load","model"})
    {
        Fresh(); resetAt=stage; SendSmoke(packet);
        Check(!Pending() && !received && !smokeMessages && ringRngCalls == 3, "ring resource reset cancels remaining parser iterations");
    }
    Fresh(); auto* slot = SmokeSeed({50,0,30}); traceFraction=.5f; traceNormal={1,0,0};
    clockNow=10.1f; DrawParticleEffects();
    const auto& wall=std::get<smoke::Particle>(slot->particle);
    Check(traceCalls == 1 && wall.velocity[0] < 0 && wall.internalFlags == 0x20 &&
        wall.lastPosition == wall.origin && vertices == 4, "SDK wall trace drives real reflection");
    Fresh(); slot = SmokeSeed({50,0,-2}); traceFraction=.5f;
    clockNow=10.1f; DrawParticleEffects();
    const auto& floor=std::get<smoke::Particle>(slot->particle);
    Check(floor.internalFlags == 0 && floor.velocity == std::array<float,3>{} &&
        floor.rotationVelocity == std::array<float,3>{}, "SDK floor trace settles smoke");
    Fresh(); slot=SmokeSeed({}); traceFraction=.5f;
    clockNow=10.1f; DrawParticleEffects();
    Check(std::get<smoke::Particle>(slot->particle).internalFlags == 0, "SDK low speed smoke stops");
    for(const char* stage : {"hull","trace","camera"})
    {
        Fresh(); SmokeSeed({50,0,30}); resetAt=stage; clockNow=10.1f; DrawParticleEffects();
        Check(!particlePool.ActiveCount() && !Pending() && !vertices, "trace callback invalidation aborts update/draw");
    }
    Fresh(); slot=Seed(10);
    const auto originalAngles=slot->angles;
    view.viewangles[0]=70; DrawParticleEffects();
    Check(slot->angles == originalAngles && angleInputs.back()[0] == 27, "equal-clock Draw uses stored object angles");
    clockNow=10.1f; DrawParticleEffects();
    Check(slot->angles[0] == 70 && angleInputs.back()[0] == 70, "Update stores current camera pitch before Draw");
    Fresh(); slot=SmokeSeed({}); view.viewangles[0]=70; DrawParticleEffects();
    Check(angleInputs.back() == std::array<float,3>{0,0,1}, "new smoke Draw keeps constructor angles before first Update");
    smoke::Particle lifetime;
    lifetime.die=10;
    Check(smoke::PrepareDraw(lifetime,9,{},{}).has_value() &&
        !smoke::PrepareDraw(lifetime,10,{},{}).has_value() &&
        smoke::PrepareDraw(lifetime,11,{},{}).has_value(), "Draw equality gate is separate from manager retirement");
}




std::vector<std::uint8_t> SlidingPacket()
{
    std::vector<std::uint8_t> b(sliding::Sprite.begin(),sliding::Sprite.end());b.push_back(0);
    const auto number=[&](std::uint32_t v,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(std::uint8_t(v>>(8*i)));};
    const auto real=[&](float f){number(std::bit_cast<std::uint32_t>(f),4);};
    number(44,2);
    for(int n:{32,-40,48,56,-64,72})number(std::uint16_t(n),2);
    number(3,2);real(.1f);real(.5f);number(0x30,4);
    for(unsigned n:{0,30,5,255,255,255,255})number(n,1);
    for(float n:{0.f,50.f,0.f,.2f,.2f,0.f,0.f})real(n);
    Check(b.size()==82,"sliding actual packet length");return b;
}
void SlidingCases()
{
    Fresh();auto slide=SlidingPacket(),sparks=Packet(),smokePacket=SmokePacket();
    InitializeParticleEffects();Check(parseCount==&currentParseCount,"matched native parsecount resolved");
    Send(slide);Check(Pending()==1 && !ownerCalls && !rngCalls && std::get<sliding::Emitter>(emitters.Records()[0].payload).owner==44,
        "sliding parses owner without premature query");
    Frame(10);Check(!emitted && !ownerCalls,"owner not checked before strict emission time");
    Frame(10.01f);Check(slidingEmitted==1 && ownerCalls==1 && rngCalls==4,"zero spread still consumes all four RNG calls");
    spark::Particle first{};
    auto capture=[&](const Slot& s){if(const auto* p=std::get_if<SlidingParticle>(&s.particle))first=p->value;};
    particlePool.VisitActive(capture);
    Check(first.origin==std::array<float,3>{104,195,306} && first.velocity==std::array<float,3>{7,-8,9} &&
        first.alpha==250 && first.growth==.2f && first.size==50 && first.die==10.01f+.5f,"sliding original world offset/clamp/fields");
    emitterOwner.origin[0]=200;emitterOwner.origin[1]=400;emitterOwner.origin[2]=600;
    emitterOwner.angles[0]=90;emitterOwner.angles[1]=180;emitterOwner.angles[2]=270;
    Frame(10.2f);Check(slidingEmitted==2 && ownerCalls==2,"moving owner emits at each current sample");
    unsigned old{},moved{};auto scan=[&](const Slot& s)
    {if(const auto* p=std::get_if<SlidingParticle>(&s.particle)){old+=p->value.origin==std::array<float,3>{104,195,306};moved+=p->value.origin==std::array<float,3>{204,395,606};}};
    particlePool.VisitActive(scan);
    Check(old==1 && moved==1,"offset not rotated, emitted particles never reattach");
    currentParseCount=78;local.curstate.messagenum=77;Frame(10.4f);
    Check(slidingEmitted==2 && ownerMismatches==1 && !Pending() &&
        emitters.Records()[0].total==0 && emitters.Records()[0].remaining==0 &&
        std::get<sliding::Emitter>(emitters.Records()[0].payload).owner==44 &&
        emitters.Records()[0].next==10.4f+.1f && particlePool.ActiveCount()==2,"mismatch consumes schedule then clears counts only");
    Fresh();currentParseCount=77;local.curstate.messagenum=1000;Send(slide);Frame(10.01f);
    Check(slidingEmitted==1,"global parsecount is independent from local-player messagenum");
    Fresh();Send(slide);Send(sparks);ownerNull=true;Frame(10.01f);
    Check(invalid==1 && !emitted && emitters.Records()[0].remaining==2 && emitters.Records()[1].remaining==3,
        "null owner host safety stops pass without invented stamp or success");
    Fresh();Send(slide);resetAt="owner";Frame(10.01f);
    Check(!Pending() && !particlePool.ActiveCount() && !emitted,"owner callback reset invalidates emission");
    Fresh();Send(slide);samples={10.01f,10.02f,10.03f};sampleAt=0;Frame(10.01f);
    particlePool.VisitActive(capture);
    Check(first.born==10.03f && first.die==10.01f+.5f,"separate factory birth and emitter lifetime clocks");
    float pvsTime{};auto pvs=[&](const Slot& s){pvsTime=s.nextPvsCheck;};particlePool.VisitActive(pvs);
    Check(pvsTime==10.02f,"factory has separate PVS-time sample");
    Fresh();Send(slide);Frame(10.01f);clockNow=10.31f;DrawParticleEffects();
    particlePool.VisitActive(capture);
    Check(first.frame==9 && first.roll==1 && first.alpha<250 && !traceCalls && !hullCalls,
        "sliding uses loop30 animation and no collision trace");
    for(float now:{10.2f,10.4f})Frame(now);
    Check(slidingEmitted==3 && !Pending(),"sliding three strict attempts");
    Fresh();Send(sparks);Send(slide);SendSmoke(smokePacket);Frame(10.01f);
    Check(emitted==38 && slidingEmitted==1 && smokeEmitted==36 && particlePool.ActiveCount()==38,
        "three kinds share actual ordinary pool");
    Fresh();for(unsigned i=0;i<128;++i)Send(slide);Send(sparks);
    Check(Pending()==128 && received==128 && exhausted==1 && loadCalls==128,"sliding shares emitter capacity with sparks");
    Fresh();Send(slide);Send(sparks);for(unsigned i=0;i<1200;++i)Seed(float(i));Frame(10.01f);
    Check(exhausted==1 && !rngCalls && emitters.Records()[0].remaining==2 && emitters.Records()[1].remaining==3,
        "sliding failed factory consumes count and stops later producers");
}


void SlidingRuntimeOracle()
{
    std::string raw;int scenario;
    while(std::cin>>raw>>scenario)
    {
        Fresh();std::vector<std::uint8_t> bytes;
        for(std::size_t at=0;at<raw.size();at+=2)bytes.push_back(std::uint8_t(std::stoul(raw.substr(at,2),nullptr,16)));
        Send(bytes);
        Check(std::holds_alternative<sliding::Emitter>(emitters.Records()[0].payload),"sliding oracle receiver");
        if(scenario==4)for(unsigned i=0;i<1200;++i)Seed(float(i));
        const float times[]{10.f,10.01f,10.11f,10.2f,10.4f};
        std::cout << "{\"states\":[";
        for(unsigned step=0;step<5;++step)
        {
            if(step)std::cout << ',';
            if(scenario==1 && step>=2){emitterOwner.origin[0]=200;emitterOwner.origin[1]=400;emitterOwner.origin[2]=600;
                emitterOwner.angles[0]=90;emitterOwner.angles[1]=180;emitterOwner.angles[2]=270;}
            if((scenario==2 && step>=1)||(scenario==3 && step>=2))currentParseCount=78;
            Frame(times[step]);
            const auto& record=emitters.Records()[0];
            std::cout << '[' << record.total << ',' << record.remaining << ','
                << std::bit_cast<std::uint32_t>(record.next) << ',' << std::get<sliding::Emitter>(record.payload).owner << ']';
        }
        std::array<const Slot*,particles::OrdinaryCapacity> slots{};std::size_t count{};
        auto scan=[&](const Slot& slot){if(std::holds_alternative<SlidingParticle>(slot.particle))slots[count++]=&slot;};
        particlePool.VisitActive(scan);
        std::cout << "],\"particles\":[";
        for(std::size_t i=0;i<count;++i)
        {
            if(i)std::cout << ',';
            const auto& p=std::get<SlidingParticle>(slots[count-i-1]->particle).value;
            std::cout << '[';bool first=true;
            for(float v:{p.origin[0],p.origin[1],p.origin[2],p.velocity[0],p.velocity[1],p.velocity[2],
                         p.born,p.die,p.size,p.growth,p.alpha,p.roll})
            {if(!first)std::cout << ',';first=false;std::cout << std::bit_cast<std::uint32_t>(v);}
            std::cout << ']';
        }
        std::cout << "],\"rng\":" << rngCalls << "}\n";
    }
}


std::vector<std::uint8_t> HexPacket(std::string_view text)
{
    std::vector<std::uint8_t> bytes;
    for(std::size_t i=0;i<text.size();i+=2)bytes.push_back(std::uint8_t(std::stoul(std::string(text.substr(i,2)),nullptr,16)));
    return bytes;
}
std::vector<std::uint8_t> MeteorPacket(unsigned attachment=0)
{
    auto bytes=HexPacket(attachment?
        "737072697465732f6275726e352e737072000100000000000000000000000000ffff0ad7233ccdcc4c3e301c000001020001000f05faffffff000000420000b84100000000000000000000a0400000000000000000":
        "737072697465732f6275726e352e737072000100000000000000000000000000ffff0ad7233ccdcc4c3e301c000000020001000f05faffffff000040410000a0410000000000000000000000400000000000000000");
    bytes[meteor::Sprites[0].size()+1]=44;
    return bytes;
}
std::vector<std::uint8_t> StopPacket()
{
    return HexPacket("737072697465732f646f742e7370720000000000000000000000000000000000000000000000000000040000020000");
}
void SetMeteorRandom(unsigned emissions=1)
{
    selectedRandom.clear();selectedRandomAt=0;
    for(unsigned i=0;i<emissions;++i)
        for(float v:{1.25f,7.5f,-2.5f,3.5f,-.5f,.25f,1.f,1.125f})selectedRandom.push_back(v);
}
void MeteorCases()
{
    Fresh();auto left=MeteorPacket(),right=MeteorPacket(1),stop=StopPacket();
    Check(meteor::Read(left)->attachment==0 && meteor::Read(right)->attachment==1 &&
        meteor::ReadStop(stop),"original Meteor hand and short stop forms decode");
    for(std::size_t n=0;n<stop.size();++n)Check(!meteor::ReadStop(std::span(stop).first(n)),"truncated stop is never a complete command");
    for(std::size_t n=0;n<left.size();++n)Check(!meteor::Read(std::span(left).first(n)),"truncated hand emitter rejected");
    Send(left);Send(right);
    Check(Pending()==2 && loadCalls==5 && modelCalls==5 && !ownerCalls &&
        emitters.Records()[0].tag==2 && emitters.Records()[0].total==-1 &&
        emitters.Records()[0].remaining==-1,"two hands share unlimited group2 records and cold companions");
    const auto loads=loadCalls;SetMeteorRandom(2);meteorChoice=2;Frame(10.01f);
    Check(meteorEmitted==2 && rngCalls==16 && longRngCalls==2 && ownerCalls==2 &&
        loadCalls==loads && Pending()==2,"successful fireball consumes eight Float and one Long per hand");
    std::array<meteor::Particle,2> before{};unsigned count{};
    auto capture=[&](const Slot& s){if(const auto* p=std::get_if<meteor::Particle>(&s.particle))before[count++]=*p;};
    particlePool.VisitActive(capture);
    Check(count==2 && before[0].value.origin==std::array<float,3>{443.5f,547.5f,667.5f} &&
        before[1].value.origin==std::array<float,3>{113.5f,217.5f,337.5f} &&
        before[0].value.size==24.25f && before[1].value.size==21.25f &&
        before[0].mass==1.125f && before[1].value.velocity==std::array<float,3>{-6,3,12},
        "both actual hand attachments use original random model, offsets, size and spread");
    const auto firstNext=emitters.Records()[0].next;const auto* firstModel=emitters.Records()[0].model;
    Send(stop);
    Check(!Pending() && particlePool.ActiveCount()==2 && emitterStops==1 && loadCalls==loads &&
        emitters.Records()[0].tag==0 && emitters.Records()[0].active==0 &&
        emitters.Records()[0].next==firstNext && emitters.Records()[0].model==firstModel &&
        std::get<meteor::Emitter>(emitters.Records()[0].payload).owner==44,"stop clears four fields and retains live particles, next, model and owner");
    std::array<meteor::Particle,2> after{};count=0;
    auto afterCapture=[&](const Slot& s){if(const auto* p=std::get_if<meteor::Particle>(&s.particle))after[count++]=*p;};
    particlePool.VisitActive(afterCapture);
    for(unsigned i=0;i<2;++i)Check(before[i].value.origin==after[i].value.origin && before[i].value.velocity==after[i].value.velocity &&
        before[i].value.born==after[i].value.born && before[i].value.die==after[i].value.die &&
        before[i].value.size==after[i].value.size && before[i].value.alpha==after[i].value.alpha,"stop never modifies an issued particle");
    Frame(10.1f);Check(meteorEmitted==2 && !Pending(),"stopped hands do not resume on later frames");
    DrawParticleEffects();
    Check(vertices==8 && !traceCalls && !hullCalls && binds[0][0]==6 && binds[1][0]==6,
        "issued random burn sprites keep ordinary update/draw after stop");
    Fresh();Send(left);Send(right);SetMeteorRandom(2);Frame(10.01f);emitterOwner.attachment[0][0]=999;
    Send(stop);clockNow=10.1f;DrawParticleEffects();count=0;particlePool.VisitActive(afterCapture);
    Check(after[1].value.origin[0]!=999 && after[1].value.alpha<250 && after[1].value.alpha>0,
        "issued Meteor particles drift and fade without reattaching");

    Fresh();auto sparks=Packet(),slide=SlidingPacket();Send(sparks);Send(left);Send(slide);Send(stop);
    Check(Pending()==2 && emitters.Records()[0].remaining==3 && emitters.Records()[2].remaining==3,
        "group stop leaves unrelated world and owner records");
    Fresh();auto other=left;other[meteor::Sprites[0].size()+1]=55;Send(left);Send(other);
    Check(emitters.StopTagged(2,44,EmitterOwner)==particles::EmitterResult::Complete &&
        emitters.Records()[0].total==0 && emitters.Records()[1].total==-1 &&
        std::get<meteor::Emitter>(emitters.Records()[1].payload).owner==55,"positive owner filter preserves other owner in same group");
    Send(stop);Check(!Pending(),"owner0 stop covers all remaining owners in group2");
    Fresh();Send(left);SetMeteorRandom();selectedRandom.resize(4);meteorChoice=1;
    for(unsigned i=0;i<1200;++i)Seed(float(i));Frame(10.01f);
    Check(!meteorEmitted && exhausted==1 && rngCalls==4 && longRngCalls==1 &&
        emitters.Records()[0].next==10.01f+.01f && emitters.Records()[0].remaining==-1,
        "allocation failure preserves pre-RNG but never consumes post-factory XYZ/mass");
    Fresh();models[5].numframes=0;Send(left);SetMeteorRandom();selectedRandom.resize(4);meteorChoice=1;Frame(10.01f);
    Check(!meteorEmitted && invalid==1 && rngCalls==4 && longRngCalls==1 && !particlePool.ActiveCount(),
        "missing selected companion fails after original pre-RNG without substitute");
    for(const char* stage:{"load","model","owner","integer"})
    {
        Fresh();if(std::string_view(stage)=="load"||std::string_view(stage)=="model")resetAt=stage;
        Send(left);if(std::string_view(stage)=="owner"||std::string_view(stage)=="integer"){resetAt=stage;SetMeteorRandom();Frame(10.01f);}
        Check(std::all_of(emitters.Records().begin(),emitters.Records().end(),[](const Record& r)
            {return r.active!=1 && (r.next==0 || (r.total==0 && r.remaining==0));}) &&
            !particlePool.ActiveCount() && !meteorEmitted,"Meteor callback reset preserves original inactive bytes without a live schedule");
        Frame(20);Check(!meteorEmitted,"retired or zero-next Meteor record cannot emit later");
    }
    Fresh();Send(left);SetMeteorRandom();samples={10.01f,10.011f,10.012f};sampleAt=0;Frame(10.01f);count=0;particlePool.VisitActive(afterCapture);
    float pvs{};auto pvsCapture=[&](const Slot& s){pvs=s.nextPvsCheck;};particlePool.VisitActive(pvsCapture);
    Check(after[0].value.born==10.012f && after[0].value.die==10.01f+.2f && pvs==10.011f,
        "Meteor preserves all three original time samples");
    Fresh();Send(left);SetMeteorRandom();Frame(10.01f);Send(stop);clockNow=10.21f;DrawParticleEffects();
    Check(!particlePool.ActiveCount() && !vertices,"Meteor dies through ordinary pool after alpha/lifetime update");
}

void FrustumCases()
{
    Fresh(); Seed(10); DrawParticleEffects();
    Check(matrixQueries==std::vector<int>{0xba7,0xba6},"frustum prepared once per transparent view");
    for(const char* stage : {"projection","modelMatrix"})
    {
        Fresh(); Seed(10);resetAt=stage;DrawParticleEffects();
        Check(!particlePool.ActiveCount() && !vertices,"matrix callback invalidates stale view");
    }
    Fresh(); projectionMatrix=modelMatrix; auto* slot=Seed(40);std::get<spark::Particle>(slot->particle).size=5;
    std::get<spark::Particle>(slot->particle).origin={3,0,0};
    DrawParticleEffects();
    Check(!vertices && particlePool.ActiveCount()==1,"PVS true cannot override frustum outside");
    std::get<spark::Particle>(slot->particle).origin={0,0,0};DrawParticleEffects();
    Check(vertices==4,"inside fixed AABB remains visible");
    Fresh(); projectionMatrix=modelMatrix; slot=Seed(1);std::get<spark::Particle>(slot->particle).size=0;
    std::get<spark::Particle>(slot->particle).origin={1,0,0};DrawParticleEffects();
    Check(!vertices,"exact frustum plane is outside for zero extent");
}
float InputBits()
{
    std::uint32_t bits;
    Check(bool(std::cin>>bits),"missing float32 bits");
    return std::bit_cast<float>(bits);
}
void FrustumOracle()
{
    std::uint32_t first;
    while(std::cin>>first)
    {
        particles::Matrix projection,model;
        projection[0]=std::bit_cast<float>(first);
        for(unsigned i=1;i<16;++i)projection[i]=InputBits();
        for(float& value:model)value=InputBits();
        const auto planes=particles::GenerateFrustum(projection,model);
        std::cout << '[';
        for(unsigned i=0;i<24;++i)std::cout << (i?",":"") << std::bit_cast<std::uint32_t>(planes[i/4][i%4]);
        std::cout << "]\n";
    }
}
void VisibilityOracle()
{
    std::uint32_t first;
    while(std::cin>>first)
    {
        particles::Vec3 origin{std::bit_cast<float>(first),InputBits(),InputBits()};
        const float size=InputBits();
        particles::Frustum planes;
        for(auto& plane:planes)for(float& value:plane)value=InputBits();
        int pvs;Check(bool(std::cin>>pvs),"missing PVS");
        std::cout << (particles::FixedBoundsVisible(origin,size/5.f,planes,pvs!=0)?1:0) << '\n';
    }
}


void MeteorRuntimeOracle()
{
    std::string raw;int choice,fail;
    while(std::cin>>raw>>choice>>fail)
    {
        Fresh();auto bytes=HexPacket(raw);
        const auto event=meteor::Read(bytes);Check(bool(event),"actual Meteor raw packet");
        expectedOwner=event->owner;emitterOwner.index=expectedOwner;meteorChoice=choice;
        samples={InputBits(),InputBits(),InputBits()};sampleAt=0;
        for(unsigned i=0;i<8;++i)selectedRandom.push_back(InputBits());
        Send(bytes);
        if(fail)for(unsigned i=0;i<1200;++i)Seed(float(i));
        Frame(samples[0]);
        const auto& record=emitters.Records()[0];
        std::cout << "{\"record\":[" << record.tag << ',' << record.total << ',' << record.remaining << ','
            << record.active << ',' << std::bit_cast<std::uint32_t>(record.next) << "],\"particles\":[";
        bool firstParticle=true;
        auto print=[&](const Slot& slot)
        {
            if(const auto* p=std::get_if<meteor::Particle>(&slot.particle))
            {
                if(!firstParticle)std::cout << ',';firstParticle=false;std::cout << '[';bool first=true;
                for(float value:{p->value.origin[0],p->value.origin[1],p->value.origin[2],p->value.velocity[0],
                    p->value.velocity[1],p->value.velocity[2],p->value.born,p->value.die,p->value.size,
                    p->initialSize,p->value.growth,p->value.alpha,p->mass,p->value.roll,slot.nextPvsCheck})
                {if(!first)std::cout << ',';first=false;std::cout << std::bit_cast<std::uint32_t>(value);}
                std::cout << ',' << p->value.frame << ',' << unsigned(p->value.sprite) << ']';
            }
        };
        particlePool.VisitActive(print);
        std::cout << "],\"rng\":" << rngCalls << ",\"long\":" << longRngCalls << ",\"time\":" << timeCalls << "}\n";
    }
}
void MeteorUpdateOracle()
{
    std::uint32_t first;
    while(std::cin>>first)
    {
        float a[17];a[0]=std::bit_cast<float>(first);
        for(unsigned i=1;i<17;++i)a[i]=InputBits();
        meteor::Particle p;
        p.value.origin={a[0],a[1],a[2]};p.value.velocity={a[3],a[4],a[5]};
        p.value.born=a[6];p.value.die=a[7];p.value.size=a[8];p.initialSize=a[9];
        p.value.growth=a[10];p.value.alpha=a[11];p.mass=a[12];p.value.roll=a[13];
        meteor::Tick(p,a[14],a[15],a[16],1);
        std::cout << '[';bool firstValue=true;
        for(float value:{p.value.born,p.value.die,p.value.size,p.value.alpha,p.value.roll,
            p.value.origin[0],p.value.origin[1],p.value.origin[2],p.value.velocity[0],p.value.velocity[1],p.value.velocity[2],
            p.initialSize,p.value.growth,p.mass})
        {if(!firstValue)std::cout << ',';firstValue=false;std::cout << std::bit_cast<std::uint32_t>(value);}
        std::cout << ',' << p.value.frame << "]\n";
    }
}
void MeteorStopOracle()
{
    unsigned owner;
    while(std::cin>>owner)
    {
        Fresh();
        for(unsigned i=0;i<128;++i)
        {
            auto& r=const_cast<Record&>(emitters.Records()[i]);
            r={};r.tag=i%3==0?2:i%3==1?99:0;r.total=-1;r.remaining=3;r.active=i%3;
            meteor::Emitter event;event.owner=std::uint16_t((i/3)%3==0?0:(i/3)%3==1?1:7);
            event.size=20;event.spread=12;event.growth=2;
            r.payload=event;r.model=&models[4];r.next=10.f+float(i)*.125f;r.interval=.01f;
        }
        emitters.StopTagged(2,std::uint16_t(owner),EmitterOwner);
        std::cout << '[';
        for(unsigned i=0;i<128;++i)
        {
            if(i)std::cout << ',';
            const auto& r=emitters.Records()[i];
            std::cout << '[' << r.tag << ',' << EmitterOwner(r.payload) << ',' << r.total << ',' << r.remaining << ','
                << r.active << ',' << std::bit_cast<std::uint32_t>(r.next) << ','
                << std::bit_cast<std::uint32_t>(r.interval) << ',' << (r.model==&models[4]?1:0) << ']';
        }
        std::cout << "]\n";
    }
}

void CameraOracle()
{
    int mode, target, secondary;
    while(std::cin >> mode >> target >> secondary)
    {
        Fresh(); observerState[0]=mode;observerState[1]=target;observerState[2]=secondary;
        for(unsigned i=0;i<3;++i)Check(bool(std::cin >> renderCamera[i]),"missing render camera");
        for(unsigned i=0;i<3;++i)Check(bool(std::cin >> chaseCamera[i]),"missing chase camera");
        for(unsigned i=0;i<3;++i)Check(bool(std::cin >> view.viewangles[i]),"missing engine camera");
        Camera camera{epoch};
        const auto angles=camera();
        std::cout << '[' << std::bit_cast<std::uint32_t>(angles[0]) << ','
            << std::bit_cast<std::uint32_t>(angles[1]) << ',' << std::bit_cast<std::uint32_t>(angles[2])
            << ',' << viewAngleCalls << "]\n";
    }
}

void MathValues()
{
    Check(preciseMath.Initialize(),"precise-math init");
    double value;
    while(std::cin >> value)
        std::cout << '[' << std::bit_cast<std::uint64_t>(preciseMath.cosine(value)) << ','
            << std::bit_cast<std::uint64_t>(preciseMath.sine(value)) << "]\n";
}
void MathIdentity()
{
    Check(preciseMath.Initialize(),"precise-math init");
    char filename[32768]{};
    const auto size=GetModuleFileNameA(preciseMath.Module(),filename,sizeof(filename));
    Check(size && size<sizeof(filename),"precise module path");
    // Resolve through this x86 process's actual file handle; a 64-bit Python
    // verifier would otherwise reinterpret redirected System32 as the x64 DLL.
    const auto file=CreateFileA(filename,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"precise module file");
    char finalPath[32768]{};
    const auto finalSize=GetFinalPathNameByHandleA(file,finalPath,sizeof(finalPath),FILE_NAME_NORMALIZED);
    CloseHandle(file);
    Check(finalSize && finalSize<sizeof(finalPath),"actual x86 CRT file path");
    for(unsigned i=0;i<finalSize;++i) if(finalPath[i]=='\\')finalPath[i]='/';
    const char* actual=std::string_view(finalPath).starts_with("//?/") ? finalPath+4 : finalPath;
    const auto base=reinterpret_cast<std::uintptr_t>(preciseMath.Module());
    std::cout << "{\"path\":" << std::quoted(actual) << ",\"cosineRva\":"
        << reinterpret_cast<std::uintptr_t>(GetProcAddress(preciseMath.Module(),"_libm_sse2_cos_precise"))-base
        << ",\"sineRva\":"
        << reinterpret_cast<std::uintptr_t>(GetProcAddress(preciseMath.Module(),"_libm_sse2_sin_precise"))-base << "}\n";
}

void SmokeRingOracle()
{
    std::string raw;
    while(std::cin >> raw)
    {
        Fresh(); std::vector<std::uint8_t> packet;
        for(std::size_t i=0;i<raw.size();i+=2) packet.push_back(static_cast<std::uint8_t>(std::stoul(raw.substr(i,2),nullptr,16)));
        SendSmoke(packet); Check(Pending()==36,"real ring accepted");
        std::cout << '[';
        for(std::size_t i=0;i<36;++i)
        {
            if(i)std::cout << ',';
            const auto& e=std::get<smoke::Emitter>(emitters.Records()[i].payload);
            std::cout << '[';
            bool first=true;
            for(float f : {e.origin[0],e.origin[1],e.origin[2],e.velocity[0],e.velocity[1],e.velocity[2],e.spread,e.size,e.growth,emitters.Records()[i].next})
            {
                if(!first)std::cout << ',';
                first=false; std::cout << std::bit_cast<std::uint32_t>(f);
            }
            std::cout << ',' << emitters.Records()[i].remaining << ']';
        }
        std::cout << "]\n";
    }
}

void EmitterOracle()
{
    std::string raw;
    while (std::cin >> raw)
    {
        Fresh(); std::vector<std::uint8_t> packet;
        for (std::size_t i = 0; i < raw.size(); i += 2) packet.push_back(static_cast<std::uint8_t>(std::stoul(raw.substr(i,2), nullptr, 16)));
        Send(packet); Check(Pending() == 1, "oracle raw packet accepted");
        const auto event = std::get<spark::Emitter>(emitters.Records()[0].payload); Random random{epoch};
        const auto p = spark::Spawn(event, 10.5f, random);
        std::cout << '[' << unsigned(event.sprite) << ',' << unsigned(event.rotation) << ',' << event.interval;
        for (float f : event.origin) std::cout << ',' << f;
        for (auto c : event.color) std::cout << ',' << unsigned(c);
        for (float f : p.velocity) std::cout << ',' << f;
        std::cout << ',' << p.born << ',' << p.die << ',' << p.rotationVelocity << "]\n";
    }
}

void GiftCases()
{
    Fresh(); auto gift=Packet();
    gift[31]=gift[32]=0;
    const auto setGravity=[&](float value)
    { const auto bits=std::bit_cast<std::uint32_t>(value);for(unsigned i=0;i<4;++i)gift[62+i]=static_cast<std::uint8_t>(bits>>(8*i)); };
    Check(!spark::Read(gift),"zero-speed gift cannot use spawn gravity");
    setGravity(.01f);
    const auto parsed=spark::Read(gift);
    Check(parsed && parsed->velocity==spark::Vec3{} && parsed->gravity==.01f,"gift velocity and gravity preserved");
    Send(gift);Frame(10.01f);
    Check(particlePool.ActiveCount()==1,"gift uses the actual shared ordinary pool");
    auto inspect=[](const Slot& slot)
    {
        const auto& p=std::get<spark::Particle>(slot.particle);
        Check(p.velocity==spark::Vec3{-30,7.5f,30} && p.gravity==.01f,"actual gift Spawn uses message fields");
    };
    particlePool.VisitActive(inspect);
    gift[31]=0x40;gift[32]=0x06;
    Check(!spark::Read(gift),"spawn velocity cannot use gift gravity");
    setGravity(std::numeric_limits<float>::quiet_NaN());Check(!spark::Read(gift),"nonfinite gravity rejected");
    setGravity(.01f);gift[31]=gift[32]=0;gift[27]=1;
    Check(!spark::Read(gift),"unproved gift horizontal speed rejected");
    gift[27]=0;gift.pop_back();Check(!spark::Read(gift),"truncated gift rejected");
}

void GiftRuntimeOracle()
{
    std::string raw;
    while(std::cin>>raw)
    {
        const float now=InputBits(),previousTime=InputBits(),gravity=InputBits();
        Fresh();auto bytes=HexPacket(raw);Send(bytes);
        Check(Pending()==1,"gift oracle actual Receive accepted");Frame(10.01f);
        Check(particlePool.ActiveCount()==1,"gift oracle actual shared Frame emitted");
        auto print=[&](const Slot& slot)
        {
            auto p=std::get<spark::Particle>(slot.particle);
            const bool alive=spark::Tick(p,now,previousTime,gravity,
                std::get<spark::Emitter>(emitters.Records()[0].payload).sprite?17:16,slot.model->numframes);
            std::cout<<'['<<(alive?1:0);
            for(float value:{p.size,p.alpha,p.roll,p.die,p.origin[0],p.origin[1],p.origin[2],
                p.velocity[0],p.velocity[1],p.velocity[2],static_cast<float>(p.frame),p.born,p.growth,p.rotationVelocity,p.gravity})
                std::cout<<','<<std::bit_cast<std::uint32_t>(value);
            std::cout<<"]\n";
        };
        particlePool.VisitActive(print);
    }
}
}


int main(int argc, char** argv)
{
    std::cout << std::setprecision(9);
    if (argc == 2 && std::string(argv[1]) == "--sliding-runtime-oracle") { test::SlidingRuntimeOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--frustum-oracle") { test::FrustumOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--visibility-oracle") { test::VisibilityOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--meteor-runtime-oracle") { test::MeteorRuntimeOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--meteor-update-oracle") { test::MeteorUpdateOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--meteor-stop-oracle") { test::MeteorStopOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--camera-oracle") { test::CameraOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--math-values") { test::MathValues(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--math-identity") { test::MathIdentity(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--smoke-ring-oracle") { test::SmokeRingOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--emitter-oracle") { test::EmitterOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--gift-runtime-oracle") { test::GiftRuntimeOracle(); return 0; }
    if (argc == 2 && std::string(argv[1]) == "--geometry-oracle")
    {
        float a[10];
        while (std::cin >> a[0])
        {
            for (unsigned i = 1; i < 10; ++i) if (!(std::cin >> a[i])) return 2;
            csol::teddy_sparks::Particle p;
            p.origin = {a[0], a[1], a[2]}; p.size = a[3];
            const auto quad = csol::teddy_sparks::Corners(p, {a[4], a[5], a[6]}, {a[7], a[8], a[9]});
            std::cout << '[';
            for (unsigned i = 0; i < 12; ++i)
                std::cout << (i ? "," : "") << quad[i / 3][i % 3];
            std::cout << "]\n";
        }
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--particle-oracle")
    {
        float a[19];
        while (std::cin >> a[0])
        {
            for (unsigned i = 1; i < 19; ++i) if (!(std::cin >> a[i])) return 2;
            csol::teddy_sparks::Particle p;
            p.born = a[0]; p.die = a[1]; p.size = a[2]; p.growth = a[3]; p.alpha = a[4];
            for (unsigned i = 0; i < 3; ++i) { p.origin[i] = a[5+i]; p.velocity[i] = a[8+i]; }
            p.roll = a[14]; p.rotationVelocity = a[15]; p.frame = static_cast<int>(a[18]);
            const bool alive = csol::teddy_sparks::Tick(p, a[11], a[12], a[13],
                static_cast<int>(a[16]), static_cast<int>(a[17]));
            std::cout << '[' << (alive ? 1 : 0) << ',' << p.size << ',' << p.alpha << ',' << p.roll << ',' << p.die;
            for (float v : p.origin) std::cout << ',' << v;
            for (float v : p.velocity) std::cout << ',' << v;
            std::cout << ',' << p.frame << ',' << p.born << ',' << p.growth << ',' << p.rotationVelocity << "]\n";
        }
        return 0;
    }
    try { test::Cases(); test::SharedCases(); test::FrustumCases(); test::SlidingCases(); test::MeteorCases(); test::GiftCases(); std::cout << "{\"success\":true,\"checks\":" << test::checks << "}\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
