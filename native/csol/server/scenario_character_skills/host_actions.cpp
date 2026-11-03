// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_actions.hpp"
#include <array>
#include <charconv>
#include <cfloat>
#include <cmath>

namespace csol::scenario::skills
{
bool ReadCharacterActions(std::string_view csv, CharacterActionTable& output, ConfigError& error)
{
    CsvDocument document;
    if (!ReadCsv(csv, document, error)) return false;
    if (document.records.empty())
    { error.message = "character emotion table is empty"; return false; }
    const auto& header = document.records.front().fields;
    if (header.size() != 35 || header[0] != "model_id" || header[8] != "wa05" ||
        header[18] != "seq05" || header[24] != "frame01" || header[34] != "vmodelIndex")
    { error.message = "character emotion table columns do not match the original layout"; return false; }
    CharacterActionTable candidate;
    for (std::size_t i = 1; i < document.records.size(); ++i)
    {
        const auto& row = document.records[i];
        error.byteOffset = row.byteOffset;
        if (row.fields.size() == 1 && row.fields.front().empty()) continue;
        if (row.fields.size() != 35)
        { error.message = "character emotion row has an invalid column count"; return false; }
        const auto number = [&](unsigned column, int& value)
        {
            const auto& field = row.fields[column];
            const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
            return result.ec == std::errc{} && result.ptr == field.data() + field.size();
        };
        CharacterAction5 action;
        action.sourceName = row.fields[1];
        int sequence{}, frames{};
        if (!number(0, action.modelId) || !number(8, action.weaponAnimation) ||
            !number(18, sequence) || !number(34, action.viewModel) ||
            action.modelId < 0 || sequence < 1 || sequence > 10 ||
            action.weaponAnimation < 0 || action.weaponAnimation > 255 ||
            action.viewModel < 0 || action.viewModel > 1 ||
            !number(23 + static_cast<unsigned>(sequence), frames) || frames < 0)
        { error.message = "character action5 contains an invalid index or frame count"; return false; }
        action.sequence = 197 + sequence;
        // HW379F8FA9 divides integers before converting the result to float.
        action.frameRate = frames > 2 ? static_cast<float>(7680 / (frames - 1)) : 1.0f;
        // J menu rows: HW reads every frame column with atoi, so an empty
        // frame is 0 and keeps rate 1. Offered slots need real wa/seq values.
        action.emotionMenu = row.fields[2];
        if (!number(3, action.emotionCount) || action.emotionCount < 0 || action.emotionCount > 10 ||
            (action.emotionCount && (action.emotionMenu.size() < 2 || action.emotionMenu.front() != '#')))
        { error.message = "character emotion menu count or key is invalid"; return false; }
        for (int slot = 1; slot <= action.emotionCount; ++slot)
        {
            int weapon{}, seq{}, slotFrames{};
            if (!number(3 + slot, weapon) || !number(13 + slot, seq) || weapon < 0 || weapon > 255 ||
                seq < 1 || seq > 10)
            { error.message = "character emotion slot contains an invalid index"; return false; }
            const auto& frameField = row.fields[23 + seq];
            if (!frameField.empty() && (!number(23 + seq, slotFrames) || slotFrames < 0))
            { error.message = "character emotion slot contains an invalid frame count"; return false; }
            action.emotions[slot] = {197 + seq, weapon,
                slotFrames > 2 ? static_cast<float>(7680 / (slotFrames - 1)) : 1.0f};
        }
        if (action.frameRate <= 0 || !candidate.emplace(action.modelId, action).second)
        { error.message = "character action5 has zero frame rate or a duplicate model id"; return false; }
    }
    output = std::move(candidate);
    error = {};
    return true;
}

int EmotionMenuKeys(int count)
{
    // Same double operations as 114106E8..11410715; exact for 0..10.
    return static_cast<int>(std::pow(2.0, static_cast<double>(count)) - 1.0 + 512.0);
}

bool ReadTitleMessage(std::string_view titles, std::string_view name, std::string& body)
{
    // GoldSrc client message files: outside a body, trimmed blank, "//" and
    // "$" directive lines are skipped and the last other line names the
    // next "{" body. Body lines are kept raw until a line starting with "}".
    if (titles.starts_with("\xEF\xBB\xBF")) titles.remove_prefix(3);
    const auto trim = [](std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
        return text;
    };
    std::string_view current;
    std::string result;
    bool inText{}, matched{}, first{true};
    for (std::size_t at = 0; at < titles.size();)
    {
        auto end = titles.find('\n', at);
        if (end == std::string_view::npos) end = titles.size();
        auto text = titles.substr(at, end - at);
        at = end + 1;
        if (text.ends_with('\r')) text.remove_suffix(1);
        const auto trimmed = trim(text);
        if (!inText)
        {
            if (trimmed.empty() || trimmed.starts_with("//") || trimmed.starts_with('$')) continue;
            if (trimmed.starts_with('}')) return false;
            if (trimmed.starts_with('{'))
            { inText = true; matched = current == name; result.clear(); first = true; continue; }
            current = trimmed;
            continue;
        }
        if (trimmed.starts_with('}'))
        {
            if (matched) { body = std::move(result); return true; }
            inText = false;
            continue;
        }
        if (!matched) continue;
        if (!first) result += '\n';
        result += text;
        first = false;
    }
    return false;
}

bool ActionButtonsAllowed(std::uint32_t buttons, bool hasUnexpiredConstraint)
{
    constexpr std::uint32_t directions = 0x618;
    return !(buttons & ~directions) && (!(buttons & directions) || hasUnexpiredConstraint);
}
bool ActionFinished(float now, float deadline, bool sequenceFinished)
{
    return (deadline > FLT_EPSILON && now > deadline) ||
        (deadline < FLT_EPSILON && sequenceFinished);
}
}
