// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual data-layer library. Actual-resource expectations are
// independently generated with Python csv and kept outside the source tree.
#include "config.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

using namespace csol::scenario;

namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}

bool Same(const CsvDocument& a, const CsvDocument& b)
{
    if (a.sourceBytes != b.sourceBytes || a.records.size() != b.records.size()) return false;
    for (std::size_t i = 0; i < a.records.size(); ++i)
        if (a.records[i].byteOffset != b.records[i].byteOffset || a.records[i].fields != b.records[i].fields)
            return false;
    return true;
}
bool Same(const WaveTable& a, const WaveTable& b)
{
    if (!Same(a.source, b.source) || a.groups != b.groups || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i)
    {
        const auto& x = a.rows[i]; const auto& y = b.rows[i];
        if (x.recordIndex != y.recordIndex || x.waveId != y.waveId || x.spawnPoint != y.spawnPoint ||
            x.startTime != y.startTime || x.endTime != y.endTime || x.interval != y.interval ||
            x.types != y.types || x.weights != y.weights || x.weightTotal != y.weightTotal ||
            x.eventMessage != y.eventMessage) return false;
    }
    return true;
}
bool Same(const RoundTable& a, const RoundTable& b)
{
    if (!Same(a.source, b.source) || a.headerRecord != b.headerRecord || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i)
        if (a.rows[i].recordIndex != b.rows[i].recordIndex || a.rows[i].round != b.rows[i].round ||
            a.rows[i].waves != b.rows[i].waves) return false;
    return true;
}

const std::string WaveHeader = "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\r\n";

void CsvTests()
{
    ConfigError error{999, "old"};
    CsvDocument document;
    const std::string korean = "\xb6\xf3\xbf\xee\xb5\xe5\xc1\xa6";
    const std::string source = "\xef\xbb\xbf" "h,h,\r\n\"" + korean + "\r\nx,\"\"y\"\"\",,\r\n\r\nlast,";
    Check(ReadCsv(source, document, error), "CSV CP949 quoted CRLF input");
    Check(error.byteOffset == 0 && error.message.empty(), "CSV success resets error");
    Check(document.sourceBytes == source, "CSV retains exact original bytes and BOM");
    Check(document.records.size() == 4, "CSV blank record and final comma retained");
    if (document.records.size() != 4) return;
    Check(document.records[0].byteOffset == 3 && document.records[0].fields == std::vector<std::string>{"h", "h", ""}, "CSV duplicate and empty headers preserved");
    Check(document.records[1].byteOffset == 9 && document.records[1].fields == std::vector<std::string>{korean + "\r\nx,\"y\"", "", ""}, "CSV raw CP949, quote, comma and embedded newline");
    Check(document.records[2].fields == std::vector<std::string>{""}, "CSV blank row preserved");
    Check(document.records[3].byteOffset == source.find("last,") && document.records[3].fields == std::vector<std::string>{"last", ""}, "CSV final empty field without newline");
    const auto previous = document;
    const std::vector<std::pair<std::string, std::size_t>> malformed = {
        {"", 0}, {"ok\r\na\"b", 5}, {"\"a\"x", 3}, {"\"a", 2},
        {std::string("ab\0cd", 5), 2}, {"valid,row\n\"unterminated", 23}
    };
    for (const auto& [bytes, offset] : malformed)
    {
        Check(!ReadCsv(bytes, document, error), "CSV rejects malformed input");
        Check(error.byteOffset == offset && !error.message.empty(), "CSV precise failure byte offset");
        Check(Same(document, previous), "CSV failed import preserves complete prior output");
    }
    Check(ReadCsv("a\rb\nc\r\n", document, error), "CSV accepts CR LF CRLF record separators");
    Check(document.records.size() == 3 && document.records[1].byteOffset == 2 && document.records[2].byteOffset == 4, "CSV mixed separator offsets");
    Check(ReadCsv(std::string(65536, 'x'), document, error), "CSV accepts max field length");
    auto boundary = document;
    Check(!ReadCsv(std::string(65537, 'x'), document, error) && error.byteOffset == 65536, "CSV rejects excessive unquoted field");
    Check(Same(document, boundary), "CSV field limit failure is transactional");
    Check(ReadCsv('"' + std::string(65536, 'x') + '"', document, error), "CSV accepts max quoted field length");
    boundary = document;
    Check(!ReadCsv('"' + std::string(65537, 'x') + '"', document, error) && error.byteOffset == 65537, "CSV rejects excessive quoted field");
    Check(Same(document, boundary), "CSV quoted limit failure is transactional");
    Check(ReadCsv(std::string(255, ','), document, error) && document.records[0].fields.size() == 256, "CSV accepts 256 columns");
    boundary = document;
    Check(!ReadCsv(std::string(256, ','), document, error), "CSV rejects 257 columns");
    Check(Same(document, boundary), "CSV column limit failure is transactional");
    Check(ReadCsv(std::string(100000, '\n'), document, error) && document.records.size() == 100000, "CSV accepts 100000 blank records");
    boundary = document;
    Check(!ReadCsv(std::string(100001, '\n'), document, error) && error.byteOffset == 100000, "CSV rejects 100001 records");
    Check(Same(document, boundary), "CSV record limit failure is transactional");
    Check(!ReadCsv(std::string(8388609, 'x'), document, error) && error.byteOffset == 0, "CSV rejects excessive file size");
    Check(Same(document, boundary), "CSV file limit failure is transactional");
    Check(ReadCsv("ok,\r\n", document, error) && error.message.empty(), "CSV recovers after failure");
    boundary = document;
    Check(ReadCsv(document.sourceBytes, document, error) && Same(document, boundary), "CSV can reload its own retained bytes");
}

