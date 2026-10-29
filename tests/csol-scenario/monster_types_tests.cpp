// SPDX-License-Identifier: GPL-3.0-or-later
// Independent expectations follow the recovered mode-15 field contract.
// Actual CSO resources remain external to this source-only test target.
#include "monster_types.hpp"

#include <bit>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, std::string_view label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
const std::string header = "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n";
using Fields = std::vector<std::string>;
Fields Base()
{
    return {"1", "zombi_host", "10", "60", "3", "50", "40", "240", "300", "0", "1000", "255 255 255", "0", "101", "0", "0", "1", "120"};
}
std::string Csv(const Fields& fields)
{
    std::string value;
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (i) value += ',';
        if (fields[i].find_first_of(",\r\n\"") == std::string::npos) value += fields[i];
        else
        {
            value += '"';
            for (char c : fields[i]) { value += c; if (c == '"') value += '"'; }
            value += '"';
        }
    }
    return value + "\r\n";
}
void JsonString(std::ostream& out, std::string_view value)
{
    constexpr char digits[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127) out << "\\u00" << digits[c >> 4] << digits[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}
template<class Range> void Numbers(std::ostream& out, const Range& values)
{
    out << '[';
    bool first = true;
    for (auto value : values) { if (!first) out << ','; first = false; out << +value; }
    out << ']';
}
std::string Dump(const MonsterTypeTable& table)
{
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"sourceBytes\":"; JsonString(out, table.source.sourceBytes);
    out << ",\"records\":[";
    for (std::size_t i = 0; i < table.source.records.size(); ++i)
    {
        if (i) out << ',';
        const auto& record = table.source.records[i];
        out << "{\"offset\":" << record.byteOffset << ",\"fields\":[";
        for (std::size_t j = 0; j < record.fields.size(); ++j)
        { if (j) out << ','; JsonString(out, record.fields[j]); }
        out << "]}";
    }
    out << "],\"rows\":[";
    for (std::size_t i = 0; i < table.rows.size(); ++i)
    {
        if (i) out << ',';
        const auto& row = table.rows[i];
        out << "{\"record\":" << row.recordIndex << ",\"effectiveKey\":" << row.Key();
        out << ",\"typed\":{\"id\":" << row.id << ",\"appearance\":";
        JsonString(out, row.appearance);
        out << ",\"attackDamage\":" << row.attackDamage << ",\"attackRange\":" << row.attackRange
            << ",\"attackInterval\":" << row.attackInterval << ",\"health\":" << row.health
            << ",\"armor\":" << row.armor << ",\"speed\":" << row.speed << ",\"bonusMoney\":" << row.bonusMoney
            << ",\"priority\":" << +row.priority << ",\"skill\":" << row.skill << ",\"color\":";
        Numbers(out, row.color);
        out << ",\"Killtype\":" << row.killType << ",\"EventClass\":" << row.eventClass
            << ",\"propensity\":" << +row.propensity << ",\"clearpoint\":" << row.clearPoint << ",\"dropItem\":";
        Numbers(out, row.dropItems);
        out << ",\"exp\":" << row.experience << "},\"optionalColumns\":{\"18\":" << (row.column18 ? "true" : "false")
            << ",\"19\":" << row.column19 << ",\"20\":" << row.column20 << ",\"21\":";
        Numbers(out, row.column21);
        out << ",\"22\":" << row.column22 << "}}";
    }
    out << "],\"byId\":[";
    bool first = true;
    for (const auto [key, index] : table.byId)
    { if (!first) out << ','; first = false; out << '[' << key << ',' << index << ']'; }
    out << "]}";
    return out.str();
}
bool Read(const Fields& fields, MonsterTypeTable& table)
{
    ConfigError error{999, "stale"};
    const auto success = ReadMonsterTypes(header + Csv(fields), table, error);
    if (success) Check(error.byteOffset == 0 && error.message.empty(), "success clears stale diagnostic");
    return success;
}
void Reject(const std::string& input, std::size_t offset, std::string_view label)
{
    MonsterTypeTable table; ConfigError error;
    if (!ReadMonsterTypes(header + Csv(Base()), table, error)) throw std::runtime_error("invalid seed fixture");
    const auto before = Dump(table);
    Check(!ReadMonsterTypes(input, table, error), label);
    Check(!error.message.empty() && error.byteOffset == offset, "failure identifies record start");
    Check(Dump(table) == before, "invalid file preserves complete prior table");
}
void Values()
{
    MonsterTypeTable table;
    Check(Read(Base(), table), "ordinary mode-15 type accepted");
    Check(table.rows.size() == 1 && table.byId.size() == 1 && !table.Find(32800), "file rows only, no synthetic type");
    if (table.rows.empty()) return;
    const auto& base = table.rows.front();
    Check(base.id == 1 && base.appearance == "zombi_host" && base.attackDamage == 10 && base.attackRange == 60 && base.attackInterval == 3,
        "identity and attack fields retain original order");
    Check(base.health == 50 && base.armor == 40 && base.speed == 240 && base.bonusMoney == 300 && base.skill == 1000 && base.experience == 120,
        "health armor motion reward skill and experience retain order");
    Check(!base.column18 && base.column19 == -1 && base.column20 == -1 && base.column21 == std::array<float, 3>{} && base.column22 == -1,
        "absent optional columns have recovered defaults");
    auto fields = Base();
    fields[0] = " +65537tail"; fields[5] = " -2147483648tail"; fields[6] = "2147483647tail"; fields[17] = "  +120suffix";
    fields[2] = "65535"; fields[3] = "32768"; fields[7] = "65536"; fields[8] = "-32769";
    fields[9] = "128"; fields[10] = "65535"; fields[12] = "-32768"; fields[13] = "32767"; fields[14] = "-129";
    Check(Read(fields, table), "decimal prefix, full-width and narrow fields accepted");
    const auto& wide = table.rows.front();
    Check(wide.id == 65537 && wide.health == INT32_MIN && wide.armor == INT32_MAX && wide.experience == 120, "full int32 values and decimal prefixes retained");
    Check(wide.attackDamage == -1 && wide.attackRange == -32768 && wide.speed == 0 && wide.bonusMoney == 32767 && wide.skill == -1,
        "int16 conversion retains low bits");
    Check(wide.killType == -32768 && wide.eventClass == 32767 && wide.priority == -128 && wide.propensity == 127,
        "signed narrow control fields retain original bits");
    fields = Base();
    for (auto column : {2, 3, 7, 8, 9, 10, 12, 13, 14, 16}) fields[column].clear();
    Check(Read(fields, table), "empty narrow fields and item vector accepted");
    const auto& empty = table.rows.front();
    Check(empty.attackDamage == 0 && empty.attackRange == 0 && empty.speed == 0 && empty.bonusMoney == 0 && empty.priority == 0 &&
        empty.skill == 0 && empty.killType == 0 && empty.eventClass == 0 && empty.propensity == 0 && empty.dropItems.empty(), "empty narrow defaults and item vector are zero/empty");
    fields = Base(); fields[11] = "-2 3.5"; fields[16] = " | 1 2 | - 3 | + 4 |+-5|  |";
    Check(Read(fields, table), "original digit-run colors and whitespace-stripped item segments accepted");
    Check(table.rows.front().color == std::array<float, 3>{2, 3, 5}, "color scans unsigned digit runs without sign or fraction meaning");
    Check(table.rows.front().dropItems == std::vector<std::int32_t>{12, -3, 4, -5}, "drop parser skips empty segments and accepts original plus-minus prefix");
    fields = Base(); fields[11] = "300,400|500";
    Check(Read(fields, table) && table.rows.front().color == std::array<float, 3>{300, 400, 500}, "color values are not clamped to bytes");
    fields = Base(); fields.resize(23);
    fields[18] = "1"; fields[19] = "-7"; fields[20] = "99"; fields[21] = "-4 8"; fields[22] = "123";
    Check(Read(fields, table), "optional extension fields accepted");
    const auto& optional = table.rows.front();
    Check(optional.column18 && optional.column19 == -7 && optional.column20 == 99 && optional.column21 == std::array<float, 3>{4, 8, 0} && optional.column22 == 123,
        "extension fields retain original defaults and values");
    Check(optional.id == 1 && optional.Key() == 99 && table.Find(1) == nullptr && table.Find(99) == &table.rows.front(), "positive override affects key only");
    for (auto enabled : {"0", "2", "-1", "257"})
    {
        fields[18] = enabled; Check(Read(fields, table) && !table.rows.front().column18, "column18 compares full integer to exactly one");
    }
    fields = Base(); fields.resize(23);
    Check(Read(fields, table) && table.rows.front().column19 == -1 && table.rows.front().column20 == -1 && table.rows.front().column22 == -1,
        "explicit empty optional integers retain negative-one default");
    for (auto column : {18, 19, 20, 22}) fields[column] = " \t";
    Check(Read(fields, table) && table.rows.front().column19 == 0 && table.rows.front().column20 == 0 && table.rows.front().column22 == 0 && !table.rows.front().column18,
        "whitespace optional integers use atoi zero instead of missing fallback");
    fields = Base();
    for (auto column : {0, 4, 5, 6, 15, 17}) fields[column] = " \t";
    Check(Read(fields, table) && table.rows.front().id == 0 && table.rows.front().attackInterval == 0 && table.rows.front().health == 0 && table.rows.front().armor == 0 && table.rows.front().clearPoint == 0 && table.rows.front().experience == 0,
        "nonempty whitespace core scalars follow original zero parsing");
}
void FloatingPoint()
{
    MonsterTypeTable table;
    struct Sample { const char* input; float expected; };
    for (const auto& sample : {Sample{"0.1", static_cast<float>(0.1)}, {" +1.25e2tail", 125.f}, {"-0", -0.f}, {"1e-50", 0.f}, {"1.25e", 1.25f}, {".5tail", .5f}, {"-2.5suffix", -2.5f}})
    {
        auto fields = Base(); fields[4] = fields[15] = sample.input;
        Check(Read(fields, table), "decimal float prefix accepted");
        Check(std::bit_cast<std::uint32_t>(table.rows.front().attackInterval) == std::bit_cast<std::uint32_t>(sample.expected) &&
            std::bit_cast<std::uint32_t>(table.rows.front().clearPoint) == std::bit_cast<std::uint32_t>(sample.expected), "double-to-float bits including signed zero match contract");
    }
}
void IdentityAndOwnership()
{
    std::string source = header;
    for (const auto id : {1, 65537, -1, INT32_MIN, INT32_MAX})
    { auto fields = Base(); fields[0] = std::to_string(id); source += Csv(fields); }
    auto override = Base(); override[0] = "45"; override[5] = "901"; override.resize(23); override[20] = "65537"; source += Csv(override);
    auto duplicate = Base(); duplicate[5] = "902"; source += Csv(duplicate);
    source += " \t, \t\r\n";
    auto last = Base(); last[0] = "77"; last[1] = std::string("Unknown,") + static_cast<char>(0xb0) + static_cast<char>(0xa1); source += Csv(last);
    MonsterTypeTable table; ConfigError error;
    Check(ReadMonsterTypes(source, table, error), "duplicates full-width ids unknown appearance and blank record accepted");
    Check(table.rows.size() == 8 && table.source.records.size() == 10 && table.byId.size() == 6, "preserve all rows and blank source record, index latest six keys");
    Check(table.Find(1) == &table.rows[6] && table.Find(1)->health == 902 && table.Find(65537) == &table.rows[5] && table.Find(65537)->id == 45,
        "last duplicate effective key wins without 16-bit truncation");
    Check(table.Find(-1) && table.Find(INT32_MIN) && table.Find(INT32_MAX) && !table.Find(45) && !table.Find(999), "full signed key lookup and absent key behave correctly");
    Check(table.rows.back().recordIndex == 9 && table.rows.back().appearance == last[1], "blank record does not renumber following source, raw appearance bytes retained");
    Check(table.source.sourceBytes == source && table.source.records.back().fields == last, "owned original bytes and quoted source fields retained");
    const auto before = Dump(table);
    Check(ReadMonsterTypes(table.source.sourceBytes, table, error) && Dump(table) == before, "reloading aliased owned source is safe");
    source.assign(source.size(), 'x'); last.assign(last.size(), "changed");
    Check(Dump(table) == before, "output owns all source and field bytes");
    Check(ReadMonsterTypes(header, table, error) && table.rows.empty() && table.byId.empty(), "valid header-only replacement clears prior file rows");
}
void InvalidInput()
{
    Reject("", 0, "empty file rejected");
    Reject(std::string("\xef\xbb\xbf", 3), 0, "BOM-only file rejected without dereferencing empty records");
    Reject("id,appearance\r\n", 0, "short header rejected");
    auto wrong = header; wrong.replace(0, 2, "ID"); Reject(wrong + Csv(Base()), 0, "strict header name policy enforced");
    for (auto column : {0, 1, 4, 5, 6, 11, 15, 17})
    {
        auto fields = Base(); fields[column].clear();
        Reject(header + Csv(Base()) + Csv(fields), header.size() + Csv(Base()).size(), "missing core field rejected transactionally after valid row");
    }
    for (const auto* text : {"word", "+", "-", "+-1", "2147483648", "-2147483649"})
    {
        auto fields = Base(); fields[5] = text;
        Reject(header + Csv(fields), header.size(), "malformed or overflowing scalar integer rejected");
    }
    for (const auto* text : {"nan", "inf", "-inf", "1e999", "1e-999", "3.5e38", "+-1", "0x1p2", "-0x1p2", "+0X1P2", "word"})
    {
        auto fields = Base(); fields[4] = text;
        Reject(header + Csv(fields), header.size(), "unsafe or unsupported float rejected explicitly");
    }
    for (const auto* text : {"1 2", "1 2 3 4", "2147483648 2 3", "words"})
    {
        auto fields = Base(); fields[11] = text;
        Reject(header + Csv(fields), header.size(), "unsafe core color shape rejected");
    }
    for (const auto& text : {std::string("word"), std::string("2147483648"), std::string(64, '1')})
    {
        auto fields = Base(); fields[16] = text;
        Reject(header + Csv(fields), header.size(), "unsafe drop item rejected");
    }
    auto fields = Base(); fields.resize(23); fields[21] = "1 2 3 4";
    Reject(header + Csv(fields), header.size(), "optional vector overflow rejected");
    fields = Base(); fields.resize(23); fields[20] = "2147483648";
    Reject(header + Csv(fields), header.size(), "override key overflow rejected");
}
void ResourceFailures()
{
    MonsterTypeTable table; ConfigError error;
    Check(Read(Base(), table), "resource failure fixture seeded");
    const auto before = Dump(table);
    for (std::size_t size : {0u, 1u, 7u, 9u, 8u * 1024 * 1024 + 8})
    {
        const std::vector<std::byte> bytes(size);
        error = {999, "stale"};
        Check(!ReadMonsterTypeResource(bytes, table, error) && error.byteOffset == 0 && error.message == "invalid encrypted scenario configuration size", "type resource entry rejects invalid encrypted length");
        Check(Dump(table) == before, "resource size failure preserves entire prior table");
    }
    const unsigned char ciphertext[]{0xf1, 0x5d, 0x9c, 0x83, 0xa1, 0x4c, 0x4e, 0x63}; // Original-HW oracle: eight zero bytes.
    Check(!ReadMonsterTypeResource(std::as_bytes(std::span(ciphertext)), table, error), "valid ciphertext with empty decoded table rejected");
    Check(Dump(table) == before && !error.message.empty(), "decoded parser failure preserves prior table");
}
int Inspect(const char* mode, const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) { std::cerr << "Cannot open external fixture\n"; return 2; }
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    MonsterTypeTable table; ConfigError error;
    const bool valid = std::string_view(mode) == "--inspect-resource" ?
        ReadMonsterTypeResource(std::as_bytes(std::span(bytes.data(), bytes.size())), table, error) : ReadMonsterTypes(bytes, table, error);
    if (!valid) { std::cerr << error.byteOffset << ": " << error.message << '\n'; return 3; }
    std::cout << Dump(table) << '\n';
    return 0;
}
}
int main(int argc, char** argv)
{
    if (argc == 3 && (std::string_view(argv[1]) == "--inspect-decoded" || std::string_view(argv[1]) == "--inspect-resource"))
        return Inspect(argv[1], argv[2]);
    Values(); FloatingPoint(); IdentityAndOwnership(); InvalidInput(); ResourceFailures();
    std::cout << "CSOL scenario monster types: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
