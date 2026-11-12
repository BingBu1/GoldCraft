// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "aurora_render.hpp"
#include <array>
#include <cstddef>
#include <string_view>
struct cl_enginefuncs_s;
namespace guest::cof::client
{
enum class AuroraResult { Applied, NotPrepared, InvalidInput, Unsupported, MissingBackend,
    ResourceFailed, ParseFailed, UnknownField, CapacityExceeded, RenderFailed };
struct AuroraRange { float left{},right{}; bool defined{}; };
enum class AuroraField : unsigned { Life,Gravity,Rate,Force,Pitch,Yaw,Size,EndSize,
    SizeDelta,Frame,FrameRate,Alpha,EndAlpha,Red,EndRed,Green,EndGreen,Blue,EndBlue,
    Angle,AngleDelta,Drag,WindStrength,WindYaw,Count };
struct AuroraType
{
    std::array<AuroraRange,static_cast<unsigned>(AuroraField::Count)> fields{};
    std::array<char,30> name{},sprayName{};
    bool spray{},sprite{};
};
struct AuroraParticle
{
    int next{-1},type{};
    AuroraRenderParticle render{};
    std::array<float,3> velocity{},acceleration{},wind{},colorDelta{};
    float alphaDelta{},sizeDelta{},frameDelta{},angleDelta{},drag{},age{},life{},nextSpray{};
    int owner{};
};
struct AuroraSystem
{
    bool used{},kill{};
    int owner{},state{},attachment{},head{-1},free{},main{-1};
    float deadline{},fadeStart{},fadeDuration{};
    std::array<char,128> model{};
    std::array<AuroraParticle,20> particles{};
private:
    friend class Aurora;
    AuroraSystem* next_{};
};
// Isolated first-four-pistol local gunsmoke. Single owner thread.
// Full retained nodes use stable 64-system slabs: cold initial allocation, rare Add
// growth, no Frame allocation. Empty systems retain their original logical life.
// The human authorized initializing proven raw Type controls and initial poolpos0;
// all other supported original defaults/order and reused slot positions persist.
class Aurora
{
public:
    static constexpr unsigned SystemsPerSlab=64;
    explicit Aurora(cl_enginefuncs_s* engine) noexcept : engine_(engine) {}
    ~Aurora() noexcept;
    Aurora(const Aurora&)=delete;
    Aurora& operator=(const Aurora&)=delete;
    Aurora(Aurora&&)=delete;
    Aurora& operator=(Aurora&&)=delete;
    AuroraResult Prepare(std::string_view script) noexcept;
    AuroraResult Add(int owner,std::string_view script,int local,
                     std::string_view model,int attachment,float particleLimit) noexcept;
    bool DeleteOwner(int owner) noexcept;
    void KillLocal(bool immediate) noexcept;
    void Clear() noexcept;
    void VideoInit() noexcept; // Original HUD_VidInit clears systems; resource must be prepared again.
    AuroraResult Frame(float elapsed,int mode,const std::array<float,3>& right,
                       const std::array<float,3>& up) noexcept;
    const AuroraSystem* First() const noexcept { return head_; }
    const AuroraSystem* Next(const AuroraSystem& s) const noexcept {return s.next_;}
    const std::array<AuroraType,2>& Types() const noexcept {return types_;}
private:
    struct Slab;
    bool Grow() noexcept;
    void Release(AuroraSystem&) noexcept;
    float Random(const AuroraType&,AuroraField) noexcept;
    int Create(AuroraSystem&,int type) noexcept;
    void Drain(AuroraSystem&) noexcept;
    bool Update(AuroraSystem&,AuroraParticle&,float) noexcept;
    cl_enginefuncs_s* engine_{};
    std::array<AuroraType,2> types_{};
    Slab* slabs_{};
    AuroraSystem* free_{};
    AuroraDegreeTable table_{};
    const model_s* sprite_{};
    bool prepared_{};
    AuroraSystem* head_{};
};
}
