// SPDX-License-Identifier: GPL-3.0-or-later
#include "damage_styles.hpp"
#include "../config_codec.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace csol::damage
{
namespace
{
using Value = rapidjson::Value;
const Value& Field(const Value& object, const char* name)
{
    if (!object.IsObject() || !object.HasMember(name)) throw std::runtime_error("missing damage style field");
    return object[name];
}
float Scalar(const Value& object, const char* name, float fallback = 0)
{
    if (!object.HasMember(name)) return fallback;
    const auto& v = object[name];
    if (!v.IsNumber() || !std::isfinite(v.GetDouble()) || std::abs(v.GetDouble()) > 32767)
        throw std::runtime_error("invalid damage style scalar");
    return v.GetFloat();
}
std::string Text(const Value& object, const char* name)
{
    const auto& v = Field(object, name);
    if (!v.IsString()) throw std::runtime_error("invalid damage style string");
    return {v.GetString(), v.GetStringLength()};
}
unsigned Enum(const Value& names, const std::string& name)
{
    const auto& v = Field(names, name.c_str());
    if (!v.IsUint()) throw std::runtime_error("invalid damage style enum");
    return v.GetUint();
}
std::string Resource(const Value& object, const char* field)
{
    auto path = Text(object, field);
    if (!path.starts_with("resource/FloatingDamage/") || path.find("..") != path.npos ||
        path.find_first_of("\\:\0", 0, 3) != path.npos)
        throw std::runtime_error("invalid damage style resource");
    return path + ".tga";
}
}
const Part* Skin::Find(Style style) const
{
    if (style == Style::Dot) style = Style::Normal;
    const auto it = std::find_if(styles.begin(), styles.end(), [=](const Part& p) { return p.type == unsigned(style); });
    return it == styles.end() ? nullptr : &*it;
}
std::vector<Skin> ReadSkins(std::span<const std::byte> resource)
{
    if (resource.empty() || resource.size() > 2 * 1024 * 1024) throw std::runtime_error("invalid damage style size");
    std::string plain(reinterpret_cast<const char*>(resource.data()), resource.size());
    const auto first = plain.find_first_not_of(" \r\n\t");
    if (first == plain.npos || plain[first] != '{')
        if (!DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size()))))
            throw std::runtime_error("truncated damage configuration");
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseCommentsFlag>(plain.data(), plain.size());
    if (doc.HasParseError()) throw std::runtime_error("invalid damage style JSON");
    const auto& root = Field(doc, "FLOATING_DMG_STYLE");
    const auto& definitions = Field(root, "STYLE_SET");
    if (!definitions.IsArray() || definitions.Size() > 256) throw std::runtime_error("invalid damage skins");
    std::vector<Skin> result;
    for (const auto& definition : definitions.GetArray())
    {
        Skin skin;
        skin.id = int(Enum(Field(root, "SKIN_TYPE"), Text(definition, "id")));
        if (skin.id <= 0 || std::any_of(result.begin(), result.end(), [&](const Skin& s) { return s.id == skin.id; }))
            throw std::runtime_error("duplicate damage skin");
        skin.itemId = int(Scalar(definition, "ItemID"));
        skin.preview = Resource(definition, "SkinImage");
        skin.randomWidth = int(Scalar(definition, "randomPosWidth"));
        skin.randomHeight = int(Scalar(definition, "randomPosHeight"));
        skin.animation = {Scalar(definition, "AnimDuration"), Scalar(definition, "AnimSkipTime"), Scalar(definition, "FloatingSpeed")};
        if (skin.animation.duration <= 0 || skin.animation.skipTime < 0 || skin.animation.skipTime > skin.animation.duration ||
            skin.randomWidth < 0 || skin.randomHeight < 0) throw std::runtime_error("invalid damage animation");
        for (const bool icon : {false, true})
        {
            const auto& parts = Field(definition, icon ? "SubStyles" : "Styles");
            if (!parts.IsArray() || parts.Size() > 16) throw std::runtime_error("invalid damage parts");
            auto& target = icon ? skin.icons : skin.styles;
            for (const auto& row : parts.GetArray())
            {
                Part p;
                p.type = Enum(Field(root, icon ? "STYLE_SUB_TYPE" : "STYLE_TYPE"), Text(row, "type"));
                if (std::any_of(target.begin(), target.end(), [&](const Part& existing) { return existing.type == p.type; }))
                    throw std::runtime_error("duplicate damage part");
                p.resource = Resource(row, "resource");
                p.width = Scalar(row, "ContentsWidth"); p.height = Scalar(row, "ContentsHeight");
                p.start = Scalar(row, "ContentsScaleStart"); p.end = Scalar(row, "ContentsScaleEnd");
                p.distance = Scalar(row, "ContentsScaleDistRatio"); p.interval = Scalar(row, "ContentsInterval");
                p.x = int(Scalar(row, "OffsetPosX")); p.y = int(Scalar(row, "OffsetPosY"));
                // Later atlases omit ContentsWidth/Height; the original digit
                // widget measures its image instead. Icons need explicit size.
                if (p.width < 0 || p.height < 0 || (icon && (p.width == 0 || p.height == 0)) ||
                    p.start < 0 || p.end < 0 || p.distance < 0)
                    throw std::runtime_error("invalid damage dimensions");
                if (icon)
                {
                    p.horizontal = int(Enum(Field(root, "STYLE_ANCHOR_HORIZONTAL_TYPE"), Text(row, "ContentsAnchorH")));
                    p.vertical = int(Enum(Field(root, "STYLE_ANCHOR_VERTICAL_TYPE"), Text(row, "ContentsAnchorV")));
                    const auto& relative = Field(row, "ContentsScaleRelative");
                    if (!relative.IsBool()) throw std::runtime_error("invalid damage icon scale");
                    p.relative = relative.GetBool();
                }
                target.push_back(std::move(p));
            }
        }
        result.push_back(std::move(skin));
    }
    std::sort(result.begin(), result.end(), [](const Skin& a, const Skin& b) { return a.id < b.id; });
    return result;
}

