// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Character voice selection for sound/sentences.json (J emotion voices).
// Voice ids/names: HW 37541310. Character model id -> voice: client 02A3E730.
#include <rapidjson/document.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace csol::voice
{
// Index = original voice id (0..69). ids 2/3/4/6/68 exist in the table but
// are not produced by the character mapping below.
inline constexpr std::array<std::string_view, 70> VoiceNames{
    "man", "woman", "zombie", "zombie_woman", "chn_woman", "jin", "cso2", "19s1man", "19s1woman", "19s2man",
    "19s2woman", "19s3tr", "19s3ct", "19s4tr", "19s4ct", "leechang", "yeongsin", "20s2tr", "20s2ct", "20s3tr",
    "20s3ct", "20s4tr", "20s4ct", "21s1tr", "21s1ct", "21s2tr", "21s2ct", "21s3tr", "21s3ct", "21s4tr",
    "21s4ct", "22s1tr", "22s1ct", "22s2tr", "22s2ct", "22s3tr", "22s3ct", "22s4tr", "22s4ct", "23s1tr",
    "23s1ct", "23s2tr", "23s2ct", "23s3tr", "23s3ct", "23s4tr", "23s4ct", "23s5tr", "24s1tr", "24s1ct",
    "24s2tr", "24s2ct", "24s3tr", "24s3ct", "24s4tr", "24s4ct", "25s1tr", "25s1ct", "25s2tr", "25s2ct",
    "25s3tr", "25s3ct", "25s4tr", "25s4ct", "25s5ct", "26s1tr", "26s2ct", "pass26s2tr", "ladynormal", "26s3ct"};

// 02A3E730: switch on model id-76 (0..0x5B); every other id uses the male
// flag (non-zero -> man 0, zero -> woman 1).
inline int CharacterVoiceId(int modelId, bool male)
{
    switch (modelId)
    {
    case 76: case 77: return 5;
    case 100: return 7;
    case 101: return 8;
    case 104: return 10;
    case 105: return 9;
    case 108: return 11;
    case 109: return 12;
    case 166: return 67;
    case 167: return 69;
    default:
        if (modelId >= 112 && modelId <= 165) return modelId - 99;
        return male ? 0 : 1;
    }
}
inline std::string_view CharacterVoiceKey(int modelId, bool male)
{ return VoiceNames[static_cast<std::size_t>(CharacterVoiceId(modelId, male))]; }

// HW 3753FBC0 is an explicit voice table, independent of the character's
// sex flag. 37541120 retries the same sentence only when its vector is empty.
inline int FallbackVoiceId(int voice)
{
    switch (voice)
    {
    case 4: case 5: case 6: case 8: case 10: case 11: case 12: case 13:
    case 17: case 21: case 22: case 24: case 25: case 26: case 31: case 32:
    case 34: case 35: case 37: case 38: case 43: case 45: case 46: case 47:
    case 49: case 50: case 51: case 54: case 55: case 56: case 57: case 58:
    case 60: case 61: case 62: case 63: case 64: case 66: case 69:
        return 1;
    case 7: case 9: case 14: case 15: case 16: case 18: case 23: case 33:
    case 36: case 44: case 48: case 59: case 67:
        return 0;
    case 68:
        return 3;
    default:
        return voice;
    }
}

// Studio event options are "%!mrad_emotion_hi"; table keys are upper case.
inline std::string SentenceKey(std::string_view options)
{
    if (options.starts_with('%')) options.remove_prefix(1);
    if (options.starts_with('!')) options.remove_prefix(1);
    std::string key(options);
    for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return key;
}

// A relative wav below sound/: no traversal, drive, or unexpected bytes.
inline bool ValidSoundPath(std::string_view path)
{
    if (path.empty() || path.size() > 150 || !path.ends_with(".wav") || path.front() == '/' ||
        path.find("..") != path.npos || path.find(':') != path.npos) return false;
    for (char c : path)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') &&
            c != '/' && c != '_' && c != '-' && c != '.') return false;
    return true;
}

// sentence key -> voice key -> candidate wavs (one is picked at random).
using SentenceTable = std::map<std::string, std::map<std::string, std::vector<std::string>, std::less<>>, std::less<>>;

