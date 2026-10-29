// SPDX-License-Identifier: GPL-3.0-or-later
#include "equipment.hpp"
#include "../config_codec.hpp"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <algorithm>
#include <charconv>
#include <bit>
#include <cmath>
#include <memory>

namespace csol::scenario
{
namespace
{
std::string_view Trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n\v\f");
    return first == text.npos ? std::string_view{} : text.substr(first);
}

bool Integer(std::string_view text, std::int32_t& output)
{
    output = 0;
    text = Trim(text);
    if (text.empty()) return true;
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9') return true;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} || result.ec == std::errc::invalid_argument;
}

bool Number(std::string_view text, float& output)
{
    output = 0;
    text = Trim(text);
    if (text.empty()) return true;
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '+' || text.front() == '-') return true;
    }
    auto magnitude = text;
    if (magnitude.starts_with('-')) magnitude.remove_prefix(1);
    if (magnitude.starts_with("0x") || magnitude.starts_with("0X")) return false;
    double value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc::invalid_argument) return true;
    if (result.ec != std::errc{} || !std::isfinite(value)) return false;
    output = static_cast<float>(value);
    return std::isfinite(output);
}

const rapidjson::Value* Member(const rapidjson::Value& parent, const char* name)
{
    if (!parent.IsObject()) return nullptr;
    const auto found = parent.FindMember(name);
    return found == parent.MemberEnd() ? nullptr : &found->value;
}

using Names = std::map<std::string, std::int32_t, std::less<>>;
bool ReadNames(const rapidjson::Value* node, Names& output)
{
    if (!node) return true;
    if (!node->IsObject()) return false;
    for (const auto& field : node->GetObject())
    {
        if (!field.value.IsInt()) return false;
        output[std::string(field.name.GetString(), field.name.GetStringLength())] = field.value.GetInt();
    }
    return true;
}

std::int32_t NameValue(const Names& values, const rapidjson::Value& name)
{
    const auto found = values.find(std::string_view(name.GetString(), name.GetStringLength()));
    return found == values.end() ? 0 : found->second;
}

bool File(const char* path, ResourceFileApi files, std::string& output, ResourceError& error)
{
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path, &length), files.release);
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024)
    { error = {path, {0, "scenario equipment resource is missing or has an invalid size"}}; return false; }
    output.assign(reinterpret_cast<const char*>(bytes.get()), length);
    return true;
}
}

bool ReadEquipmentBuffs(std::string_view bytes, EquipmentTables& output, ConfigError& error)
{
    error = {};
    CsvDocument document;
    if (!ReadCsv(bytes, document, error)) return false;
    decltype(output.buffs) buffs;
    for (std::size_t row = 1; row < document.records.size(); ++row)
    {
        const auto& record = document.records[row];
        if (record.fields.size() < 18)
        { error = {record.byteOffset, "equipment buff requires eighteen columns"}; return false; }
        const auto& fields = record.fields;
        EquipmentBuff buff;
        std::int32_t global{}, worn{}, cumulative{};
        if (!Integer(fields[0], buff.item) || !Integer(fields[5], global) || !Integer(fields[6], worn) ||
            !Integer(fields[7], buff.type) || !Integer(fields[8], buff.priority) ||
            !Integer(fields[11], cumulative) || !Integer(fields[12], buff.accumulationId) ||
            !Integer(fields[13], buff.accumulationLimit))
        { error = {record.byteOffset, "equipment buff integer exceeds int32"}; return false; }
        constexpr std::array<std::size_t, 6> columns{9, 10, 14, 15, 16, 17};
        for (std::size_t i = 0; i < columns.size(); ++i)
            if (!Number(fields[columns[i]], buff.values[i]))
            { error = {record.byteOffset, "equipment buff value is outside the finite float contract"}; return false; }
        buff.global = global > 0;
        buff.wornOnly = worn > 0;
        buff.cumulative = cumulative != 0;
        buffs[buff.item].push_back(buff);
    }
    output.buffSource = std::move(document);
    output.buffs = std::move(buffs);
    return true;
}

bool ReadEquipmentBuffResource(std::span<const std::byte> encoded, EquipmentTables& output, ConfigError& error)
{
    error = {};
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    { error.message = "invalid encrypted equipment buff size"; return false; }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    const auto embedded = plain.find('\0');
    if (embedded != plain.npos)
    { error = {embedded, "embedded NUL in equipment buff resource"}; return false; }
    return ReadEquipmentBuffs(plain, output, error);
}

