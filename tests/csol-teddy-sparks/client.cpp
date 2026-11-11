// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/bosses/teddy_sparks.cpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <vector>

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
unsigned checks{}, timeCalls{}, loadCalls{}, rngCalls{}, chainCalls{}, hookCalls{}, begins{}, ends{}, vertices{}, pvsCalls{};
unsigned rngAxis{};
float clockNow{10};
std::string resetAt;
bool reenterRandom{}, resetRandom{};
model_t models[2]{};
movevars_t movement{};
triangleapi_t triangles{};
metahook_api_t hookApi{};
std::vector<std::array<float, 2>> randomBounds, uv;
std::vector<std::array<float, 4>> colors;
std::vector<std::array<int, 2>> binds;
std::vector<std::array<float, 3>> positions;
std::vector<int> modes, culls;
std::vector<std::uint8_t> reentryPacket;

void Check(bool good, const char* why)
{
    ++checks;
    if (!good) throw std::runtime_error(why);
}
void ResetCallback(const char* stage)
{
    if (resetAt == stage) { resetAt.clear(); ResetTeddySparks(); }
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
void Fresh()
{
    ResetTeddySparks();
    received = emitted = invalid = exhausted = draws = 0;
    timeCalls = loadCalls = rngCalls = chainCalls = hookCalls = begins = ends = vertices = pvsCalls = 0;
    rngAxis = 0;
    randomBounds.clear(); uv.clear(); colors.clear(); binds.clear(); positions.clear(); modes.clear(); culls.clear();
    resetAt.clear(); reenterRandom = resetRandom = false; clockNow = 10;
    models[0] = {}; models[1] = {};
    for (auto& model : models) model.type = mod_sprite;
    models[0].numframes = 29; models[1].numframes = 17;
    movement = {}; movement.gravity = 800;
    view = {}; view.movevars = &movement;
    engine = {}; triangles = {}; hookApi = {};
    api = &hookApi;
    previous = Previous;
    engine.GetClientTime = []() { ++timeCalls; ResetCallback("time"); return clockNow; };
    engine.CL_LoadModel = [](const char* path, int* index) -> model_t*
    {
        ++loadCalls; ResetCallback("load");
        const unsigned n = std::string_view(path) == spark::Sprites[1] ? 1 : 0;
        *index = n + 2; return &models[n];
    };
    engine.pfnRandomFloat = [](float lo, float hi)
    {
        static constexpr float xyz[]{-1, .25f, 1};
        const float value = lo == 1 ? 1.25f : xyz[rngAxis++ % 3]; ++rngCalls;
        randomBounds.push_back({lo, hi});
        if (reenterRandom) { reenterRandom = false; Send(reentryPacket); TeddySparksFrame(); DrawTeddySparks(); }
        if (resetRandom) { resetRandom = false; ResetTeddySparks(); }
        Check(lo <= value && value <= hi, "RNG boundary/order");
        return value;
    };
    engine.pfnAngleVectors = [](const float*, float* forward, float* right, float* up)
    {
        for (unsigned i = 0; i < 3; ++i) { forward[i] = i == 2; right[i] = i == 0; up[i] = i == 1; }
        ResetCallback("angles");
    };
    triangles.RenderMode = [](int mode) { modes.push_back(mode); ResetCallback("mode"); };
    triangles.CullFace = [](TRICULLSTYLE style) { culls.push_back(style); ResetCallback("cull"); };
    triangles.SpriteTexture = [](model_t* model, int frame) { binds.push_back({model == &models[1] ? 1 : 0, frame}); ResetCallback("sprite"); return 1; };
    triangles.Begin = [](int primitive) { Check(primitive == TRI_QUADS, "quad primitive"); ++begins; ResetCallback("begin"); };
    triangles.End = []() { ++ends; ResetCallback("end"); };
    triangles.Color4f = [](float r, float g, float b, float a) { colors.push_back({r,g,b,a}); ResetCallback("color"); };
    triangles.TexCoord2f = [](float u, float v) { uv.push_back({u,v}); ResetCallback("tex"); };
    triangles.Vertex3f = [](float x, float y, float z) { ++vertices; positions.push_back({x,y,z}); ResetCallback("vertex"); };
    triangles.BoxInPVS = [](float*, float*) { ++pvsCalls; ResetCallback("pvs"); return 1; };
    engine.pTriAPI = &triangles;
    hookApi.HookUserMsg = [](const char* name, pfnUserMsgHook fn) { ++hookCalls; Check(std::string_view(name) == "TempEmit" && fn == Receive, "actual hook installation"); return Previous; };
    message_chain::installed.clear();
}
void Frame(float now) { clockNow = now; TeddySparksFrame(); }
void DrawSeed()
{
    Fresh();
    sprites = {&models[0], &models[1]}; haveTime = true; lastTime = clockNow;
    particleEnd = 3;
    for (unsigned i = 0; i < particleEnd; ++i)
    {
        particles[i] = {}; particles[i].active = true; particles[i].inPvs = true;
        auto& p = particles[i].particle;
        p.sprite = i == 2 ? 1 : 0; p.frame = 2; p.color = {250,123,70}; p.alpha = 240;
        p.origin = {10,20,30}; p.size = 180;
    }
}
void Cases()
{
    Fresh(); auto p0 = Packet(), p1 = Packet(1);
    InitializeTeddySparks(); InitializeTeddySparks();
    Check(hookCalls == 1 && previous == Previous, "message chain once");
    std::vector<std::uint8_t> other{1,2,3};
    Check(Send(other) == 73 && chainCalls == 1, "non-owned message preserved");
    auto bad = p0; bad.pop_back(); Check(Send(bad) == 1 && invalid == 1 && !emitterEnd && !loadCalls, "owned malformed rejected");
    for (unsigned at : {19u, 33u, 39u, 43u, 47u, 48u, 49u, 50u, 54u, 58u, 62u, 66u, 70u, 74u, 78u})
    { bad = p0; bad[at] ^= 0x40; Check(Send(bad) == 1 && !emitterEnd, "malformed field rejected"); }
    bad = p0; bad[51] = 0; Check(Send(bad) == 1 && !emitterEnd, "out-of-range RGB rejected");
    Check(Send(p0) == 1 && Send(p1) == 1 && emitterEnd == 2 && loadCalls == 2, "dual packet actual Receive");
    Check(emitters[0].remaining == 3 && emitters[1].parameters.sprite == 1, "dual emitter state");
    Frame(10); Check(!emitted && !rngCalls, "strict initial equal");
    Frame(10.5f); Check(emitted == 2 && rngCalls == 8 && emitters[0].remaining == 2, "one per emitter due");
    const auto strict = emitters[0].next; Frame(strict); Check(emitted == 2, "strict next equal");
    Frame(100); Check(emitted == 4 && emitters[0].remaining == 1 && emitters[1].remaining == 1, "no catchup");
    Frame(101); Check(emitted == 6 && !emitterEnd, "three emissions total");
    Check(loadCalls == 2, "no frame resource IO");

    Fresh(); clockNow = 0; Send(p0); Frame(1); Check(!emitted && emitters[0].remaining == 3, "zero-next disabled");
    Fresh(); for (unsigned i = 0; i < 128; ++i) Send(p0);
    Check(emitterEnd == 128 && received == 128 && loadCalls == 1, "128 emitter pool/cache");
    Send(p1); Check(exhausted == 1 && received == 128 && loadCalls == 1, "full emitter does not load");
    emitters[7].remaining = emitters[41].remaining = 0; Send(p1);
    Check(emitters[7].remaining == 3 && emitters[7].parameters.sprite == 1 && emitters[41].remaining == 0, "first free emitter");

    Fresh(); Send(p0); Send(p1); particleEnd = spark::ParticleCapacity;
    for (auto& slot : particles) { slot = {}; slot.active = true; slot.particle.born = 10; slot.particle.die = 20; }
    Frame(10.5f);
    Check(exhausted == 1 && emitters[0].remaining == 2 && emitters[1].remaining == 3 && !rngCalls, "1200 factory full consumes then ends pass");
    particles[13].active = false; Frame(10.51f);
    Check(emitted == 1 && particles[13].active && particles[13].particle.sprite == 1 && emitters[1].remaining == 2, "pending emitter uses first free particle");

    Fresh(); Frame(100); Frame(200);
    Check(!loadCalls && !rngCalls && !pvsCalls && timeCalls == 2 && lastTime == 200, "empty frame O1 clock only");
    clockNow = 200; Send(p0); const float now = 200.016f; Frame(now);
    Check(particleEnd == 1 && particles[0].active, "idle spawn remains active");
    Check(particles[0].particle.origin[0] == 10.f + (-30.f) * (now - 200.f), "idle gap delta only current frame");
    const auto before = particles[0].particle; const auto oldLast = lastTime;
    clockNow = 199; Send(p1);
    Check(particleEnd == 1 && particles[0].particle.origin == before.origin && lastTime == oldLast, "lagged message preserves live effects/time");

    Fresh(); Send(p0); resetRandom = true; Frame(10.5f);
    Check(!emitterEnd && !particleEnd && !sprites[0] && rngCalls == 1, "RNG reset aborts stale slot commit");
    clockNow = 20; Send(p1); Frame(20.01f); Check(particleEnd == 1 && particles[0].particle.sprite == 1, "session reset overwrites retired slot");
    Fresh(); Send(p0); reentryPacket = p1; reenterRandom = true; Frame(10.5f);
    Check(invalid == 1 && received == 1 && emitted == 1 && rngCalls == 4, "RNG reentry blocked without duplicate emission");
    Fresh(); resetAt = "load"; Send(p0); Check(!emitterEnd && !sprites[0], "loader reset aborts stale resource");
    Fresh(); Send(p0); resetAt = "pvs"; Frame(10.5f); Check(!particleEnd && !emitterEnd, "PVS reset aborts live integration");

    DrawSeed(); DrawTeddySparks();
    Check(begins == 2 && ends == 2 && vertices == 12 && binds.size() == 2, "state-compatible dual sprite batches");
    Check(binds[0] == std::array<int,2>{0,2} && binds[1] == std::array<int,2>{1,2}, "sprite frame texture binding");
    Check(colors.size() == 4 && colors[0] == std::array<float,4>{250.f/255,123.f/255,70.f/255,240.f/255}, "additive RGB alpha");
    Check(modes.front() == kRenderTransAdd && modes.back() == kRenderNormal && culls.back() == TRI_FRONT, "draw API state restore");
    Check(uv[0] == std::array<float,2>{0,0} && uv[1] == std::array<float,2>{0,1} && uv[2] == std::array<float,2>{1,1} && uv[3] == std::array<float,2>{1,0}, "quad UV order");
    Check(!loadCalls && !rngCalls, "draw does not perform resource IO or RNG at same time");
    for (const char* stage : {"mode","cull","sprite","begin","angles","color","tex","vertex","end"})
    {
        DrawSeed(); resetAt = stage; DrawTeddySparks();
        Check(begins == ends && modes.back() == kRenderNormal && culls.back() == TRI_FRONT, "callback reset balances BeginEnd/restores state");
        Check(!particleEnd && !emitterEnd, "callback reset leaves no live stale effects");
    }
    DrawSeed(); particles[0].particle.frame = models[0].numframes; particles[1].active = particles[2].active = false;
    DrawTeddySparks(); Check(binds.size() == 1 && binds[0][1] == 29, "draw preserves frame==frames");
    spark::Particle boundary; boundary.born = 10; boundary.die = 20;
    Check(spark::Tick(boundary, 11, 10, 800, 17, 17) && boundary.frame == 17, "strict finite animation equality");
    Check(!spark::Tick(boundary, std::nextafter(11.f, 12.f), 11, 800, 17, 17), "strict finite animation after bound");
}
void EmitterOracle()
{
    std::string raw;
    while (std::cin >> raw)
    {
        Fresh(); std::vector<std::uint8_t> packet;
        for (std::size_t i = 0; i < raw.size(); i += 2) packet.push_back(static_cast<std::uint8_t>(std::stoul(raw.substr(i,2), nullptr, 16)));
        Send(packet); Check(emitterEnd == 1, "oracle raw packet accepted");
        const auto event = emitters[0].parameters; Random random{epoch};
        const auto p = spark::Spawn(event, 10.5f, random);
        std::cout << '[' << unsigned(event.sprite) << ',' << unsigned(event.rotation) << ',' << event.interval;
        for (float f : event.origin) std::cout << ',' << f;
        for (auto c : event.color) std::cout << ',' << unsigned(c);
        for (float f : p.velocity) std::cout << ',' << f;
        std::cout << ',' << p.born << ',' << p.die << ',' << p.rotationVelocity << "]\n";
    }
}
}

int main(int argc, char** argv)
{
    std::cout << std::setprecision(9);
    if (argc == 2 && std::string(argv[1]) == "--emitter-oracle") { test::EmitterOracle(); return 0; }
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
    try { test::Cases(); std::cout << "{\"success\":true,\"checks\":" << test::checks << "}\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
