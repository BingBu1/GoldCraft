// SPDX-License-Identifier: GPL-3.0-or-later
#include "equipment.hpp"
#include "../config_codec.hpp"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <charconv>
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
}