std::size_t LayoutDigits(const Number& number, const Part& part, TextureMetrics image,
    Anchor camera, float x, float y, std::span<Quad> output)
{
    if (!image.width || !image.height || image.paddedWidth < image.width || image.paddedHeight < image.height ||
        !std::isfinite(x) || !std::isfinite(y) || number.textLength > output.size()) return 0;
    const auto metrics = MeasureDigits(image.width, image.height);
    if (!metrics) return 0;
    const auto sampled = Sample(number, {*metrics, part.start, part.end, part.distance}, camera);
    if (!sampled) return 0;
    const float glyph = metrics->glyphWidth - 1.0f;
    if (glyph <= 0) return 0;
    // Verified constructor 0295CD80: glyph source width = measuredWidth-1,
    // image-number screen stride = width + ContentsInterval (integer steps).
    const float width = float(sampled->width), height = float(sampled->height);
    const int count = number.textLength;
    const int total = int(count * width + (count - 1) * part.interval);
    int left = int(float(int(x)) - float(total) * .5f + float(part.x + number.offsetX));
    const int top = int(float(int(y)) - height * .5f + float(part.y + number.offsetY) - sampled->rise);
    std::size_t used{};
    for (const char digit : number.Text())
    {
        // 02C92635 / numeric setter: minus uses the eleventh UV cell.
        const int cell = digit == '-' ? 10 : digit - '0';
        if (cell < 0 || cell > 10) return 0;
        const float u0 = cell * (glyph + 1) / image.width;
        const float u1 = (cell * (glyph + 1) + glyph) / image.width;
        output[used++] = {float(left), float(top), width, height, u0, 0, u1, 1, sampled->alpha};
        left = int(float(left) + width + part.interval);
    }
    return used;
}

std::optional<Quad> LayoutMiss(const Number& n, const Part& p, TextureMetrics image,
    Anchor camera, float x, float y)
{
    const auto a = Sample(n, {{float(image.width), float(image.height)}, p.start, p.end, p.distance}, camera);
    if (!a || !image.paddedWidth || !image.paddedHeight) return std::nullopt;
    return Quad{float(int(int(x) - a->width * .5f + p.x + n.offsetX)),
        float(int(int(y) - a->height * .5f + p.y + n.offsetY - a->rise)),
        float(a->width), float(a->height), 0, 0,
        float(image.width) / image.paddedWidth, float(image.height) / image.paddedHeight, a->alpha};
}

std::optional<Quad> LayoutIcon(const Number& n, const Part& icon, const Part& digits,
    Anchor camera, std::span<const Quad> bounds)
{
    if (bounds.empty() || icon.start <= 0 || icon.width <= 0 || icon.height <= 0) return std::nullopt;
    // Original 0295BAE0: parent remaining-time fraction and distance penalty;
    // icon alpha uses real age, while the digit animation may have skipped.
    const float remaining = std::max(0.0f, (n.animation.duration - n.age) / n.animation.duration);
    const float parentScale = std::max(digits.start > 0 ? digits.end / digits.start : 0, remaining);
    const float scale = std::max(icon.end / icon.start, parentScale);
    const float dx = n.anchor.x - camera.x, dy = n.anchor.y - camera.y, dz = n.anchor.z - camera.z;
    const float penalty = float(std::sqrt(double(dx*dx + dy*dy + dz*dz))) * .025400051847100258f * digits.distance;
    float width = icon.width * icon.start * scale - penalty;
    float height = icon.height * icon.start * scale - penalty;
    float ox = icon.x * scale + penalty, oy = icon.y * scale + penalty;
    const float minimum = icon.width * icon.end;
    if (width < minimum) { ox -= minimum - width; width = minimum; }
    const float minimumHeight = width * icon.height / icon.width;
    if (height < minimumHeight) { oy -= minimumHeight - height; height = minimumHeight; }
    const auto& first = bounds.front(); const auto& last = bounds.back();
    // Original GetBounds walks count strides, including the last interval.
    const float right = last.x + last.width + digits.interval;
    const auto anchored = [](int anchor, float low, float high, float size, float offset) {
        if (anchor == 1) return low + (high - low - size) * .5f + offset;
        if (anchor == 2) return high - size + offset;
        return low + size * .5f + offset;
    };
    const float x = anchored(icon.horizontal, first.x, right, width, ox);
    const float y = anchored(icon.vertical, first.y, first.y + first.height, height, oy);
    if (!std::isfinite(x) || !std::isfinite(y) || width > 32767 || height > 32767) return std::nullopt;
    return Quad{float(int(x)), float(int(y)), float(int(width)), float(int(height)), 0, 0, 1, 1,
        std::uint8_t(std::clamp(remaining * 255, 0.0f, 255.0f))};
}
}
