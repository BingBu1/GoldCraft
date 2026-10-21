// Verified animation evaluation carried from HLAM CSOLModelJSON.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_json.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace csol {
namespace {
using Value = rapidjson::Value;
const Value& Get(const Value& value, const char* key)
{
    static const Value missing;
    if (!value.IsObject()) return missing;
    const auto it = value.FindMember(key);
    return it == value.MemberEnd() ? missing : it->value;
}
double Number(const Value& object, const char* key, double fallback, double low, double high, bool integer = false)
{
    const auto& value = Get(object, key);
    if (!object.IsObject() || !object.HasMember(key)) return fallback;
    if (!value.IsNumber()) throw std::runtime_error("invalid JSON number");
    const auto n = value.GetDouble();
    if (!std::isfinite(n) || n < low || n > high || (integer && std::floor(n) != n))
        throw std::runtime_error("JSON number out of range");
    return n;
}
bool Boolean(const Value& object, const char* key)
{
    const auto& value = Get(object, key);
    if (!object.IsObject() || !object.HasMember(key)) return false;
    if (!value.IsBool()) throw std::runtime_error("invalid JSON boolean");
    return value.GetBool();
}
std::string Name(const Value& object, const char* key, bool path)
{
    const auto& value = Get(object, key);
    if (!value.IsString()) throw std::runtime_error("missing resource name");
    std::string name(value.GetString(), value.GetStringLength());
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.size() > (path ? 256u : 63u) || name.front() == '/' ||
        name.find(':') != name.npos || name.find('\0') != name.npos || name == "." || name == ".." ||
        name.starts_with("../") || name.ends_with("/..") || name.find("/../") != name.npos ||
        (!path && name.find('/') != name.npos)) throw std::runtime_error("invalid resource path");
    return name;
}
const Value& Array(const Value& value, unsigned limit)
{
    if (!value.IsArray() || value.Size() > limit) throw std::runtime_error("invalid JSON array");
    return value;
}
FadeRule ReadFade(const Value& object, FadeRule fallback = {})
{
    fallback.Offset = int(Number(object, "offsetFrame", fallback.Offset, -1000000, 1000000, true));
    fallback.In = float(Number(object, "fadeIn", fallback.In, 0, 1000000));
    fallback.Out = float(Number(object, "fadeOut", fallback.Out, 0, 1000000));
    fallback.Hold = float(Number(object, "holdTime", fallback.Hold, -1000000, 1000000));
    fallback.Minimum = float(Number(object, "alphaMin", fallback.Minimum, -1000000, 1000000));
    fallback.Maximum = float(Number(object, "alphaMax", fallback.Maximum, -1000000, 1000000));
    if (object.HasMember("isFadeInOnce")) fallback.Once = Boolean(object, "isFadeInOnce");
    return fallback;
}
}
ModelJSON ParseModelJSON(std::string_view data, int textures, int sequences)
{
    if (data.size() > 4 * 1024 * 1024 || textures < 0 || textures > 4096 || sequences < 0 || sequences > 4096)
        throw std::runtime_error("model JSON exceeds limits");
    rapidjson::Document doc;
    doc.Parse(data.data(), data.size());
    if (doc.HasParseError() || !doc.IsObject()) throw std::runtime_error("invalid model JSON");
    ModelJSON result;
    const auto index = [&](const Value& entry, const char* field, int limit) {
        if (!entry.IsObject()) throw std::runtime_error("JSON entry must be an object");
        const int n = int(Number(entry, field, -1, -1, 1000000, true));
        return n >= 0 && n < limit ? n : -1;
    };
    const auto& bindTex = Get(doc, "bindTex");
    if (!bindTex.IsNull()) for (const auto& entry : Array(bindTex, 256).GetArray())
    {
        const int slot = index(entry, "texIndex", textures);
        if (slot < 0) continue;
        auto name = Name(entry, "texName", false);
        if (std::none_of(result.Textures.begin(), result.Textures.end(), [&](const auto& b){ return b.Texture == slot; }))
            result.Textures.push_back({slot, std::move(name)});
    }
    const auto& fades = Get(doc, "fadeTex");
    if (!fades.IsNull()) for (const auto& entry : Array(fades, 256).GetArray())
    {
        FadeBinding binding;
        binding.Texture = index(entry, "texIndex", textures);
        if (binding.Texture < 0) continue;
        binding.Default = ReadFade(entry);
        binding.Rules.resize(sequences);
        const auto& rules = Get(entry, "sequence");
        if (!rules.IsNull())
        {
            binding.Restricted = !Array(rules, 4096).Empty();
            for (const auto& row : rules.GetArray())
            {
                const int seq = index(row, "index", sequences);
                if (seq < 0) continue;
                auto rule = ReadFade(row, binding.Default); rule.Sequence = seq;
                if (binding.Rules[seq].Sequence < 0) binding.Rules[seq] = rule;
            }
        }
        if (std::none_of(result.Fades.begin(), result.Fades.end(), [&](const auto& b){return b.Texture == binding.Texture;}))
            result.Fades.push_back(std::move(binding));
    }
    const auto& sprites = Get(doc, "bindSpr");
    if (!sprites.IsNull()) for (const auto& entry : Array(sprites, 256).GetArray())
    {
        SpriteBinding binding;
        binding.Texture = index(entry, "texIndex", textures);
        if (binding.Texture < 0) continue;
        binding.Sprite = Name(entry, "sprName", true);
        auto extension = binding.Sprite.substr(binding.Sprite.size() > 4 ? binding.Sprite.size() - 4 : 0);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return std::tolower(c); });
        if (extension != ".spr") throw std::runtime_error("bindSpr requires a sprite");
        binding.FPS = Number(entry, "sprFPS", 30, 0, 1000);
        binding.Rules.resize(sequences);
        for (const auto& row : Array(Get(entry, "sequence"), 4096).GetArray())
        {
            const int seq = index(row, "index", sequences);
            if (seq < 0) continue;
            SpriteRule rule; rule.Sequence = seq;
            rule.Loop = Boolean(row, "isLoop"); rule.Sync = Boolean(row, "isSyncFrame");
            rule.Offset = int(Number(row, "offsetFrame", 0, -1000000, 1000000, true));
            rule.Start = int(Number(row, "startFrame", 0, -1000000, 1000000, true));
            rule.End = int(Number(row, "endFrame", 0, -1000000, 1000000, true));
            rule.Rate = Number(row, "frameRate", 1, 0, 1000);
            if (binding.Rules[seq].Sequence < 0) binding.Rules[seq] = rule;
        }
        if (std::none_of(result.Sprites.begin(), result.Sprites.end(), [&](const auto& b){return b.Texture == binding.Texture;}))
            result.Sprites.push_back(std::move(binding));
    }
    return result;
}
void FadeState::BeginFrame()
{
	if (!PreviousActive) Completed = false;
	PreviousActive = CurrentActive;
	CurrentActive = false;
}

