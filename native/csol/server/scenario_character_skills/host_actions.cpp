// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_actions.hpp"
#include <array>
#include <charconv>
#include <cfloat>

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
        if (action.frameRate <= 0 || !candidate.emplace(action.modelId, action).second)
        { error.message = "character action5 has zero frame rate or a duplicate model id"; return false; }
    }
    output = std::move(candidate);
    error = {};
    return true;
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