void WaveTests()
{
    ConfigError error;
    WaveTable table;
    const std::string source = "metadata,\xb6\xf3\r\n"
        "waveID,eventmessage,zombiRatio,zombi,intervalTime,endTime,startTime,spawnPoint\r\n"
        "7,\"message,\"\"quoted\"\"\",127 1,40 2,32767,+170,-2,13\r\n"
        "7,,-128 -1,6 7,0,0,0,13\r\n"
        "9,0,1,9999,0,0,0,9999\r\n\r\n";
    Check(ReadWaves(source, table, error), "waves support named column order and metadata");
    Check(table.rows.size() == 3 && table.source.sourceBytes == source, "waves retain all nonblank rows and source");
    if (table.rows.size() != 3) return;
    Check(table.rows[0].recordIndex == 2 && table.rows[0].startTime == -2 && table.rows[0].endTime == 170 && table.rows[0].interval == 32767, "wave integer boundaries and record identity");
    Check(table.rows[0].types == std::vector<int>{40, 2} && table.rows[0].weights == std::vector<std::int8_t>{127, 1} && table.rows[0].weightTotal == -128, "wave weight sum uses original signed byte width");
    Check(table.rows[1].weightTotal == 127 && table.rows[0].eventMessage == "message,\"quoted\"", "wave negative weight wrapping and quoted event bytes");
    Check(table.groups.at(7).at(13) == std::vector<std::size_t>{0, 1}, "wave duplicate point rows retained in order");
    Check(table.rows[2].IsEvent() && !table.rows[0].IsEvent(), "point 9999 is an event");
    const auto previous = table;
    std::vector<std::string> bad = {
        "", "1,2\n", "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio\n",
        "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage,waveID\n",
        WaveHeader + "1,2,0,1,5,1\n",
        WaveHeader + "1,2,0,1,5,1,1,\n2,3,bad,1,5,1,1,\n"
    };
    for (const auto& row : {
        "x,2,0,1,5,1,1,", "1,2147483648,0,1,5,1,1,", "1,2,-2147483649,1,5,1,1,",
        "1,2,+-1,1,5,1,1,", "1,2,0,1,-1,1,1,", "1,2,0,1,32768,1,1,",
        "1,2,0,1,5,+-1,1,", "1,2,0,1,5,1,+-1,", "1,2,0,1,5,1 2,1,",
        "1,2,0,1,5,,1,", "1,2,0,1,5,1,,", "1,2,0,1,5,1,128,", "1,2,0,1,5,1,-129,",
        "1,2,0,1,5,1.0,1,", "1,2,0,1,5,1,+,", "1,2,0,1,5,1,--1,"})
        bad.push_back(WaveHeader + row + '\n');
    for (const auto& bytes : bad)
    {
        Check(!ReadWaves(bytes, table, error) && !error.message.empty(), "waves reject invalid input");
        Check(Same(table, previous), "waves failure preserves source, rows and index transactionally");
    }
    Check(ReadWaves(WaveHeader + "+1,+2,-2147483648,2147483647,0,+1,+1,\n", table, error), "waves valid signed bounds and empty event column");
    Check(table.rows.size() == 1 && table.rows[0].eventMessage.empty(), "explicit empty event retained");
}

