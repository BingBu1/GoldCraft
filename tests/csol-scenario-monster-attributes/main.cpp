// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/monster_attributes.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, std::string_view label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
std::array<std::int16_t, 7> Values(const RoundMonsterDeltas& d)
{ return {d.damage, d.range, d.health, d.armor, d.speed, d.interval, d.money}; }
std::array<std::uint32_t, 7> Bits(const MonsterAttributes& a)
{
    return {std::bit_cast<std::uint32_t>(a.health), std::bit_cast<std::uint32_t>(a.armor),
        std::bit_cast<std::uint32_t>(a.attackDamage), std::bit_cast<std::uint32_t>(a.attackRange),
        std::bit_cast<std::uint32_t>(a.attackInterval), std::bit_cast<std::uint32_t>(a.speed),
        std::bit_cast<std::uint32_t>(a.bonusMoney)};
}
RoundTable Table()
{
    RoundTable table; table.headerRecord = 6; table.source.records.resize(10);
    table.rows = {{7, 91, {}}, {8, -4, {}}};
    table.source.records[7] = {123, {"91", "wave", "unused", "1", "2", "3", "4", "5", "6", "7"}};
    table.source.records[8] = {456, {"-4", "wave", "unused", "8", "9", "10", "11", "12", "13", "14"}};
    table.source.records[9] = {789, {"999", "wave", "unused", "99"}};
    return table;
}
void RoundFields()
{
    auto table = Table(); ConfigError error{999, "stale"}; RoundMonsterDeltas deltas;
    Check(ReadRoundMonsterDeltas(table, 0, deltas, error) && Values(deltas) == std::array<std::int16_t, 7>{1,2,3,4,5,6,7},
        "fixed columns 3..9 use physical source row seven, not its arbitrary round number");
    Check(error.byteOffset == 0 && error.message.empty(), "successful read clears previous diagnostic");
    Check(ReadRoundMonsterDeltas(table, 1, deltas, error) && Values(deltas) == std::array<std::int16_t, 7>{8,9,10,11,12,13,14},
        "second logical row reads physical row eight");
    struct Lexeme { const char* text; std::int16_t expected; };
    const Lexeme lexemes[] = {{"",0}, {" \t\v\f\r\n",0}, {"abc",0}, {"+",0}, {"-",0},
        {" +x",0}, {"-+1",0}, {"+-1",0}, {"+ 2",0}, {"0x10",0}, {"0123e9",123},
        {"-32768",-32768}, {"32767",32767}, {"32768",-32768}, {"65535",-1}, {"65536",0},
        {"65537tail",1}, {"-32769suffix",32767}, {"\t+65535tail",-1},
        {"2147483647",-1}, {"-2147483648",0}, {"\r\n-12tail",-12}};
    for (std::size_t column = 3; column <= 9; ++column)
    for (const auto& lexical : lexemes)
    {
        auto input = Table(); input.source.records[7].fields[column] = lexical.text;
        std::array<std::int16_t, 7> wanted{1,2,3,4,5,6,7}; wanted[column-3] = lexical.expected;
        Check(ReadRoundMonsterDeltas(input, 0, deltas, error) && Values(deltas) == wanted,
            "integer getter prefix and low16 storage apply independently to each delta column");
    }
    for (std::size_t length = 0; length <= 9; ++length)
    {
        auto input = Table(); input.source.records[7].fields.resize(length);
        std::array<std::int16_t, 7> wanted{1,2,3,4,5,6,7};
        for (std::size_t i = 0; i < wanted.size(); ++i) if (i + 3 >= length) wanted[i] = 0;
        Check(ReadRoundMonsterDeltas(input, 0, deltas, error) && Values(deltas) == wanted,
            "missing delta cells become zero without borrowing later source fields");
    }
    for (std::size_t column = 3; column <= 9; ++column)
    for (const char* overflow : {"2147483648", "-2147483649", "+99999999999999999999999tail"})
    {
        auto input = Table(); input.source.records[7].fields[column] = overflow;
        deltas = {11,22,33,44,55,66,77}; const auto before = Values(deltas);
        Check(!ReadRoundMonsterDeltas(input, 0, deltas, error) && Values(deltas) == before &&
            error.byteOffset == 123 && !error.message.empty(),
            "local int32-overflow rejection preserves all prior deltas and reports physical byte offset");
    }
    for (int invalid = 0; invalid < 6; ++invalid)
    {
        auto input = Table(); std::size_t index = 0;
        if (invalid == 0) input.headerRecord = 0;
        if (invalid == 1) index = 2;
        if (invalid == 2) input.rows[0].recordIndex = 8;
        if (invalid == 3) input.rows[1].recordIndex = 7, index = 1;
        if (invalid == 4) input.source.records.resize(7);
        if (invalid == 5) input.rows.clear();
        deltas = {11,22,33,44,55,66,77}; const auto before = Values(deltas);
        Check(!ReadRoundMonsterDeltas(input, index, deltas, error) && Values(deltas) == before && !error.message.empty(),
            "invalid header source identity or logical index cannot reuse another physical row");
    }
}
MonsterType Base()
{
    MonsterType type; type.health = 50; type.armor = 40; type.attackDamage = 10; type.attackRange = 60;
    type.attackInterval = 3; type.speed = 240; type.bonusMoney = 300; return type;
}
void Arithmetic()
{
    auto type = Base(); RoundMonsterDeltas delta{}; MonsterAttributes output;
    ConfigError error{999,"stale"};
    Check(ResolveMonsterAttributes(type, delta, output, error) &&
        Bits(output) == Bits({50,40,10,60,3,240,300}) && error.byteOffset == 0 && error.message.empty(),
        "base values are resolved without extra multipliers and clear stale diagnostics");
    delta = {5,6,7,8,9,32767,10};
    Check(ResolveMonsterAttributes(type, delta, output, error) && Bits(output) == Bits({57,48,15,66,3,249,310}),
        "positive scalar deltas leave interval delta deliberately unused");
    delta = {-32768,-32768,-32768,-32768,-32768,-32768,-32768};
    Check(ResolveMonsterAttributes(type, delta, output, error) && Bits(output) == Bits({10,0,0,0,3,-32528,0}),
        "negative deltas apply floors except speed, which retains a negative signed result");
    type.health = INT32_MAX; type.armor = INT32_MAX; delta = {}; delta.health = delta.armor = 1;
    Check(ResolveMonsterAttributes(type, delta, output, error) && output.health == 10 && output.armor == 0,
        "health and armor positive overflow wrap to signed negative before their floors");
    type.health = INT32_MIN; type.armor = INT32_MIN; delta.health = delta.armor = -1;
    Check(ResolveMonsterAttributes(type, delta, output, error) && Bits(output)[0] == 0x4f000000u && Bits(output)[1] == 0x4f000000u,
        "negative overflow wraps to INT32_MAX then rounds to float32 2147483648");
    type = Base(); type.attackDamage = type.attackRange = type.speed = type.bonusMoney = 32767;
    delta = {32767,32767,0,0,32767,0,32767};
    Check(ResolveMonsterAttributes(type, delta, output, error) && Bits(output) == Bits({50,40,65534,65534,3,390,65534}),
        "signed-short sums promote to int32; only speed has the 390 ceiling");
    for (const float interval : {-100.0f, -0.0f, 0.0f, 0.25f, 0.4f, 0.5f, 3.0f})
    {
        type = Base(); type.attackInterval = interval; delta = {}; delta.interval = 32767;
        const float wanted = interval < 0.4f ? 0.4f : interval;
        Check(ResolveMonsterAttributes(type, delta, output, error) &&
            std::bit_cast<std::uint32_t>(output.attackInterval) == std::bit_cast<std::uint32_t>(wanted),
            "finite base interval uses original float32 floor without round interval addition");
    }
    for (const std::uint32_t bits : {0x7f800000u,0xff800000u,0x7fc00001u,0xffc00001u})
    {
        type = Base(); type.attackInterval = std::bit_cast<float>(bits);
        output = {1,2,3,4,5,6,7}; const auto before = Bits(output);
        Check(!ResolveMonsterAttributes(type, delta, output, error) && Bits(output) == before && !error.message.empty(),
            "nonfinite interval is an explicit local rejection with full prior output preservation");
    }
}
int Oracle()
{
    std::array<std::int64_t,14> raw{};
    while (std::cin >> raw[0])
    {
        for (std::size_t i=1; i<raw.size(); ++i) if (!(std::cin >> raw[i])) return 2;
        MonsterType type; RoundMonsterDeltas delta; MonsterAttributes output{1,2,3,4,5,6,7}; ConfigError error;
        type.health=static_cast<std::int32_t>(raw[0]); type.armor=static_cast<std::int32_t>(raw[1]);
        type.attackDamage=static_cast<std::int16_t>(raw[2]); type.attackRange=static_cast<std::int16_t>(raw[3]);
        type.attackInterval=std::bit_cast<float>(static_cast<std::uint32_t>(raw[4]));
        type.speed=static_cast<std::int16_t>(raw[5]); type.bonusMoney=static_cast<std::int16_t>(raw[6]);
        delta = {static_cast<std::int16_t>(raw[7]),static_cast<std::int16_t>(raw[8]),static_cast<std::int16_t>(raw[9]),
            static_cast<std::int16_t>(raw[10]),static_cast<std::int16_t>(raw[11]),static_cast<std::int16_t>(raw[12]),static_cast<std::int16_t>(raw[13])};
        std::cout << ResolveMonsterAttributes(type,delta,output,error);
        for (auto value:Bits(output)) std::cout << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof()?0:2;
}
std::string File(const char* path)
{
    std::ifstream stream(path,std::ios::binary); if(!stream) throw std::runtime_error("fixture file unavailable");
    return {std::istreambuf_iterator<char>(stream),{}};
}
int Actual(const char* typesPath, const char* roundsPath)
{
    MonsterTypeTable types; RoundTable rounds; ConfigError error;
    if(!ReadMonsterTypes(File(typesPath),types,error) || !ReadRounds(File(roundsPath),rounds,error))
        throw std::runtime_error(error.message);
    std::cout << "tables " << types.rows.size() << ' ' << rounds.rows.size() << ' ' << rounds.headerRecord << '\n';
    for(std::size_t i=0; i<rounds.rows.size(); ++i)
    {
        RoundMonsterDeltas delta;
        if(!ReadRoundMonsterDeltas(rounds,i,delta,error)) throw std::runtime_error(error.message);
        std::cout << "round " << i << ' ' << rounds.rows[i].recordIndex << ' ' << rounds.rows[i].round;
        for(auto value:Values(delta)) std::cout << ' ' << value;
        std::cout << '\n';
        for(const auto& type:types.rows)
        {
            MonsterAttributes output;
            if(!ResolveMonsterAttributes(type,delta,output,error)) throw std::runtime_error(error.message);
            std::cout << "value " << i << ' ' << type.id;
            for(auto value:Bits(output)) std::cout << ' ' << value;
            std::cout << '\n';
        }
    }
    return 0;
}
}
int main(int argc,char**argv)
{
    try
    {
        if(argc==2 && std::string_view(argv[1])=="--oracle") return Oracle();
        if(argc==4 && std::string_view(argv[1])=="--actual") return Actual(argv[2],argv[3]);
        RoundFields(); Arithmetic();
    }
    catch(const std::exception& error) {std::cerr<<"EXCEPTION: "<<error.what()<<'\n'; return 2;}
    std::cout<<"monster_attributes checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
