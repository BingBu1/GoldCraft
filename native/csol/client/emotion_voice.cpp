// SPDX-License-Identifier: GPL-3.0-or-later
#include "emotion_voice.hpp"
#include "characters.hpp"
#include "../character_voice.hpp"
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace csol::client {
namespace {
constexpr char SentencesPath[] = "sound/sentences.json";
voice::SentenceResolver sentences;
std::string loadedLevel;
bool loaded{};

// The table arrives with the map's resources; read it once per level.
voice::SentenceResolver* Sentences()
{
    const char* level = engine.pfnGetLevelName ? engine.pfnGetLevelName() : nullptr;
    const std::string current = level ? level : "";
    if (loaded && current == loadedLevel) return &sentences;
    loaded = true; loadedLevel = current; sentences = voice::SentenceResolver{};
    try
    {
        const auto bytes = ReadResource(SentencesPath);
        voice::SentenceTable table;
        if (voice::ReadSentences({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, table, "MRAD_EMOTION"))
        {
            sentences = voice::SentenceResolver(std::move(table));
        }
        else engine.Con_Printf("CSOL emotion voice: invalid %s\n", SentencesPath);
    }
    catch (const std::exception& error) { engine.Con_Printf("CSOL emotion voice: %s\n", error.what()); }
    return &sentences;
}
}

void ResetEmotionVoices()
{
    sentences = voice::SentenceResolver{};
    loadedLevel.clear();
    loaded = false;
}

bool EmotionVoiceStudioEvent(const mstudioevent_t* event, const cl_entity_t* entity)
{
    if (!event || !entity || event->event != 5050) return false;
    // 02A3E730 reads the view player's model id; only v_emotion models
    // carry 5050, so the event belongs to that player's own viewmodel.
    const auto* character = ViewCharacter();
    auto* table = character ? Sentences() : nullptr;
    if (!table) return true;
    const std::string_view options(event->options, strnlen(event->options, sizeof(event->options)));
    const std::vector<std::string>* paths{};
    try
    {
        paths = table->Find(voice::SentenceKey(options),
            voice::CharacterVoiceKey(character->id, character->male),
            [](const std::string& path) { return Exists(path.c_str()); });
    }
    catch (const std::exception& error)
    {
        engine.Con_Printf("CSOL emotion voice: %s\n", error.what());
        return true;
    }
    // FindSentence includes the original per-voice fallback. A sentence
    // absent from both JSON and the shipped legacy table stays silent.
    if (!paths || !engine.pfnPlaySoundByName) return true;
    // The original consumes rand() even for a one-entry vector.
    const auto& path = (*paths)[static_cast<std::size_t>(std::rand()) % paths->size()];
    engine.pfnPlaySoundByName(const_cast<char*>(path.c_str()), 1.0f);
    return true;
}
}