void TimeAndShortRowTests()
{
    ConfigError error; WaveTable table;
    const std::string source = WaveHeader + "1,2,5.5,10.6, ,40,1\n"
        "2,9999,0.1,0,0,9999,1,49\n"
        "3,4,-2.7,+3.9,7.9,1,1,\n"
        "4,5,, \t,\t,1,1\n"
        "5,6,2147483647.9,-2147483648.9,32767.9,1,1\n"
        "6,7,1e5,0x40,-0.9,1,1,\n";
    Check(ReadWaves(source, table, error), "real resource decimal times blank interval and omitted event parse");
    Check(table.source.sourceBytes == source && table.rows.size() == 6, "compatible time import retains complete source bytes and all rows");
    if (table.rows.size() != 6) return;
    const int starts[]{5, 0, -2, 0, 2147483647, 1};
    const int ends[]{10, 0, 3, 0, (-2147483647 - 1), 0};
    const int intervals[]{0, 0, 7, 0, 32767, 0};
    for (std::size_t i = 0; i < table.rows.size(); ++i)
        Check(table.rows[i].startTime == starts[i] && table.rows[i].endTime == ends[i] && table.rows[i].interval == intervals[i], "time conversion follows integer prefix without floating rounding");
    Check(table.source.records[1].fields.size() == 7 && table.source.records[1].fields[2] == "5.5" && table.source.records[1].fields[4] == " ", "short record column count decimal token and blank field remain exact");
    Check(table.rows[0].eventMessage.empty() && table.rows[1].eventMessage == "49" && table.rows[1].IsEvent(), "omitted ordinary event and explicit event value stay distinct in source");
    const auto previous = table;
    for (const auto& values : {"2147483648.0,0,0", "-2147483649.1,0,0", "0,2147483648,0",
        "0,0,32768.1", "0,0,-1.1", "nan,0,0", "inf,0,0", ".5,0,0", "+,0,0", "--1,0,0", "+-1,0,0"})
    {
        Check(!ReadWaves(WaveHeader + "1,2," + values + ",1,1\n", table, error) && !error.message.empty(), "bad or unsafe time prefix rejected");
        Check(Same(table, previous), "failed time import retains all previous source rows and groups");
    }
    const std::string reordered = "waveID,zombi,spawnPoint,endTime,zombiRatio,startTime,intervalTime,eventmessage\n9,40,13,12.5,1,4.9, \n";
    Check(ReadWaves(reordered, table, error) && table.rows.size() == 1, "reordered columns allow missing final event only");
    Check(table.rows[0].startTime == 4 && table.rows[0].endTime == 12 && table.rows[0].interval == 0 && table.rows[0].eventMessage.empty(), "reordered named times map correctly");
    Check(table.source.sourceBytes == reordered && table.source.records[1].fields.size() == 7, "reordered short raw record stays unchanged");
    const auto reorderedBefore = table;
    Check(!ReadWaves("waveID,eventmessage,startTime,endTime,intervalTime,zombi,zombiRatio,spawnPoint\n1,,0,1,5,1,1\n", table, error) && Same(table, reorderedBefore), "reordered missing required final column still rejected transactionally");
    for (const auto& row : {"1.5,2,0,1,5,1,1", "1,2.5,0,1,5,1,1", "1,2,0,1,5,1.5,1", "1,2,0,1,5,1,1.5"})
        Check(!ReadWaves(WaveHeader + row + '\n', table, error) && Same(table, reorderedBefore), "time compatibility does not relax IDs types or weights");
}

