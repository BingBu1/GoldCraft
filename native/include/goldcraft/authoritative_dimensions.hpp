#pragma once
#include <cmath>
#include <cstdint>
#include <ostream>

namespace goldcraft {
constexpr float authoritative_body_min = 0.1f;
constexpr float authoritative_body_max = 4.0f;
// Camera eye height is independent from the instantaneous collision body.
// Epic Fight resetSize changes the body/AABB without changing Entity.eyeHeight.
// Retain the old validator's global eye envelope (maximum body height + 0.1).
constexpr float authoritative_eye_max = authoritative_body_max + 0.1f;
enum class DimensionRejection : std::uint32_t { none, flags, non_finite, width, height, eye };
inline DimensionRejection validate_authoritative_dimensions(std::uint32_t flags, float eye, float width, float height) noexcept {
    if (flags & ~7u) return DimensionRejection::flags;
    if (!std::isfinite(eye) || !std::isfinite(width) || !std::isfinite(height)) return DimensionRejection::non_finite;
    if (width < authoritative_body_min || width > authoritative_body_max) return DimensionRejection::width;
    if (height < authoritative_body_min || height > authoritative_body_max) return DimensionRejection::height;
    if (eye < 0 || eye > authoritative_eye_max) return DimensionRejection::eye;
    return DimensionRejection::none;
}
class AuthoritativeDimensionTrace {
    std::uint64_t accepted_ = 0, eye_above_body_ = 0, rejected_ = 0;
    std::uint32_t slot_ = 0, life_ = 0, flags_ = 0;
    DimensionRejection reason_ = DimensionRejection::none;
    float eye_ = 0, width_ = 0, height_ = 0;
public:
    void observe(DimensionRejection result, std::uint32_t slot, std::uint32_t life, std::uint32_t flags, float eye, float width, float height) noexcept {
        if (result == DimensionRejection::none) {
            ++accepted_; if (eye > height + 0.1f) ++eye_above_body_;
            return;
        }
        ++rejected_; reason_ = result; slot_ = slot; life_ = life; flags_ = flags;
        eye_ = eye; width_ = width; height_ = height;
    }
    void write(std::ostream& out) const {
        const auto number = [&out](float value) { if (std::isfinite(value)) out << value; else out << "null"; };
        out << "\"authoritativeDimensions\":{\"accepted\":" << accepted_ << ",\"eyeAboveBody\":" << eye_above_body_
            << ",\"rejected\":" << rejected_ << ",\"lastReason\":" << static_cast<std::uint32_t>(reason_)
            << ",\"slot\":" << slot_ << ",\"life\":" << life_ << ",\"flags\":" << flags_ << ",\"eye\":";
        number(eye_); out << ",\"width\":"; number(width_); out << ",\"height\":"; number(height_); out << '}';
    }
};
}
