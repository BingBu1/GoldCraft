// SPDX-License-Identifier: GPL-3.0-or-later
// J emotion table/menu checks on the original common emotion.csv and
// titles.txt. Expected tuples were derived independently from the CSV.
#include "../../native/csol/server/scenario_character_skills/host_actions.hpp"
#include "../../native/csol/character_voice.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>

namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", label); } }
std::string Read(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
using namespace csol::scenario::skills;
bool Slots(const CharacterAction5& row, std::initializer_list<std::tuple<int, int, int>> expected)
{
    int slot = 1;
    for (const auto& [sequence, weapon, rate] : expected)
    {
        const auto& entry = row.emotions[slot++];
        if (entry.sequence != sequence || entry.weaponAnimation != weapon || entry.frameRate != static_cast<float>(rate))
            return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc != 4) { std::printf("usage: emotion_tests emotion.csv titles.txt sentences.json\n"); return 2; }
    const auto csv = Read(argv[1]);
    const auto titles = Read(argv[2]);
    CharacterActionTable table;
    csol::scenario::ConfigError error;
    Check(ReadCharacterActions(csv, table, error), "real common emotion table parses");
    if (!error.message.empty()) std::printf("parser: %s at %zu\n", error.message.c_str(), error.byteOffset);
    Check(table.size() == 160, "all160 original rows loaded");

    // action5 stays exactly as before (skill regression).
    Check(table.contains(101) && table.at(101).sequence == 204 && table.at(101).weaponAnimation == 30 &&
        table.at(101).frameRate == 142, "type1 action5 tuple unchanged");
    Check(table.contains(139) && table.at(139).sequence == 202 && table.at(139).weaponAnimation == 169 &&
        table.at(139).frameRate == 128, "type29 action5 tuple unchanged");

    // menuselect 1140EFA5: wa/seq by slot, rate by the seq value.
    Check(table.contains(1) && table.at(1).emotionCount == 4 && table.at(1).emotionMenu == "#CSO_EmotionA" &&
        Slots(table.at(1), {{198, 0, 80}, {199, 4, 57}, {201, 2, 36}, {200, 6, 68}}), "urban four emotions");
    Check(table.contains(47) && table.at(47).emotionCount == 6 && table.at(47).emotionMenu == "#CSO_EmotionB" &&
        Slots(table.at(47), {{198, 0, 80}, {199, 4, 57}, {201, 2, 36}, {200, 6, 68}, {203, 12, 42}, {202, 8, 38}}),
        "marineboy six emotions including dance");
    Check(table.contains(101) && table.at(101).emotionMenu == "#CSO_EmotionJ" &&
        Slots(table.at(101), {{198, 1, 128}, {199, 5, 54}, {201, 3, 48}, {200, 7, 64}}),
        "female buffclass uses its own wa and frame columns");
    bool allValid = true;
    for (const auto& [id, row] : table)
        for (int slot = 1; slot <= row.emotionCount; ++slot)
        {
            const auto& entry = row.emotions[slot];
            allValid &= entry.sequence >= 198 && entry.sequence <= 207 && entry.frameRate > 0 &&
                entry.weaponAnimation >= 0 && entry.weaponAnimation <= 255;
        }
    Check(allValid, "every offered slot of every row is a valid emotion action");

    // cheer 114106E8: pow(2,count)-1 plus key0 (512).
    Check(EmotionMenuKeys(0) == 512 && EmotionMenuKeys(4) == 527 && EmotionMenuKeys(5) == 543 &&
        EmotionMenuKeys(6) == 575 && EmotionMenuKeys(10) == 1535, "original menu key mask");

    std::string body;
    Check(ReadTitleMessage(titles, "CSO_EmotionA", body) && body.starts_with("\\r") &&
        body.find("\n1. \"") != std::string::npos && body.ends_with("0. \xE5\x8F\x96\xE6\xB6\x88"),
        "CSO_EmotionA body keeps color codes, items and cancel line");
    std::string aa;
    Check(ReadTitleMessage(titles, "CSO_EmotionAA", aa) && aa != body, "AA is an exact key, not a prefix of A");
    bool menus = true;
    for (const auto& [id, row] : table)
    {
        std::string text;
        if (row.emotionCount) menus &= ReadTitleMessage(titles, std::string_view(row.emotionMenu).substr(1), text) && !text.empty();
    }
    Check(menus, "every referenced radio_text menu exists in CSO titles");
    Check(!ReadTitleMessage(titles, "CSO_EmotionZZZ", body), "missing key rejected");
    Check(!ReadTitleMessage("CSO_X\r\n{\r\nline\r\n", "CSO_X", body), "unterminated body rejected");
    Check(ReadTitleMessage("// note\nCSO_X\n\n {\n first\n}\nCSO_Y\n{\nother\n}\n", "CSO_X", body) && body == " first",
        "blank line and indented brace between name and body accepted; body kept raw");
    Check(ReadTitleMessage("CSO_X\n{\na\n}\nCSO_Y\n{\nother\n}\n", "CSO_Y", body) && body == "other",
        "later message found after skipping an earlier body");
    Check(ReadTitleMessage("\xEF\xBB\xBF" "CSO_X\r\n{\r\na\r\n\r\nb\r\n}\r\n", "CSO_X", body) && body == "a\n\nb",
        "BOM and CRLF stripped, blank line kept");

    // Invalid tables never replace the active one.
    const auto header = csv.substr(0, csv.find('\n') + 1);
    const auto before = table.size();
    std::string bad = header + "1,urban,#CSO_EmotionA,11,0,4,2,6,0,,,,,,1,2,4,3,1,,,,,,96,134,113,211,96,183,2,2,2,2,0\n";
    Check(!ReadCharacterActions(bad, table, error) && table.size() == before, "count above10 rejected");
    bad = header + "1,urban,#CSO_EmotionA,4,0,4,2,6,0,,,,,,1,0,4,3,1,,,,,,96,134,113,211,96,183,2,2,2,2,0\n";
    Check(!ReadCharacterActions(bad, table, error) && table.size() == before, "offered slot seq0 rejected");
    bad = header + "1,urban,CSO_EmotionA,4,0,4,2,6,0,,,,,,1,2,4,3,1,,,,,,96,134,113,211,96,183,2,2,2,2,0\n";
    Check(!ReadCharacterActions(bad, table, error) && table.size() == before, "menu key without # rejected");
    CharacterActionTable single;
    // frame04 is read only by slot3 (seq4); action5 (seq05=1) uses frame01.
    const std::string empty = header + "1,urban,#CSO_EmotionA,4,0,4,2,6,0,,,,,,1,2,4,3,1,,,,,,96,134,113,,96,183,2,2,2,2,0\n";
    Check(ReadCharacterActions(empty, single, error) && single.at(1).emotions[3].frameRate == 1.0f &&
        single.at(1).emotions[1].frameRate == 80.0f, "empty frame column is atoi 0 and keeps rate1");

    // Character voice: client 02A3E730 and HW 37541310.
    using namespace csol::voice;
    Check(CharacterVoiceKey(76, true) == "jin" && CharacterVoiceKey(77, false) == "jin", "jin characters");
    Check(CharacterVoiceKey(100, true) == "19s1man" && CharacterVoiceKey(101, false) == "19s1woman" &&
        CharacterVoiceKey(104, false) == "19s2woman" && CharacterVoiceKey(105, true) == "19s2man", "season19 swapped ids");
    Check(CharacterVoiceKey(108, true) == "19s3tr" && CharacterVoiceKey(109, false) == "19s3ct" &&
        CharacterVoiceKey(112, true) == "19s4tr" && CharacterVoiceKey(114, true) == "leechang" &&
        CharacterVoiceKey(165, false) == "26s2ct", "112..165 map to voice id minus 99");
    Check(CharacterVoiceKey(166, true) == "pass26s2tr" && CharacterVoiceKey(167, false) == "26s3ct", "166/167 skip ladynormal");
    Check(CharacterVoiceKey(1, true) == "man" && CharacterVoiceKey(1, false) == "woman" &&
        CharacterVoiceKey(102, false) == "woman" && CharacterVoiceKey(110, true) == "man" &&
        CharacterVoiceKey(168, true) == "man", "other ids follow the male flag");
    Check(SentenceKey("%!mrad_emotion_Disappointment") == "MRAD_EMOTION_DISAPPOINTMENT" &&
        SentenceKey("!MRAD_X") == "MRAD_X", "event option normalization");
    const auto json = Read(argv[3]);
    SentenceTable sentences;
    Check(ReadSentences(json, sentences, "MRAD_EMOTION"), "real sentences.json parses with legacy comments");
    Check(FindSentence(sentences, "MRAD_EMOTION_HI", "buffdefault") && !sentences.at("MRAD_EMOTION_HI").contains("22s2tr"),
        "templates are kept unexpanded until file checks");
    const auto common = std::filesystem::path(argv[3]).parent_path().parent_path();
    ExpandTemplates(sentences, [&](const std::string& path) { return std::filesystem::exists(common / path); });
    const auto* expanded = FindSentence(sentences, "MRAD_EMOTION_HI", "22s2tr");
    Check(expanded && !expanded->empty() && expanded->front().starts_with("vox/buffclass22s2tr/"),
        "listed voice without own entry gets the existing expanded files");
    Check(ExpandTemplate("vox/$buffclassname/emotion/hi1.wav", "26s3ct") == "vox/buffclass26s3ct/emotion/hi1.wav" &&
        ExpandTemplate("vox/emotion/hi1.wav", "26s3ct").empty(), "template marker replacement");
    const auto* hi = FindSentence(sentences, "MRAD_EMOTION_HI", "man");
    Check(hi && hi->size() == 1 && hi->front() == "weapons/man_hi.wav", "man hello");
    const auto* hi2 = FindSentence(sentences, "MRAD_EMOTION_HI", "19s2man");
    Check(hi2 && hi2->size() == 2, "array voice keeps both random candidates");
    Check(!FindSentence(sentences, "MRAD_EMOTION_E", "man") && !FindSentence(sentences, "MRAD_EMOTION_SKILL", "man"),
        "sentences missing from the original table stay silent");
    Check(!FindSentence(sentences, "MRAD_AFFIRM", "man"), "prefix keeps only emotion sentences");
    // Independent cross-check: a buffclass row's mapped voice points at its
    // own vox folder in at least one emotion sentence.
    int crossed{}, mismatched{};
    std::istringstream rows(csv);
    std::string line;
    std::getline(rows, line);
    while (std::getline(rows, line))
    {
        const auto first = line.find(','), second = line.find(',', first + 1);
        if (first == line.npos || second == line.npos) continue;
        const int id = std::atoi(line.substr(0, first).c_str());
        const auto name = line.substr(first + 1, second - first - 1);
        const auto key = CharacterVoiceKey(id, true);
        if (!name.starts_with("buffclass") || key == "man") continue;
        bool found{};
        for (const auto& [sentence, voices] : sentences)
            if (const auto paths = voices.find(key); paths != voices.end())
                for (const auto& path : paths->second)
                {
                    std::string lower = path;
                    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    found |= lower.find("/" + name + "/") != lower.npos;
                }
        ++crossed;
        if (!found) { ++mismatched; std::printf("voice folder mismatch: id %d %s -> %s\n", id, name.c_str(), std::string(key).c_str()); }
    }
    std::printf("cross-checked %d buffclass voices\n", crossed);
    Check(crossed > 50 && mismatched == 0, "every mapped buffclass voice uses that character's vox folder");
    SentenceTable kept = sentences;
    Check(!ReadSentences("{\"MRAD_EMOTION_HI\":{\"man\":\"../x.wav\"}}", kept, "MRAD_EMOTION") &&
        kept.size() == sentences.size(), "traversal path rejects table");

    std::printf("character emotion: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