void RoundAndReferenceTests()
{
    ConfigError error;
    RoundTable rounds;
    const std::string source = "wall,0\nwall,1\nround,waveTypes,unused\n+1,9 7 404 7 404,x\n1,7,\n";
    Check(ReadRounds(source, rounds, error), "round metadata and duplicate round IDs accepted");
    Check(rounds.headerRecord == 2 && rounds.rows.size() == 2 && rounds.source.sourceBytes == source, "round metadata retained without flattening");
    Check(rounds.rows[0].waves == std::vector<int>{9, 7, 404, 7, 404}, "round original wave order and repeated references retained");
    const auto previous = rounds;
    for (const auto& bytes : std::vector<std::string>{"", "round,other\n", "round,waveTypes,waveTypes\n",
        "round,waveTypes\n1\n", "round,waveTypes\n1,\n", "round,waveTypes\n+-1,2\n",
        "round,waveTypes\n1,+-1\n", "round,waveTypes\n1,2147483648\n",
        "round,waveTypes\n1,1\n2,invalid\n", "round,waveTypes\n1,1 2a\n"})
    {
        Check(!ReadRounds(bytes, rounds, error) && !error.message.empty(), "rounds reject malformed input");
        Check(Same(rounds, previous), "round failure preserves full old output");
    }
    WaveTable waves;
    Check(ReadWaves(WaveHeader + "7,13,0,170,5,1,1,\n7,13,0,170,5,2,1,\n9,9999,0,0,0,9999,1,event\n", waves, error), "reference fixture imports");
    SpawnPoints points;
    auto missing = FindMissingReferences(rounds, waves, points, 0);
    Check(missing.size() == 8, "missing references retain each duplicate use and exclude events");
    if (missing.size() == 8)
    {
        Check(missing[0].kind == MissingReferenceKind::MonsterPoint && missing[0].waveRecord == 1 && missing[1].waveRecord == 2, "missing point rows retain individual provenance");
        Check(missing[2].kind == MissingReferenceKind::Wave && missing[2].wave == 404 && missing[2].waveRecord == std::numeric_limits<std::size_t>::max(), "missing whole wave has absent-record sentinel");
        Check(missing[6].roundRecord == 4 && missing[6].round == 1, "duplicate round records retain source location");
    }
    SpawnPoint point; point.team = 1; point.id = 13;
    points.RegisterMonsterPoint(point, 0);
    Check(FindMissingReferences(rounds, waves, points, 0).size() == 8, "other team point does not satisfy a reference");
    point.team = 0; points.RegisterMonsterPoint(point, 0);
    missing = FindMissingReferences(rounds, waves, points, 0);
    Check(missing.size() == 2 && missing[0].wave == 404 && missing[1].wave == 404, "valid point resolves every use; missing waves remain separate");
}

class Reader
{
    std::string bytes_;
    std::size_t offset_{};
public:
    explicit Reader(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open actual-resource fixture");
        bytes_.assign(std::istreambuf_iterator<char>(file), {});
    }
    std::uint32_t U32()
    {
        if (bytes_.size() - offset_ < 4) throw std::runtime_error("truncated oracle integer");
        std::uint32_t value{};
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[offset_++])) << shift;
        return value;
    }
    int Int() { return std::bit_cast<std::int32_t>(U32()); }
    float Float() { return std::bit_cast<float>(U32()); }
    std::string String()
    {
        const auto size = U32();
        if (size > bytes_.size() - offset_) throw std::runtime_error("truncated oracle string");
        auto value = bytes_.substr(offset_, size); offset_ += size; return value;
    }
    std::vector<int> Integers()
    {
        auto size = U32(); std::vector<int> result;
        while (size--) result.push_back(Int());
        return result;
    }
    CsvDocument Document()
    {
        CsvDocument value; value.sourceBytes = String(); auto count = U32();
        while (count--)
        {
            CsvRecord row; row.byteOffset = U32(); auto columns = U32();
            while (columns--) row.fields.push_back(String());
            value.records.push_back(std::move(row));
        }
        return value;
    }
    bool AtEnd() const { return offset_ == bytes_.size(); }
};