bool ReadEquipmentSettings(std::string_view json, EquipmentTables& output, ConfigError& error)
{
    error = {};
    const auto fail = [&](const char* message) { error.message = message; return false; };
    if (json.empty() || json.size() > 8 * 1024 * 1024 || json.find('\0') != json.npos)
        return fail("invalid scenario equipment JSON size or embedded NUL");
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError())
    { error = {document.GetErrorOffset(), rapidjson::GetParseError_En(document.GetParseError())}; return false; }
    if (!document.IsObject()) return fail("scenario equipment JSON root must be an object");
    Names types, operations;
    decltype(output.effects) effects;
    decltype(output.addons) addons;
    if (const auto* clue = Member(document, "ZBS_ClueItem"))
    {
        if (!clue->IsObject() || !ReadNames(Member(*clue, "Type"), types) ||
            !ReadNames(Member(*clue, "OperatorType"), operations))
            return fail("invalid scenario clue type or operator mapping");
        if (const auto* items = Member(*clue, "ClueItems"))
        {
            if (!items->IsArray()) return fail("scenario ClueItems must be an array");
            for (const auto& item : items->GetArray())
            {
                const auto* id = Member(item, "itemID");
                const auto* type = Member(item, "type");
                const auto* value = Member(item, "value");
                const auto* resource = Member(item, "resourceName");
                const auto* operation = Member(item, "operatorType");
                // The original skips incomplete clue records, including ID 0.
                if (!id || !type || !value || !resource || !operation) continue;
                if (!id->IsInt() || !type->IsString() || !value->IsNumber() ||
                    !resource->IsString() || !operation->IsString()) return fail("invalid clue record field type");
                if (!id->GetInt()) continue;
                EquipmentEffect effect;
                effect.type = NameValue(types, *type);
                effect.operation = NameValue(operations, *operation);
                effect.value = value->GetFloat();
                if (!std::isfinite(effect.value)) return fail("clue value exceeds finite float");
                effect.resourceName.assign(resource->GetString(), resource->GetStringLength());
                effects[id->GetInt()].push_back(std::move(effect));
            }
        }
    }
    if (const auto* group = Member(document, "ZBS_Addon"))
    {
        if (!group->IsObject()) return fail("ZBS_Addon must be an object");
        if (const auto* records = Member(*group, "Addons"))
        {
            if (!records->IsArray()) return fail("scenario Addons must be an array");
            for (const auto& record : records->GetArray())
            {
                const auto* item = Member(record, "ItemID");
                const auto* params = Member(record, "Params");
                // The ID presence query has no conditional branch in the
                // original. Even missing ItemID/Params retain a physical row.
                if (!record.IsObject() || (item && !item->IsInt()) || (params && !params->IsObject()))
                    return fail("invalid addon item or parameters");
                EquipmentAddon addon;
                addon.item = item ? item->GetInt() : 0;
                if (params) for (const auto& parameter : params->GetObject())
                {
                    if (!parameter.value.IsNumber()) return fail("addon parameter must be numeric");
                    const float value = parameter.value.GetFloat();
                    if (!std::isfinite(value)) return fail("addon parameter exceeds finite float");
                    addon.parameters[std::string(parameter.name.GetString(), parameter.name.GetStringLength())] = value;
                }
                addons.push_back(std::move(addon));
            }
        }
    }
    output.effectTypes = std::move(types);
    output.effects = std::move(effects);
    output.addons = std::move(addons);
    return true;
}

bool EquipmentResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (m_busy || !files.load || !files.release)
    { error.detail.message = "scenario equipment loader is busy or unavailable"; return false; }
    struct Busy { bool& flag; ~Busy() { flag = false; } } busy{m_busy};
    m_busy = true;
    m_tables.reset();
    auto candidate = std::make_shared<EquipmentTables>();
    std::string bytes;
    constexpr const char* buffs = "resource/buff/BuffWeapon.cso";
    constexpr const char* settings = "maps/ZBS.json";
    if (!File(buffs, files, bytes, error)) return false;
    ConfigError detail;
    if (!ReadEquipmentBuffResource(std::as_bytes(std::span(bytes.data(), bytes.size())), *candidate, detail))
    { error = {buffs, std::move(detail)}; return false; }
    if (!File(settings, files, bytes, error)) return false;
    if (!ReadEquipmentSettings(bytes, *candidate, detail))
    { error = {settings, std::move(detail)}; return false; }
    m_tables = std::move(candidate);
    return true;
}

