#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>
#include <deque>

namespace goldcraft {
struct ViewPose { Vec3 feet{}; float eye = 1.62f; };

// Minecraft simulates at 20 Hz. Render the validated local prediction on its producer
// timeline, one tick behind, instead of displaying a new camera position in one step.
// No extrapolation: packet loss must never push the view through an unobserved wall.
class ViewInterpolator {
public:
    static constexpr double delay_seconds = 0.05;
    void clear() { samples_.clear(); clock_offset_ = 0; }
    bool push(ViewPose pose, double produced, double received) {
        if (!std::isfinite(produced) || !std::isfinite(received)) return false;
        if (!samples_.empty() && produced <= samples_.back().time) return false;
        bool reset = samples_.empty();
        if (!reset) {
            const auto& previous = samples_.back();
            const float dx = pose.feet.x - previous.pose.feet.x, dy = pose.feet.y - previous.pose.feet.y, dz = pose.feet.z - previous.pose.feet.z;
            reset = produced - previous.time > 0.25 || dx*dx + dy*dy + dz*dz > 16.0f;
        }
        if (reset) { samples_.clear(); clock_offset_ = received - produced; }
        else clock_offset_ = std::min(clock_offset_, received - produced);
        samples_.push_back({produced, pose});
        while (samples_.size() > 16) samples_.pop_front();
        return true;
    }
    ViewPose at(double now) const {
        if (samples_.empty()) return {};
        const double target = now - clock_offset_ - delay_seconds;
        if (target <= samples_.front().time) return samples_.front().pose;
        for (std::size_t i = 1; i < samples_.size(); ++i) {
            const auto& a = samples_[i-1]; const auto& b = samples_[i];
            if (target > b.time) continue;
            const auto t = static_cast<float>((target-a.time)/(b.time-a.time));
            return {{std::lerp(a.pose.feet.x,b.pose.feet.x,t),std::lerp(a.pose.feet.y,b.pose.feet.y,t),std::lerp(a.pose.feet.z,b.pose.feet.z,t)},std::lerp(a.pose.eye,b.pose.eye,t)};
        }
        return samples_.back().pose;
    }
private:
    struct Sample { double time; ViewPose pose; };
    std::deque<Sample> samples_;
    double clock_offset_ = 0;
};
}