// HW 3753FC50 expands a sentence's "buffdefault" templates for exactly these
// 36 names (static list built by 371ADE60) when that voice has no own entry.
inline constexpr std::string_view TemplateVoice = "buffdefault";
inline constexpr std::array<std::string_view, 36> TemplateVoices{
    "22s2tr", "22s2ct", "22s3tr", "22s3ct", "22s4tr", "22s4ct", "23s1tr", "23s1ct", "23s2tr", "23s2ct",
    "23s3tr", "23s3ct", "23s4tr", "23s4ct", "23s5tr", "24s1tr", "24s1ct", "24s2tr", "24s2ct", "24s3tr",
    "24s3ct", "24s4tr", "24s4ct", "25s1tr", "25s1ct", "25s2tr", "25s2ct", "25s3tr", "25s3ct", "25s4tr",
    "25s4ct", "25s5ct", "26s1tr", "26s2ct", "pass26s2tr", "26s3ct"};
// "/$buffclassname/" -> "/buffclass<voice>/"; empty when not a valid template.
inline std::string ExpandTemplate(std::string_view pattern, std::string_view voice)
{
    constexpr std::string_view marker = "/$buffclassname/";
    const auto at = pattern.find(marker);
    if (at == pattern.npos) return {};
    std::string path(pattern);
    path.replace(at, marker.size(), "/buffclass" + std::string(voice) + "/");
    return ValidSoundPath(path) ? path : std::string{};
}

// The original file has // comments in a legacy code page; only ASCII keys
// and paths are used. Invalid paths make the whole table invalid.
inline bool ReadSentences(std::string_view json, SentenceTable& output, std::string_view prefix = {})
{
    rapidjson::Document document;
    document.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject()) return false;
    SentenceTable table;
    for (auto sentence = document.MemberBegin(); sentence != document.MemberEnd(); ++sentence)
    {
        const auto name = SentenceKey({sentence->name.GetString(), sentence->name.GetStringLength()});
        if (!name.starts_with(prefix) || !sentence->value.IsObject()) continue;
        auto& voices = table[name];
        for (auto voice = sentence->value.MemberBegin(); voice != sentence->value.MemberEnd(); ++voice)
        {
            std::vector<std::string> paths;
            const bool pattern = std::string_view(voice->name.GetString(), voice->name.GetStringLength()) == TemplateVoice;
            const auto add = [&](const rapidjson::Value& value)
            {
                if (!value.IsString()) return false;
                std::string path(value.GetString(), value.GetStringLength());
                if (pattern ? ExpandTemplate(path, TemplateVoices.front()).empty() : !ValidSoundPath(path)) return false;
                paths.push_back(std::move(path));
                return true;
            };
            if (voice->value.IsArray())
            {
                for (const auto& value : voice->value.GetArray()) if (!add(value)) return false;
            }
            else if (!add(voice->value)) return false;
            voices[std::string(voice->name.GetString(), voice->name.GetStringLength())] = std::move(paths);
        }
    }
    output = std::move(table);
    return true;
}

// Template expansion after loading. exists(path) receives "sound/<path>",
// like the original file-system check; only existing files are added.
template <class Exists>
void ExpandTemplates(SentenceTable& table, Exists&& exists)
{
    for (auto& [name, voices] : table)
    {
        const auto patterns = voices.find(TemplateVoice);
        if (patterns == voices.end()) continue;
        const auto templates = patterns->second;
        for (const auto voice : TemplateVoices)
        {
            if (const auto own = voices.find(voice); own != voices.end() && !own->second.empty()) continue;
            std::vector<std::string> paths;
            for (const auto& pattern : templates)
                if (auto path = ExpandTemplate(pattern, voice); !path.empty() && exists("sound/" + path))
                    paths.push_back(std::move(path));
            if (!paths.empty()) voices[std::string(voice)] = std::move(paths);
        }
    }
}

inline const std::vector<std::string>* FindSentence(const SentenceTable& table, std::string_view sentence,
    std::string_view voice)
{
    const auto entry = table.find(sentence);
    if (entry == table.end()) return nullptr;
    const auto paths = entry->second.find(voice);
    if (paths != entry->second.end() && !paths->second.empty()) return &paths->second;
    const auto name = std::find(VoiceNames.begin(), VoiceNames.end(), voice);
    if (name == VoiceNames.end()) return nullptr;
    const int id = static_cast<int>(name - VoiceNames.begin());
    const int fallback = FallbackVoiceId(id);
    if (fallback == id) return nullptr;
    const auto generic = entry->second.find(VoiceNames[static_cast<std::size_t>(fallback)]);
    return generic == entry->second.end() || generic->second.empty() ? nullptr : &generic->second;
}
}