bool EquipmentResources::Clear()
{
    if (m_busy) return false;
    m_tables.reset();
    return true;
}

bool ReadClassMods(std::string_view bytes, ClassModTable& output, ConfigError& error)
{
    error = {};
    ClassModTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    for (std::size_t row = 1; row < candidate.source.records.size(); ++row)
    {
        const auto& record = candidate.source.records[row];
        if (record.fields.size() < 11)
        { error = {record.byteOffset, "class mod definition requires eleven columns"}; return false; }
        ClassModDefinition definition;
        std::int32_t type{}, subtype{};
        // Original readers are atoi/atof and store the low16 bits for these
        // two fields. Empty integer keys are malformed host input: the original
        // reuses an uninitialized temporary for some of them, not reliable0.
        for (std::size_t column = 0; column < 4; ++column)
            if (Trim(record.fields[column]).empty())
            { error = {record.byteOffset, "class mod integer key is empty"}; return false; }
        if (!Integer(record.fields[0], definition.item) || !Integer(record.fields[1], definition.category) ||
            !Integer(record.fields[2], type) || !Integer(record.fields[3], subtype))
        { error = {record.byteOffset, "class mod integer exceeds int32"}; return false; }
        definition.type = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(type));
        definition.subtype = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(subtype));
        for (std::size_t column = 0; column < definition.values.size(); ++column)
            if (!Number(record.fields[4 + column], definition.values[column]))
            { error = {record.byteOffset, "class mod parameter exceeds finite float"}; return false; }
        if (!candidate.definitions.emplace(definition.item, definition).second)
        { error = {record.byteOffset, "duplicate class mod item definition"}; return false; }
    }
    output = std::move(candidate);
    return true;
}

bool ClassModResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (m_busy || !files.load || !files.release)
    { error.detail.message = "class mod loader is busy or unavailable"; return false; }
    m_busy = true;
    struct Busy { bool& flag; ~Busy() { flag = false; } } busy{m_busy};
    m_table.reset();
    constexpr const char* path = "resource/buff/ClassMod.csv";
    std::string bytes;
    if (!File(path, files, bytes, error)) return false;
    auto candidate = std::make_shared<ClassModTable>();
    ConfigError detail;
    if (!ReadClassMods(bytes, *candidate, detail))
    { error = {path, std::move(detail)}; return false; }
    m_table = std::move(candidate);
    return true;
}

bool ClassModResources::Clear()
{
    if (m_busy) return false;
    m_table.reset();
    return true;
}

bool ReadClassModCategorySlots(std::string_view json,
    std::array<std::int32_t, 9>& output, ConfigError& error)
{
    error = {};
    if (json.empty() || json.size() > 8 * 1024 * 1024 || json.find('\0') != json.npos)
    { error.message = "invalid class mod configuration size or embedded NUL"; return false; }
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError())
    { error = {document.GetErrorOffset(), rapidjson::GetParseError_En(document.GetParseError())}; return false; }
    const auto* slots = Member(document, "CategoryMaxSlots");
    if (!slots || !slots->IsArray() || slots->Size() != output.size())
    { error.message = "class mod CategoryMaxSlots must contain nine counts"; return false; }
    std::array<std::int32_t, 9> candidate{};
    for (rapidjson::SizeType i = 0; i < slots->Size(); ++i)
    {
        const auto& value = (*slots)[i];
        // The original writes directly into five slots per group. Reject
        // malformed counts instead of reproducing that out-of-bounds write.
        if (!value.IsInt() || value.GetInt() < 0 || value.GetInt() > 5)
        { error.message = "class mod category count exceeds its five slots"; return false; }
        candidate[i] = value.GetInt();
    }
    output = candidate;
    return true;
}

