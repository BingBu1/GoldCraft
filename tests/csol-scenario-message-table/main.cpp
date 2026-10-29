// SPDX-License-Identifier: GPL-3.0-or-later
// Independent contract fixtures and full-field original resource expectations.
// No engine, SDK substitute, network or client presentation is involved.
#include "message_table.hpp"
#include "fixtures.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace csol::scenario;
int ScenarioAnnounceTableCases();
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
std::string Read(const std::string& name)
{
    std::ifstream file(name, std::ios::binary);
    if (!file) throw std::runtime_error("missing input " + name);
    return {std::istreambuf_iterator<char>(file), {}};
}
std::string Csv(const std::vector<std::string>& cells)
{
    std::string out;
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        if (i) out += ',';
        if (cells[i].find_first_of(",\"\r\n") == cells[i].npos) out += cells[i];
        else
        {
            out += '"';
            for (const char c : cells[i]) { if (c == '"') out += '"'; out += c; }
            out += '"';
        }
    }
    return out + "\r\n";
}
std::vector<std::string> Cells(std::string id = "43")
{
    std::vector<std::string> cells(15);
    cells[0] = std::move(id);
    return cells;
}
const std::string header = "arbitrary,labels,are,not,the,schema\r\n";
void Defaults(const ScenarioMessage& row)
{
    Check(row.text.empty() && row.image.empty() && row.sound.empty(), "absent strings default empty");
    Check(row.color == std::array<std::uint8_t, 3>{255, 180, 30}, "empty optional RGB preserves 255 180 30");
    Check(row.fadeIn == 0.5f && row.show == 3.0f && row.fadeOut == 0.5f, "empty durations retain defaults");
    Check(row.type == 0 && row.textType == -1, "empty type preserves 0 and -1 before presentation");
    Check(row.flickerTime == -1 && row.xCenterRatio == -1 && row.yCenterRatio == -1, "empty optional floats retain -1");
}
void Contract()
{
    MessageTable table; ConfigError error{12, "old"};
    Check(ReadMessageTable(header + "43\r\n" + Csv(Cells("44")), table, error), "fixed column table allows absent and empty optional cells");
    Check(error.message.empty() && error.byteOffset == 0, "success clears old error");
    Check(table.rows.size() == 2 && table.byId.size() == 2, "two source rows retained");
    if (table.rows.size() != 2) return;
    Defaults(table.rows[0]); Defaults(table.rows[1]);
    Check(table.Find(43) == &table.rows[0] && !table.Find(65000), "exact u16 lookup and unknown ID without insertion");
    Check(table.rows[0].recordIndex == 1 && table.rows[1].recordIndex == 2, "first record skipped with physical record index preserved");
    auto full = Cells();
    full[1] = " #KEY,\"two\"\r\nline \xb0\xa1";
    full[2] = " image.tga "; full[3] = "sound/test.wav";
    full[4] = "256"; full[5] = "-1"; full[6] = "  +513tail";
    full[7] = "+0.125suffix"; full[8] = "-2.5x"; full[9] = "1.25e1!";
    full[10] = "-2147483648suffix"; full[11] = "2147483647.5";
    full[12] = "-0"; full[13] = ".75"; full[14] = "-1e-1";
    const auto bytes = header + Csv(full);
    Check(ReadMessageTable(bytes, table, error), "decimal prefixes quoted bytes and nonnormalized values parse");
    if (table.rows.empty()) return;
    const auto& row = table.rows[0];
    Check(row.text == full[1] && row.image == full[2] && row.sound == full[3], "strings preserve whitespace localization bytes quotes and newlines");
    Check(row.color == std::array<std::uint8_t, 3>{0, 255, 1}, "RGB keeps low 8 bits instead of clamping");
    Check(row.fadeIn == 0.125f && row.show == -2.5f && row.fadeOut == 12.5f, "atof decimal prefix durations preserve sign and exponent");
    Check(row.type == INT32_MIN && row.textType == INT32_MAX, "type fields retain signed int32 range");
    Check(std::bit_cast<std::uint32_t>(row.flickerTime) == 0x80000000u && row.xCenterRatio == .75f && row.yCenterRatio == -.1f, "float32 conversion preserves negative zero and fractions");
    Check(table.source.sourceBytes == bytes && table.source.records[1].fields == full, "CSV source bytes and every original field retained");
    auto whitespace = Cells();
    for (std::size_t col = 4; col < whitespace.size(); ++col) whitespace[col] = " \t\v\f";
    Check(ReadMessageTable(header + Csv(whitespace), table, error), "nonempty whitespace numeric fields write zero");
    Check(table.rows[0].color == std::array<std::uint8_t, 3>{0, 0, 0} && table.rows[0].fadeIn == 0 && table.rows[0].show == 0 && table.rows[0].fadeOut == 0 && table.rows[0].type == 0 && table.rows[0].textType == 0 && table.rows[0].flickerTime == 0 && table.rows[0].xCenterRatio == 0 && table.rows[0].yCenterRatio == 0, "whitespace differs from empty defaults");
    for (const std::string value : {"nonsense", "+", "+-1", "--1", "++1"})
    {
        auto cells = Cells(); for (std::size_t col = 4; col < cells.size(); ++col) cells[col] = value;
        Check(ReadMessageTable(header + Csv(cells), table, error), "no numeric prefix produces zero");
        Check(table.rows[0].color[0] == 0 && table.rows[0].show == 0 && table.rows[0].textType == 0 && table.rows[0].flickerTime == 0, "atoi and atof no-prefix result zero");
    }
    const std::pair<const char*, std::uint16_t> ids[]{{"-1", 65535}, {"65536", 0}, {"65579", 43},
        {"2147483647", 65535}, {"-2147483648", 0}, {"+43suffix", 43}, {"0x43", 0}, {"invalid", 0}, {" ", 0}};
    for (const auto& [text, expected] : ids)
    {
        Check(ReadMessageTable(header + Csv(Cells(text)), table, error), "ID uses decimal prefix then low16");
        Check(table.rows[0].id == expected && table.Find(expected) == &table.rows[0], "ID narrowing lookup agrees");
    }
    auto first = full; first[0] = "43";
    auto last = Cells("65579"); last[1] = " ";
    auto third = Cells("43"); third[1] = "#LAST";
    const auto collisions = header + Csv(first) + "\r\n,,,,\r\n" + Csv(last) + Csv(third);
    Check(ReadMessageTable(collisions, table, error), "low16 collisions and blank records parse");
    Check(table.rows.size() == 3 && table.byId.size() == 1 && table.Find(43) == &table.rows.back(), "last source row wins while all duplicate rows retained");
    Check(table.rows[1].text == " " && table.rows[1].show == 3 && table.rows[1].color[1] == 180, "duplicate replaces with fresh defaults rather than inheriting earlier values");
    Check(table.Find(43)->text == "#LAST" && table.rows[2].recordIndex == 5 && table.source.records.size() == 6, "blank records stay in document but not data rows");
    Check(ReadMessageTable(header, table, error) && table.rows.empty() && table.byId.empty(), "header-only load clears prior keys");
    Check(ReadMessageTable("43,skipped\r\n44,kept", table, error) && !table.Find(43) && table.Find(44)->text == "kept", "first record skipped without guessed header names");
}
void TransactionFailures()
{
    MessageTable table; ConfigError error;
    const auto valid = header + "43,stable\r\n";
    Check(ReadMessageTable(valid, table, error), "transaction baseline loads");
    const auto* before = table.Find(43);
    const auto fail = [&](const std::string& bytes)
    {
        Check(!ReadMessageTable(bytes, table, error) && !error.message.empty(), "malformed input rejected with diagnostic");
        Check(table.source.sourceBytes == valid && table.Find(43) == before && table.byId.size() == 1 && table.rows[0].text == "stable", "failure preserves previous complete table and lookup identity");
    };
    fail(""); fail(header + ",text\r\n"); fail(header + "43,\"unterminated");
    fail(header + "43,bad\"quote\r\n"); fail(header + "43,\"x\"bad\r\n");
    fail(header + std::string("43,a\0b\r\n", 9));
    fail(std::string(8 * 1024 * 1024 + 1, 'a'));
    for (std::size_t column : {0u, 4u, 5u, 6u, 10u, 11u})
        for (const std::string value : {"2147483648", "-2147483649"})
        { auto cells = Cells(); cells[column] = value; fail(header + "44,candidate\r\n" + Csv(cells)); }
    for (std::size_t column : {7u, 8u, 9u, 12u, 13u, 14u})
        for (const std::string value : {"nan", "inf", "-inf", "1e400", "1e39", "-0x1.8p1", "+0X1p1"})
        { auto cells = Cells(); cells[column] = value; fail(header + "44,candidate\r\n" + Csv(cells)); }
    auto cells = Cells(); cells[1] = std::string(65537, 'x'); fail(header + Csv(cells));
    cells = Cells(); cells.resize(257); fail(header + Csv(cells));
    Check(ReadMessageTable(header + "99,recovered\r\n", table, error) && !table.Find(43) && table.Find(99)->text == "recovered" && error.message.empty(), "repaired load fully replaces prior table and clears error");
}
std::string Unhex(std::string_view text)
{
    std::string bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) bytes.push_back(static_cast<char>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    return bytes;
}
bool Resource(const std::string& bytes, MessageTable& table, ConfigError& error)
{ return ReadMessageTableResource(std::as_bytes(std::span(bytes.data(), bytes.size())), table, error); }
void ResourceTransactions()
{
    MessageTable table; ConfigError error;
    auto encoded = Unhex(fixtures::valid); const auto original = encoded;
    Check(Resource(encoded, table, error), "independent TEA ciphertext loads directly");
    Check(table.Find(43) && table.Find(43)->text == "#TEST" && table.Find(43)->show == 3, "resource decode reaches parser with defaults");
    Check(encoded == original, "resource decoder never mutates caller bytes");
    const auto* before = table.Find(43); const auto source = table.source.sourceBytes;
    std::vector<std::string> failuresToLoad{"", "x", std::string(9, 'x'), std::string(8 * 1024 * 1024 + 8, 'x'),
        Unhex(fixtures::empty), Unhex(fixtures::embeddedNul), Unhex(fixtures::invalidField), Unhex(fixtures::invalidCsv)};
    for (const auto& bytes : failuresToLoad)
    {
        const auto copy = bytes;
        Check(!Resource(bytes, table, error) && !error.message.empty(), "encrypted size or decoded content failure rejected");
        Check(table.Find(43) == before && table.source.sourceBytes == source && table.rows.size() == 1, "encrypted failure preserves loaded table atomically");
        Check(bytes == copy, "rejected encrypted input unmodified");
    }
    Check(Resource(Unhex(fixtures::replacement), table, error) && !table.Find(43) && table.Find(99)->text == "replacement", "successful encrypted reload retires old keys");
}
class ExpectedReader
{
    std::string bytes; std::size_t at{};
public:
    explicit ExpectedReader(const std::string& path) : bytes(Read(path)) {}
    std::uint32_t U32()
    {
        if (bytes.size() - at < 4) throw std::runtime_error("truncated independent expected data");
        std::uint32_t value{};
        for (unsigned shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at++])) << shift;
        return value;
    }
    std::string String()
    {
        const auto size = U32();
        if (size > bytes.size() - at) throw std::runtime_error("truncated independent string");
        auto result = bytes.substr(at, size); at += size; return result;
    }
    bool End() const { return at == bytes.size(); }
};
void CompareActual(const MessageTable& table, const std::string& path, const std::string& plain)
{
    ExpectedReader data(path);
    if (data.U32() != 0x3147534du) throw std::runtime_error("expected data version");
    const auto records = data.U32(), count = data.U32(), keys = data.U32();
    Check(table.source.sourceBytes == plain, "actual decoded bytes exactly match independent TEA output");
    Check(table.source.records.size() == records && table.rows.size() == count && table.byId.size() == keys, "actual complete document rows and reachable keys match independent CSV");
    if (table.source.records.size() != records || table.rows.size() != count) throw std::runtime_error("actual size differs from frozen expectation");
    for (const auto& record : table.source.records)
    {
        Check(record.byteOffset == data.U32(), "actual record byte offset preserved");
        const auto columns = data.U32();
        Check(record.fields.size() == columns, "actual source column count preserved");
        for (std::uint32_t i = 0; i < columns; ++i)
        { const auto expected = data.String(); Check(i < record.fields.size() && record.fields[i] == expected, "actual source CSV cell bytes preserved"); }
    }
    for (std::size_t i = 0; i < table.rows.size(); ++i)
    {
        const auto& row = table.rows[i];
        Check(row.recordIndex == data.U32(), "actual message original record index");
        Check(row.id == data.U32(), "actual narrowed ID");
        Check(row.text == data.String(), "actual message text bytes");
        Check(row.image == data.String(), "actual message image bytes");
        Check(row.sound == data.String(), "actual message sound bytes");
        for (const auto channel : row.color) Check(channel == data.U32(), "actual narrowed RGB channel");
        for (const auto value : {row.fadeIn, row.show, row.fadeOut}) Check(std::bit_cast<std::uint32_t>(value) == data.U32(), "actual duration float32 bits");
        Check(static_cast<std::uint32_t>(row.type) == data.U32(), "actual type int32");
        Check(static_cast<std::uint32_t>(row.textType) == data.U32(), "actual text type int32");
        for (const auto value : {row.flickerTime, row.xCenterRatio, row.yCenterRatio}) Check(std::bit_cast<std::uint32_t>(value) == data.U32(), "actual optional float32 bits");
    }
    for (std::uint32_t i = 0; i < keys; ++i)
    {
        const auto id = data.U32(), index = data.U32();
        Check(index < table.rows.size() && table.Find(static_cast<std::uint16_t>(id)) == &table.rows[index], "actual last-wins lookup matches independently built index");
    }
    Check(data.End(), "entire independent expected file consumed");
}
void Actual(const std::string& encodedPath, const std::string& plainPath, const std::string& expectedPath)
{
    const auto encoded = Read(encodedPath), plain = Read(plainPath);
    MessageTable decoded, resource; ConfigError error;
    Check(ReadMessageTable(plain, decoded, error), "actual decoded message table accepted");
    CompareActual(decoded, expectedPath, plain);
    Check(Resource(encoded, resource, error), "actual encrypted common message table accepted directly");
    CompareActual(resource, expectedPath, plain);
    Check(resource.Find(43) && resource.Find(43)->text == "#CSO_ZS_Warning_MSG_43" && resource.Find(43)->sound.empty(), "actual ID43 retains the raw localization token without parser localization or synthesized clear event");
    for (const std::uint16_t id : {193, 194, 195, 196, 197, 199, 200, 201, 202, 203, 204})
    {
        const auto* row = resource.Find(id);
        Check(row && !row->text.empty() && row->text.front() == '#' && row->sound == "events/tutor_msg.wav", "deadend event resolves localization token and original sound");
        Check(row && row->color == std::array<std::uint8_t, 3>{255, 117, 117} && row->fadeIn == .5f && row->show == 3 && row->fadeOut == .5f && row->textType == -1, "deadend event preserves original color and pre-presentation defaults");
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--announce") return ScenarioAnnounceTableCases();
        if (argc == 5 && std::string_view(argv[1]) == "--actual") Actual(argv[2], argv[3], argv[4]);
        else if (argc == 1) { Contract(); TransactionFailures(); ResourceTransactions(); }
        else throw std::runtime_error("usage: [--actual encoded decoded expected]");
    }
    catch (const std::exception& e) { ++failures; std::cerr << e.what() << '\n'; }
    std::cout << "scenario_message_table checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
