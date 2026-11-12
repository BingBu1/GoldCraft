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
unsigned checks{}, timeCalls{}, loadCalls{}, modelCalls{}, rngCalls{}, chainCalls{}, hookCalls{}, begins{}, ends{}, vertices{}, pvsCalls{}, localCalls{};
unsigned rngAxis{};
float clockNow{10};
std::string resetAt;
bool reenterRandom{}, resetRandom{}, noModel{}, inPvs{true};
model_t models[2]{};
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
std::size_t sampleAt{};
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
    ResetTeddySparks(); emitters.Initialize();
    resetAt.clear(); reenterRandom = resetRandom = noModel = false; inPvs = true;
    models[0] = {}; models[1] = {}; local = {};
    for (auto& model : models) model.type = mod_sprite;
    models[0].numframes = 29; models[1].numframes = 17;
    movement = {}; movement.gravity = 800;
    view = {}; view.movevars = &movement;
    view.viewangles[0] = 27; view.viewangles[1] = -49;
    engine = {}; triangles = {}; hookApi = {}; api = &hookApi; previous = Previous;
    samples.clear(); sampleAt = 0;
    engine.GetClientTime = []()
    {
        ++timeCalls; ResetCallback("time");
        return sampleAt < samples.size() ? samples[sampleAt++] : clockNow;
    };
    engine.pfnSPR_Load = [](const char* path)
    {
        ++loadCalls; ResetCallback("load");
        const unsigned n = std::string_view(path) == spark::Sprites[1] ? 1 : 0;
        return int(n + 2);
    };
    engine.GetSpritePointer = [](int handle) -> const model_t*
    {
        ++modelCalls; ResetCallback("model");
        return noModel ? nullptr : &models[handle == 3 ? 1 : 0];
    };
    engine.pfnRandomFloat = [](float lo, float hi)
    {
        static constexpr float xyz[]{-1, .25f, 1};
        const float value = lo == 1 ? 1.25f : xyz[rngAxis++ % 3]; ++rngCalls;
        randomBounds.push_back({lo, hi});
        if (reenterRandom) { reenterRandom = false; Send(reentryPacket); TeddySparksUpdateClientData(); DrawTeddySparks(); }
        if (resetRandom) { resetRandom = false; ResetTeddySparks(); }
        Check(lo <= value && value <= hi, "RNG boundary/order");
        return value;
    };
    engine.GetLocalPlayer = []() -> cl_entity_t* { ++localCalls; ResetCallback("local"); return &local; };
    engine.pfnAngleVectors = [](const float* angles, float* forward, float* right, float* up)
    {
        angleInputs.push_back({angles[0], angles[1], angles[2]});
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
    triangles.BoxInPVS = [](float*, float*) { ++pvsCalls; ResetCallback("pvs"); return int(inPvs); };
    engine.pTriAPI = &triangles;
    hookApi.HookUserMsg = [](const char* name, pfnUserMsgHook fn) { ++hookCalls; Check(std::string_view(name) == "TempEmit" && fn == Receive, "actual hook installation"); return Previous; };
    message_chain::installed.clear();
    // Empty transparent passes establish the independent prior-time clock.
    // Clear/VidInit intentionally do not reset that original clock.
    clockNow = 9; DrawTeddySparks(); clockNow = 10; DrawTeddySparks();
    received = emitted = invalid = exhausted = draws = 0;
    timeCalls = loadCalls = modelCalls = rngCalls = chainCalls = hookCalls = pvsCalls = localCalls = 0;
    rngAxis = 0; randomBounds.clear(); ClearDraw(); TeddySparksHudTime(10);
}
void Frame(float now) { clockNow = now; TeddySparksUpdateClientData(); }
Slot* Seed(float x, unsigned sprite = 0, float die = 20)
{
    auto* s = particlePool.Allocate(true);
    Check(s != nullptr, "fixture allocation");
    *s = {};
    s->model = &models[sprite]; s->nextPvsCheck = 100; s->inPvs = true;
    s->particle.sprite = static_cast<std::uint8_t>(sprite); s->particle.frame = 2;
    s->particle.color = {250,123,70}; s->particle.alpha = 240;
    s->particle.origin = {x,20,30}; s->particle.size = 180;
    s->particle.born = 9; s->particle.die = die;
    return s;
}
void DrawSeed()
{
    Fresh(); Seed(10); Seed(30,1); Seed(20);
}
void Cases()
{
    Fresh(); auto p0 = Packet(), p1 = Packet(1);
    InitializeTeddySparks(); InitializeTeddySparks();
    Check(hookCalls == 1 && previous == Previous, "message chain once");
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
    Fresh(); engine.GetSpritePointer = [](int) -> const model_t* { ++modelCalls; TeddySparksHudTime(30); return &models[0]; };
    Send(p0); Check(emitters.Records()[0].next == 30, "Add reads HUD cache after model callback");
    Fresh(); TeddySparksHudTime(0); Send(p0); Frame(1);
    Check(!emitted && emitters.Records()[0].remaining == 3, "zero-next disabled");
    Fresh(); noModel = true; Send(p0); Frame(10.5f);
    Check(received == 1 && emitters.Records()[0].remaining == 2 && invalid == 1 && !rngCalls, "missing model retains original consumed emitter");
    Fresh(); for (unsigned i = 0; i < 128; ++i) Send(p0);
    Check(Pending() == 128 && received == 128 && loadCalls == 128, "128 shared records load through engine cache");
    Send(p1); Check(exhausted == 1 && loadCalls == 128 && modelCalls == 128, "full pool does not load");
    ResetTeddySparks(); Check(!Pending() && !particlePool.ActiveCount(), "host reset clears particle then emitter records");
    Check(particlePool.PreviousTime() == 10, "clear preserves previous clock");

    Fresh(); Send(p0); Send(p1);
    for (unsigned i = 0; i < 1200; ++i) Seed(float(i));
    Frame(10.5f);
    Check(exhausted == 1 && emitters.Records()[0].remaining == 2 && emitters.Records()[1].remaining == 3 && !rngCalls, "factory full consumes and stops pass");
    Fresh(); DrawTeddySparks(); Check(!rngCalls && !loadCalls && !pvsCalls && timeCalls == 2, "empty transparent pass advances independent clock");
    Send(p0); Frame(10.01f);
    float bornX{};
    auto readOrigin = [&](const Slot& s) { bornX = s.particle.origin[0]; };
    particlePool.VisitActive(readOrigin);
    Check(bornX == 10, "emission has not integrated before transparent phase");
    DrawTeddySparks(); particlePool.VisitActive(readOrigin);
    Check(bornX == 10 + -30.f * (clockNow - 10), "transparent phase integrates original delta");
    const auto oldDraws = draws; const auto oldRng = rngCalls; DrawTeddySparks(); particlePool.VisitActive(readOrigin);
    Check(draws == oldDraws + 1 && rngCalls == oldRng && bornX == 10 + -30.f * (clockNow - 10), "same-clock redraw without update or emission");

    Fresh(); Send(p0); resetRandom = true; Frame(10.5f);
    Check(!Pending() && !particlePool.ActiveCount() && rngCalls == 1 && !emitted, "RNG reset invalidates allocation");
    Fresh(); Send(p0); reentryPacket = p1; reenterRandom = true; Frame(10.5f);
    Check(invalid == 1 && received == 1 && emitted == 1 && rngCalls == 4, "RNG reentry rejected");
    for (const char* stage : {"load","model"})
    { Fresh(); resetAt = stage; Send(p0); Check(!received && !Pending() && !particlePool.ActiveCount(), "resource callback invalidates Add"); }

    DrawSeed(); DrawTeddySparks();
    Check(begins == 3 && ends == 3 && vertices == 12 && binds.size() == 3, "all sorted particles drawn once");
    Check(binds[0][0] == 1 && binds[1][0] == 0 && binds[2][0] == 0 &&
        positions[0][0] == -60 && positions[4][0] == -70 && positions[8][0] == -80, "distance order preserved across sprite groups");
    Check(colors[0] == std::array<float,4>{250.f/255,123.f/255,70.f/255,240.f/255}, "additive RGB alpha");
    Check(modes.front() == kRenderTransAdd && modes.back() == kRenderNormal && culls.back() == TRI_FRONT, "draw API state restore");
    Check(uv[0] == std::array<float,2>{0,0} && uv[1] == std::array<float,2>{0,1} && uv[2] == std::array<float,2>{1,1} && uv[3] == std::array<float,2>{1,0}, "quad UV order");
    view.viewangles[0] = 45; DrawTeddySparks();
    Check(angleInputs.back()[0] == 27, "equal time keeps original camera cache");
    Check(!loadCalls && !rngCalls, "same-clock draw has no resource IO or RNG");
    for (const char* stage : {"time","local","angles","sprite","mode","cull","begin","color","tex","vertex","end"})
    {
        DrawSeed(); resetAt = stage; DrawTeddySparks();
        Check(begins == ends && !particlePool.ActiveCount() && !Pending(), "callback reset balances draws and invalidates traversal");
    }
    Fresh(); auto* s = Seed(10); s->nextPvsCheck = 0; resetAt = "pvs"; DrawTeddySparks();
    Check(!particlePool.ActiveCount() && !vertices, "PVS reset aborts particle traversal");
    Fresh(); s = Seed(10,0,10); DrawTeddySparks();
    Check(!particlePool.ActiveCount() && !vertices, "queued dying particle skips exact-die Draw");
    Fresh(); s = Seed(10,0,9); DrawTeddySparks();
    Check(!particlePool.ActiveCount() && vertices == 4, "queued already-expired particle preserves original equality-only Draw gate");
    Fresh(); s = Seed(10); s->particle.frame = models[0].numframes; DrawTeddySparks();
    Check(binds.size() == 1 && binds[0][1] == 29, "draw preserves frame==frames");

    Fresh(); clockNow = 9; DrawTeddySparks(); s = Seed(10); s->particle.velocity = {5,0,0};
    samples = {10,10,10,9,10}; sampleAt = 0; clockNow = 10; DrawTeddySparks();
    Check(s->particle.origin[0] == 10 && particlePool.PreviousTime() == 10, "per-particle equal sample skips update using original previous time");
    samples.clear(); sampleAt = 0; clockNow = 9.5f; DrawTeddySparks();
    Check(s->particle.origin[0] == 7.5f, "rewind still updates with initial snapshot");
    Fresh(); s = Seed(10); s->nextPvsCheck = 0; inPvs = false; clockNow = 10.1f; DrawTeddySparks();
    Check(!vertices && s->particle.size > 180, "invisible particle still updates");

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
        Send(packet); Check(Pending() == 1, "oracle raw packet accepted");
        const auto event = emitters.Records()[0].payload; Random random{epoch};
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
