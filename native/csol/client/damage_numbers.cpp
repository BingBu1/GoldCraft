// SPDX-License-Identifier: GPL-3.0-or-later
#include "damage_numbers.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>

namespace csol::damage
{
namespace
{
bool Finite(Anchor anchor)
{
    return std::isfinite(anchor.x) && std::isfinite(anchor.y) && std::isfinite(anchor.z);
}
bool Valid(Animation animation)
{
    return std::isfinite(animation.duration) && animation.duration > 0 &&
        std::isfinite(animation.skipTime) && animation.skipTime >= 0 &&
        animation.skipTime <= animation.duration && std::isfinite(animation.speed);
}
}

ParseResult Decode(std::span<const std::byte> bytes, Message& message)
{
    if (bytes.empty() || bytes[0] != std::byte{Opcode}) return ParseResult::Unrelated;
    if (bytes.size() != WireSize) return ParseResult::Malformed;
    const auto u8 = [&](std::size_t at) { return std::to_integer<std::uint32_t>(bytes[at]); };
    Message decoded;
    decoded.target = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(u8(1) | (u8(2) << 8)));
    decoded.style = static_cast<std::uint8_t>(u8(3));
    decoded.flags = static_cast<std::uint8_t>(u8(4));
    decoded.value = std::bit_cast<std::int32_t>(u8(5) | (u8(6) << 8) | (u8(7) << 16) | (u8(8) << 24));
    // Preserve unknown style/flag bytes at the wire boundary. A presentation
    // consumer may reject unsupported styles without corrupting the message.
    message = decoded;
    return ParseResult::Number;
}

std::optional<ImageMetrics> MeasureDigits(unsigned imageWidth, unsigned imageHeight)
{
    // Bounded resource dimensions also prevent overflow in the power-of-two loop.
    if (imageWidth < 10 || imageWidth > 16384 || !imageHeight || imageHeight > 16384)
        return std::nullopt;
    unsigned textureWidth = 256;
    while (textureWidth < imageWidth) textureWidth *= 2;
    return ImageMetrics{float(imageWidth / 10) * float(imageWidth) / float(textureWidth),
        float(imageHeight)};
}

bool Numbers::Push(Message message, Anchor anchor, Animation animation, double receivedAt, int offsetX, int offsetY)
{
    if (message.target <= 0 || message.style > static_cast<std::uint8_t>(Style::Heal) ||
        !Finite(anchor) || !Valid(animation) || !std::isfinite(receivedAt)) return false;
    Number number;
    number.message = message;
    number.anchor = anchor;
    number.animation = animation;
    number.receivedAt = receivedAt;
    number.offsetX = offsetX;
    number.offsetY = offsetY;
    const auto written = std::to_chars(number.text.data(), number.text.data() + number.text.size() - 1,
        message.value);
    if (written.ec != std::errc{}) return false;
    number.textLength = static_cast<std::uint8_t>(written.ptr - number.text.data());

    // 0295B310 marks all previous panels superseded; the next render update
    // (0295C230/0295C730), not this message callback, records their skip age.
    // Append before changing old flags so allocation failure leaves them intact.
    entries_.push_back(number);
    for (std::size_t i = 0; i + 1 < entries_.size(); ++i) entries_[i].newest = false;
    return true;
}

void Numbers::Advance(double renderTime)
{
    if (!std::isfinite(renderTime)) return;
    // Host network clocks can make small corrections. Do not rewind an
    // already rendered animation; the session owner resets on map/reconnect.
    if (hasRenderTime_) renderTime = std::max(renderTime, renderTime_);
    renderTime_ = renderTime;
    hasRenderTime_ = true;
    std::size_t kept{};
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        auto& number = entries_[i];
        const double elapsed = std::max(0.0, renderTime - number.receivedAt);
        if (elapsed > number.animation.duration) continue;
        const float age = static_cast<float>(elapsed);
        number.age = age;
        if (!number.newest && !number.supersedeObserved)
        {
            number.supersedeObserved = true;
            number.supersededAtAge = age;
            number.skipped = age < number.animation.skipTime;
        }
        // Preserve the original add-then-subtract float operation order.
        number.effectiveAge = number.skipped ?
            age + number.animation.skipTime - number.supersededAtAge : age;
        if (number.effectiveAge > number.animation.duration) continue;
        if (kept != i) entries_[kept] = number;
        ++kept;
    }
    entries_.resize(kept); // Keep capacity; frame updates never grow storage.
}

void Numbers::Reset()
{
    entries_.clear();
    renderTime_ = 0;
    hasRenderTime_ = false;
}

std::optional<Appearance> Sample(const Number& number, const ImageStyle& style, Anchor camera)
{
    if (!Valid(number.animation) || !Finite(number.anchor) || !Finite(camera) ||
        !std::isfinite(number.effectiveAge) || number.effectiveAge < 0 ||
        !std::isfinite(style.image.glyphWidth) || style.image.glyphWidth <= 0 ||
        !std::isfinite(style.image.glyphHeight) || style.image.glyphHeight <= 0 ||
        !std::isfinite(style.startScale) || style.startScale < 0 ||
        !std::isfinite(style.endScale) || style.endScale < 0 ||
        !std::isfinite(style.distanceRatio) || style.distanceRatio < 0) return std::nullopt;
    const auto& animation = number.animation;
    const float age = number.effectiveAge;
    if (age > animation.duration) return std::nullopt;
    const float dx = number.anchor.x - camera.x, dy = number.anchor.y - camera.y,
        dz = number.anchor.z - camera.z;
    const float distance = float(std::sqrt(double(dx * dx + dy * dy + dz * dz)));
    const float penalty = distance * 0.025400051847100258f * style.distanceRatio;
    const float initial = style.image.glyphWidth * style.startScale;
    const float minimum = style.image.glyphWidth * style.endScale;
    const float width = std::max(minimum,
        std::max(animation.duration - age, 0.0f) / animation.duration * initial - penalty);
    const float height = width * (style.image.glyphHeight / style.image.glyphWidth);
    const float rise = age * animation.speed;
    // Bounds are new-host drawing safety, not a changed server damage range.
    if (!std::isfinite(penalty) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(rise) || width > 32767 || height > 32767) return std::nullopt;
    const float fadeSpan = animation.duration - animation.skipTime;
    float alpha = 255;
    if (fadeSpan < 0.001f) alpha = 0;
    else if (age > animation.skipTime)
        alpha -= std::max(0.0f, (age - animation.skipTime) / fadeSpan * 255.0f);
    return Appearance{static_cast<int>(width), static_cast<int>(height),
        static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 255.0f)), rise};
}
}