void CompareDocument(const CsvDocument& actual, const CsvDocument& expected)
{
    Check(actual.sourceBytes == expected.sourceBytes, "actual CSV original bytes");
    Check(actual.records.size() == expected.records.size(), "actual CSV all source records");
    for (std::size_t i = 0; i < std::min(actual.records.size(), expected.records.size()); ++i)
    {
        Check(actual.records[i].byteOffset == expected.records[i].byteOffset, "actual CSV record byte offset");
        Check(actual.records[i].fields == expected.records[i].fields, "actual CSV all exact field bytes");
    }
}

void ActualTests(const char* path)
{
    Reader input(path);
    Check(input.String() == "CSOL-SCENARIO-FIXTURE-1", "external oracle format");
    const auto waveDocument = input.Document(); const auto roundDocument = input.Document();
    ConfigError error; WaveTable waves; RoundTable rounds;
    Check(ReadWaves(waveDocument.sourceBytes, waves, error), "actual decoded waves import");
    Check(ReadRounds(roundDocument.sourceBytes, rounds, error), "actual decoded rounds import");
    CompareDocument(waves.source, waveDocument); CompareDocument(rounds.source, roundDocument);
    auto count = input.U32(); Check(count == 218 && waves.rows.size() == count, "actual 218 wave rows");
    std::map<int, std::map<int, std::vector<std::size_t>>> expectedGroups;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto record = input.U32(); const int wave = input.Int(), point = input.Int();
        const int start = input.Int(), end = input.Int(), interval = input.Int();
        const auto types = input.Integers(), weights = input.Integers();
        const int total = input.Int(); const auto event = input.String();
        expectedGroups[wave][point].push_back(i);
        if (i >= waves.rows.size()) continue;
        const auto& row = waves.rows[i];
        Check(row.recordIndex == record && row.waveId == wave && row.spawnPoint == point, "actual wave identity and source order");
        Check(row.startTime == start && row.endTime == end && row.interval == interval, "actual wave times");
        Check(row.types == types && std::vector<int>(row.weights.begin(), row.weights.end()) == weights && row.weightTotal == total, "actual wave weighted monster lists");
        Check(row.eventMessage == event && row.IsEvent() == (point == 9999), "actual event bytes and event classification");
    }
    Check(waves.groups == expectedGroups && waves.groups.at(1).at(13).size() == 2, "actual point 13 duplicates preserved");
    const auto header = input.U32(); count = input.U32();
    Check(header == 6 && rounds.headerRecord == header && count == 5 && rounds.rows.size() == count, "actual six metadata records and five rounds");
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto record = input.U32(); const auto number = input.Int(); const auto list = input.Integers();
        if (i < rounds.rows.size())
            Check(rounds.rows[i].recordIndex == record && rounds.rows[i].round == number && rounds.rows[i].waves == list, "actual original round and wave order");
    }
    Check(rounds.rows[0].waves == std::vector<int>{2, 3, 19, 820, 821}, "actual first round reference order");
    const auto oldWaves = waves; const auto oldRounds = rounds;
    Check(!ReadWaves(waveDocument.sourceBytes + "invalid\n", waves, error) && Same(waves, oldWaves), "actual waves survive invalid next load unchanged");
    Check(!ReadRounds(roundDocument.sourceBytes + "invalid\n", rounds, error) && Same(rounds, oldRounds), "actual rounds survive invalid next load unchanged");
    auto mapCount = input.U32(); Check(mapCount == 7, "seven real season-one maps, no placeholder maps");
    std::size_t entities{}, monsterRecords{}, playerRecords{}; SpawnPoints lostcity;
    while (mapCount--)
    {
        const auto name = input.String(); entities += input.U32(); auto pointCount = input.U32();
        SpawnPoints points; std::map<std::size_t, SpawnPoint> originals;
        while (pointCount--)
        {
            const auto kind = input.U32(); SpawnPoint point; point.entityIndex = input.U32();
            point.team = input.Int(); point.id = input.Int(); const auto maximum = input.Int();
            for (auto& coordinate : point.origin) coordinate = input.Float();
            for (auto& coordinate : point.angles) coordinate = input.Float();
            originals[point.entityIndex] = point;
            if (kind) { points.AppendPlayerPoint(point); ++playerRecords; }
            else { points.RegisterMonsterPoint(point, maximum); ++monsterRecords; }
        }
        const auto monsterCount = input.U32();
        Check(points.Monsters().size() == monsterCount, "actual map monster group count");
        for (std::size_t i = 0; i < monsterCount; ++i)
        {
            const auto key = input.Int(); const auto entity = input.U32();
            auto found = points.Monsters().find(key);
            Check(found != points.Monsters().end() && found->second.point.entityIndex == entity, "actual map monster duplicate winner");
            if (found != points.Monsters().end())
            {
                const auto& expected = originals.at(entity); const auto& value = found->second.point;
                Check(value.origin == expected.origin && value.angles == expected.angles && value.team == expected.team && value.id == expected.id, "actual monster location and angles retained");
            }
        }
        const auto playerCount = input.U32();
        Check(points.Players().size() == playerCount, "actual map player group count");
        for (std::size_t i = 0; i < playerCount; ++i)
        {
            const auto key = input.Int(); const auto expected = input.Integers();
            auto found = points.Players().find(key);
            Check(found != points.Players().end() && found->second.size() == expected.size(), "actual player duplicates retained");
            if (found != points.Players().end())
                for (std::size_t j = 0; j < std::min(found->second.size(), expected.size()); ++j)
                {
                    const auto& value = found->second[j]; const auto& original = originals.at(expected[j]);
                    Check(value.entityIndex == static_cast<std::size_t>(expected[j]) && value.origin == original.origin && value.angles == original.angles && value.team == original.team && value.id == original.id, "actual player order and all positions retained");
                }
        }
        if (name == "zs_lostcity") lostcity = std::move(points);
    }
    Check(entities == 3234 && monsterRecords == 723 && playerRecords == 141, "seven-map entity and spawn inventory totals");
    const auto missing = FindMissingReferences(rounds, waves, lostcity, 0);
    count = input.U32(); Check(missing.size() == count, "actual complete missing-reference diagnostics");
    std::set<int> missingWaves, firstPoints;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto kind = input.U32(), rr = input.U32(), wr = input.U32();
        const auto number = input.Int(), wave = input.Int(), point = input.Int();
        if (i >= missing.size()) continue;
        const auto& row = missing[i];
        Check(static_cast<unsigned>(row.kind) == kind && row.roundRecord == rr && row.waveRecord == wr && row.round == number && row.wave == wave && row.spawnPoint == point, "actual missing-reference identity order and provenance");
        if (row.kind == MissingReferenceKind::Wave) missingWaves.insert(row.wave);
        else if (row.round == 1) firstPoints.insert(row.spawnPoint);
    }
    Check(missingWaves == std::set<int>{820, 821, 831, 832, 833, 841}, "six actual absent wave IDs diagnosed");
    Check(firstPoints == std::set<int>{36, 37}, "actual first-round missing points 36 and 37 diagnosed");
    Check(input.AtEnd(), "actual oracle consumed completely");
    std::cout << "actual inventory: 7 maps, 3234 entities, 723 monster points, 141 player points; missing waves 820 821 831 832 833 841; first-round missing points 36 37\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--actual") ActualTests(argv[2]);
        else if (argc == 1) { CsvTests(); WaveTests(); TimeAndShortRowTests(); RoundAndReferenceTests(); }
        else throw std::runtime_error("usage: config_tests [--actual external-fixture.bin]");
    }
    catch (const std::exception& error)
    {
        ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n';
    }
    std::cout << "config checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