bool ReadClassModPresets(std::string_view bytes, const std::array<std::int32_t, 9>& categorySlots,
    const CsvDocument& items, ClassModPresetTable& output, ResourceError& error)
{
    error = {};
    constexpr const char* presetPath = "resource/buff/ClassModPreset.csv";
    constexpr const char* itemPath = "resource/item.csv";
    if (std::any_of(categorySlots.begin(), categorySlots.end(), [](int value) { return value < 0 || value > 5; }))
    { error = {"resource/buff/ClassModConfig.json", {0, "class mod category count exceeds its five slots"}}; return false; }
    ClassModPresetTable candidate;
    candidate.categorySlots = categorySlots;
    if (!ReadCsv(bytes, candidate.source, error.detail)) { error.path = presetPath; return false; }

    // Reuse the same physical item rows. Only caps consumed by a preset need
    // conversion; Selection and unrelated weapon rows gain no new column gate.
    std::map<std::int32_t, const CsvRecord*> byItem;
    for (std::size_t row = 2; row < items.records.size(); ++row)
    {
        const auto& item = items.records[row];
        std::int32_t id{};
        if (item.fields.empty() || !Integer(item.fields[0], id))
        { error = {itemPath, {item.byteOffset, "invalid class mod item catalog key"}}; return false; }
        if (!byItem.emplace(id, &item).second)
        { error = {itemPath, {item.byteOffset, "duplicate class mod item catalog key"}}; return false; }
    }
    for (std::size_t row = 1; row < candidate.source.records.size(); ++row)
    {
        const auto& source = candidate.source.records[row];
        EquipmentLoadout::ClassCaps record;
        if (!source.fields.empty() && !Integer(source.fields[0], record.classId))
        { error = {presetPath, {source.byteOffset, "class mod preset class ID exceeds int32"}}; return false; }
        // Column1 (PresetID) is ignored by the original, including when it
        // differs from1. Groups0/1 and unfilled slots retain their initial -1.
        std::size_t column = 2;
        for (std::size_t group = 2; group < categorySlots.size(); ++group)
            for (int slot = 0; slot < categorySlots[group]; ++slot, ++column)
            {
                if (column >= source.fields.size() || source.fields[column].empty()) continue;
                if (!Integer(source.fields[column], record.slots[group * 5 + slot]))
                { error = {presetPath, {source.byteOffset, "class mod preset slot exceeds int32"}}; return false; }
            }
        if (const auto found = byItem.find(record.classId); found != byItem.end())
        {
            const auto& item = *found->second;
            for (std::size_t i = 0; i < record.values.size(); ++i)
            {
                // ItemTable.v58(i,0) selects the first integer from CSV
                // columns21..25. strtok skips comma delimiters, not spaces.
                // A missing item returns0, with no class fallback. The raw
                // CSV loader's treatment of absent columns is not verified;
                // require the five actual fields on a referenced item row.
                if (21 + i >= item.fields.size())
                { error = {itemPath, {item.byteOffset, "class mod item catalog is missing a default cap column"}}; return false; }
                std::string_view value = item.fields[21 + i];
                const auto start = value.find_first_not_of(',');
                if (start != value.npos)
                {
                    value.remove_prefix(start);
                    value = value.substr(0, value.find(','));
                }
                if (!Integer(value, record.values[i]))
                { error = {itemPath, {item.byteOffset, "class mod default cap exceeds int32"}}; return false; }
            }
        }
        // manager+4C is keyed only by classId; a later physical row replaces
        // the complete record, irrespective of its ignored PresetID.
        candidate.records.insert_or_assign(record.classId, std::move(record));
    }
    output = std::move(candidate);
    return true;
}

bool ClassModPresetResources::Load(ResourceFileApi files,
    std::shared_ptr<const CsvDocument> items, bool (*current)(const void*), const void* owner,
    ResourceError& error)
{
    error = {};
    if (m_busy || !files.load || !files.release || !items || !current)
    { error.detail.message = "class mod preset loader or item catalog is unavailable"; return false; }
    m_busy = true;
    struct Busy { bool& flag; ~Busy() { flag = false; } } busy{m_busy};
    m_table.reset();
    if (!current(owner)) return false;
    constexpr const char* settings = "resource/buff/ClassModConfig.json";
    constexpr const char* presets = "resource/buff/ClassModPreset.csv";
    std::string bytes;
    ConfigError detail;
    auto candidate = std::make_shared<ClassModPresetTable>();
    const bool settingsLoaded = File(settings, files, bytes, error);
    if (!current(owner)) { error = {}; return false; }
    if (!settingsLoaded) return false;
    std::array<std::int32_t, 9> slots{};
    if (!ReadClassModCategorySlots(bytes, slots, detail))
    { error = {settings, std::move(detail)}; return false; }
    const bool presetsLoaded = File(presets, files, bytes, error);
    if (!current(owner)) { error = {}; return false; }
    if (!presetsLoaded) return false;
    if (!ReadClassModPresets(bytes, slots, *items, *candidate, error)) return false;
    if (!current(owner)) { error = {}; return false; }
    m_table = std::move(candidate);
    return true;
}

bool ClassModPresetResources::Clear()
{
    if (m_busy) return false;
    m_table.reset();
    return true;
}
}