void FadeState::Observe(bool markCompleted)
{
	CurrentActive = true;
	if (markCompleted && PreviousActive) Completed = true;
}

const FadeRule* FadeBinding::Rule(int sequence) const
{
	if (!Restricted) return &Default;
	return sequence >= 0 && sequence < int(Rules.size()) && Rules[sequence].Sequence >= 0 ? &Rules[sequence] : nullptr;
}

FadeResult EvaluateFade(const FadeRule& rule, float frame, float fps, int frames, float rate, bool completed)
{
	FadeResult result;
	if (!std::isfinite(frame) || !std::isfinite(fps) || !std::isfinite(rate)) return result;
	constexpr float epsilon = std::numeric_limits<float>::epsilon();
	if (fps < epsilon) fps = 30;
	if (rate < epsilon) rate = 1;
	const float time = std::max(0.f, frame - float(rule.Offset)) / (rate * fps);
	float fadeIn = rule.In, fadeOut = rule.Out, factor = 1;
	if (rule.Hold < 0)
	{
		if (frames <= 0) return result;
		const float duration = std::max(0.f, float(frames) / fps - float(rule.Offset) / fps);
		const float totalFade = fadeIn + fadeOut;
		if (totalFade > duration && duration > epsilon)
		{
			const float scale = duration / totalFade;
			fadeIn *= scale; fadeOut *= scale;
		}
		const float fadeOutStart = duration - fadeOut;
		if (fadeIn > epsilon && time < fadeIn)
			factor = rule.Once && completed ? 1 : time / fadeIn;
		else
		{
			result.MarkCompleted = true;
			if (time >= fadeOutStart && fadeOut > epsilon) factor = 1 - (time - fadeOutStart) / fadeOut;
		}
	}
	else
	{
		const float fadeOutStart = rule.Hold + fadeIn;
		const float end = fadeOutStart + fadeOut;
		if (end < epsilon) return result;
		if (fadeIn > epsilon && time < fadeIn)
			factor = rule.Once && completed ? 1 : time / fadeIn;
		else if (time < fadeOutStart) result.MarkCompleted = true;
		else factor = fadeOut > epsilon && time < end ? 1 - (time - fadeIn - rule.Hold) / fadeOut : 0;
	}
	result.Alpha = std::max(0.01f, std::clamp((rule.Maximum - rule.Minimum) * factor + rule.Minimum, 0.f, 1.f));
	result.Active = true;
	return result;
}

double AdvanceSpriteFrame(const SpriteRule& rule, double fps, int frames,
	double previous, double seconds, double modelFrame)
{
	if (frames <= 1 || !std::isfinite(previous) || !std::isfinite(seconds) || !std::isfinite(modelFrame)) return 0;
	const double start = std::max(0, rule.Start);
	const double end = rule.End > 0 ? rule.End : frames - 1;
	double frame = rule.Sync ? start + std::max(0.0, modelFrame - rule.Offset)
		: std::max(start, previous + std::max(0.0, seconds) * fps * rule.Rate);
	if (frame >= end)
		frame = rule.Sync && !rule.Loop ? end : std::fmod(std::trunc(frame), end) + start;
	return frame;
}

int SpriteFrameIndex(double frame, int frames)
{
	// R_GetSpriteFrame falls back to frame zero for an out-of-range result.
	return std::isfinite(frame) && frame >= 0 && frame < frames ? int(frame) : 0;
}
}
