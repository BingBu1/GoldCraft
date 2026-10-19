#pragma once
#include "particles.hpp"
#include <algorithm>

namespace goldcraft {
constexpr unsigned max_post_effect_stages=32;
enum class PostEffectKind : std::uint8_t { bloom=1,space_broken=2,chromatic=3,air=4,screen_distortion=5,color_dispersion=6,bw_contrast=7,hsv_filter=8 };
inline constexpr unsigned post_effect_parameter_count(unsigned kind) noexcept {
    return kind==static_cast<unsigned>(PostEffectKind::bw_contrast)?30u:((kind>=1&&kind<=6)||kind==8?8u:0u);
}
struct PostEffectStage {
    std::uint8_t kind=0;
    std::int32_t priority=0;
    std::uint8_t first_batch=0,batch_count=0;
    // time, direction X/Y, strength, frequency/base, progress, upper, lower.
    // Color dispersion: Level RGB, Center XY, ColorModulate RGB.
    // HSV: Hue_W XY, Alpha, pre-blit ColorModulate RGBA, reserved zero.
    // BW contrast: Contrast, Brightness, Time, Intensity, Speed, Mode,
    // ImpactThreshold, ImpactThresholdLerp, FocalUV XY, ChromaticStrength,
    // LensDistortStrength, ColorDark RGB, ColorLight RGB, InvertFactor,
    // CappedGrayscale, RadialStrength, integral Samples, output clear RGBA,
    // and the preceding blit pass's effective ColorModulate RGBA.
    // Old kinds use exactly eight floats on wire and must keep this tail zero.
    std::array<float,30> params{};
};
struct PostEffectFrame {
    ParticleSnapshot mesh{};
    std::vector<PostEffectStage> stages;
    std::vector<std::uint8_t> depth_resets;
};
inline bool valid_post_effect_stage(const PostEffectStage& stage) noexcept {
    const auto count=post_effect_parameter_count(stage.kind);
    if(!count||stage.priority<-32768||stage.priority>32767||
       stage.first_batch>max_particle_batches||stage.batch_count>max_particle_batches-stage.first_batch)
        return false;
    if(stage.kind==static_cast<unsigned>(PostEffectKind::screen_distortion)||stage.kind==static_cast<unsigned>(PostEffectKind::color_dispersion)||stage.kind==static_cast<unsigned>(PostEffectKind::bw_contrast)||stage.kind==static_cast<unsigned>(PostEffectKind::hsv_filter)) {
        if(stage.batch_count)return false;
    }else if(!stage.batch_count)return false;
    if(stage.kind==static_cast<unsigned>(PostEffectKind::color_dispersion)&&
       (stage.params[0]<=0||stage.params[1]<=0||stage.params[2]<=0))return false;
    for(float p:stage.params)if(!std::isfinite(p)||std::abs(p)>1000000.f)return false;
    for(unsigned i=count;i<stage.params.size();++i)if(stage.params[i]!=0)return false;
    if(stage.kind==static_cast<unsigned>(PostEffectKind::hsv_filter)&&stage.params[7]!=0)return false;
    if(stage.kind==static_cast<unsigned>(PostEffectKind::bw_contrast)){
        for(unsigned i:{2u,8u,9u})if(stage.params[i]<0||stage.params[i]>1)return false;
        if(stage.params[21]!=std::trunc(stage.params[21]))return false;
    }
    return true;
}
// One atomic frame carries the ordered graph and its private mask geometry.
// No separate ordinary mesh can be mistaken for an unpaired post mask, even
// when either latest-frame queue coalesces or a send is dropped.
inline PostEffectFrame read_post_effects(Reader& reader,bool hsv_supported=false) {
    PostEffectFrame frame;
    const auto count=reader.u8();
    if(count>max_post_effect_stages)throw ProtocolError("Post-effect stage budget");
    frame.stages.reserve(count);
    for(unsigned i=0;i<count;++i) {
        PostEffectStage stage;
        stage.kind=reader.u8();stage.priority=reader.i32();
        if(stage.kind==static_cast<unsigned>(PostEffectKind::hsv_filter)&&!hsv_supported)
            throw ProtocolError("Unnegotiated HSV post stage");
        stage.first_batch=reader.u8();stage.batch_count=reader.u8();
        const auto parameter_count=post_effect_parameter_count(stage.kind);
        if(!parameter_count)throw ProtocolError("Unknown post-effect kind");
        for(unsigned p=0;p<parameter_count;++p)stage.params[p]=reader.f32();
        if(!valid_post_effect_stage(stage))throw ProtocolError("Invalid post-effect stage");
        frame.stages.push_back(stage);
    }
    const auto reset_count=reader.u8();
    if(reset_count>max_particle_batches)throw ProtocolError("Post depth reset budget");
    for(unsigned i=0;i<reset_count;++i)frame.depth_resets.push_back(reader.u8());
    const auto size=reader.u32();
    if(size>max_payload||size!=reader.remaining())throw ProtocolError("Invalid post-effect mesh length");
    Reader mesh_reader(reader.bytes(size));frame.mesh=read_particles(mesh_reader,false);reader.finish();
    std::array<bool,max_particle_batches> covered{};
    for(const auto& stage:frame.stages) {
        if(stage.first_batch>frame.mesh.batches.size()||
           stage.batch_count>frame.mesh.batches.size()-stage.first_batch)
            throw ProtocolError("Post-effect batch outside frame");
        for(unsigned i=stage.first_batch;i<unsigned(stage.first_batch)+stage.batch_count;++i) {
            if(covered[i])throw ProtocolError("Overlapping post-effect masks");
            covered[i]=true;
        }
    }
    for(unsigned i=0;i<frame.mesh.batches.size();++i)
        if(!covered[i])throw ProtocolError("Unreferenced post-effect mask");
    std::array<bool,max_particle_batches> resets{};
    for(const auto index:frame.depth_resets){
        if(index>=frame.mesh.batches.size()||resets[index])throw ProtocolError("Invalid post depth reset");
        const bool bloom=std::any_of(frame.stages.begin(),frame.stages.end(),[index](const auto& stage){
            return stage.kind==1&&index>=stage.first_batch&&unsigned(index)<unsigned(stage.first_batch)+stage.batch_count;
        });
        if(!bloom)throw ProtocolError("Post depth reset outside bloom mask");
        resets[index]=true;
    }
    return frame;
}
}
