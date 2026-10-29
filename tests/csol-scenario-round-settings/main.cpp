// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/round_settings.hpp"
#include "../../native/csol/scenario/resources.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, std::string_view label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
bool Same(const RoundSettings& settings, unsigned music, int group)
{ return settings.music == music && settings.playerSpawn == group; }

std::string Csv(std::string_view rows)
{
    return "gametype,1\nroundTime,0\nwallBonusDmg,125\nwallBonusPoint,200\n"
        "wallexp,50\nRewardRound,0\n"
        "round,waveTypes,GoalKillCount,damageAdd,attackRangeAdd,healthAdd,armorAdd,"
        "speedAdd,IntervalAdd,moneyAdd,BGM,PlayerSpawn\n" + std::string(rows);
}
RoundTable Parse(std::string_view rows)
{
    RoundTable table; ConfigError error;
    if (!ReadRounds(Csv(rows), table, error)) throw std::runtime_error(error.message);
    return table;
}
RoundTable Table(std::string_view music = "5", std::string_view group = "3")
{
    return Parse("91,71,0,11,22,33,44,55,66,77," + std::string(music) + "," +
        std::string(group) + ",999\n92,72,0,11,22,33,44,55,66,77,6,4,888\n");
}
void PositionalFields()
{
    const auto table = Table(); RoundSettings output{211,-111}; ConfigError error{999,"stale"};
    Check(ReadRoundSettings(table,0,output,error) && Same(output,5,3),
        "column10/11 refer to physical first round, not round91/wave71/extra999");
    Check(error.byteOffset == 0 && error.message.empty(), "successful getter clears stale diagnostic");
    Check(ReadRoundSettings(table,1,output,error) && Same(output,6,4),
        "second physical round uses independent settings");

    SpawnPoints points;
    for (const auto& entry : std::array<std::array<int,2>,6>{{{0,100},{3,303},{4,404},{71,717},{91,919},{92,929}}})
    {
        SpawnPoint point; point.team=2; point.id=entry[0];
        point.entityIndex=static_cast<std::size_t>(entry[1]); points.AppendPlayerPoint(point);
    }
    for (std::size_t row=0; row<2; ++row)
    {
        Check(ReadRoundSettings(table,row,output,error), "settings feed real point registry");
        const auto* group=points.FindPlayerPoints(2,output.playerSpawn);
        const std::size_t expected=row==0?303:404;
        Check(group && group->size()==1 && group->front().entityIndex==expected,
            "production point registry follows PlayerSpawn, not round/wave/index/group0");
    }
    for (const auto& entry : std::array<std::pair<const char*,unsigned>,4>{{
        {"91,71\n",0},{"91,71,0,0,0,0,0,0,0,0\n",0},
        {"91,71,0,0,0,0,0,0,0,0,23\n",23},{"91,71,0,0,0,0,0,0,0,0,,\n",0}}})
    {
        const auto missing=Parse(entry.first); output={211,-111};
        Check(ReadRoundSettings(missing,0,output,error) && Same(output,entry.second,0),
            "missing/empty optional columns default to zero, without implicit round-group fallback");
    }
    auto renamed=Table();
    renamed.source.records[6].fields[10]="PlayerSpawn";
    renamed.source.records[6].fields[11]="BGM";
    Check(ReadRoundSettings(renamed,0,output,error) && Same(output,5,3),
        "original scalar getters use fixed positions even if optional header labels disagree");
}

void IntegerGetter()
{
    struct Lexeme { const char* text; unsigned byte; int word; };
    // Expectations are fixed examples from atoi-prefix semantics and the
    // original byte/word stores; no second parser mirrors the implementation.
    constexpr Lexeme cases[]{
        {"",0,0},{" \t\v\f",0,0},{"abc",0,0},{"+",0,0},{"-",0,0},
        {"+-1",0,0},{"-+1",0,0},{" +x",0,0},{"+ 2",0,0},{"--2",0,0},
        {"0x10",0,0},{"0123e9",123,123},{"1.9",1,1},{"1e309",1,1},
        {"nan",0,0},{"infinity",0,0},{"09",9,9},{"\t +12tail",12,12},
        {" -12tail",244,-12},{"127",127,127},{"128",128,128},
        {"255",255,255},{"256",0,256},{"257",1,257},{"-1",255,-1},
        {"-257",255,-257},{"32767",255,32767},{"32768",0,-32768},
        {"65535",255,-1},{"65536",0,0},{"65537tail",1,1},
        {"-32769suffix",255,32767},{"+65535tail",255,-1},
        {"2147483647",255,-1},{"-2147483648",0,0},
        {"0000000000000000000000000000012tail",12,12}
    };
    for (const auto& c : cases)
    {
        const auto music=Table(c.text,"3"); RoundSettings output{211,-111}; ConfigError error;
        Check(ReadRoundSettings(music,0,output,error) && Same(output,c.byte,3),
            std::string("byte getter: ")+c.text);
        const auto spawn=Table("5",c.text);
        Check(ReadRoundSettings(spawn,0,output,error) && Same(output,5,c.word),
            std::string("signed short getter: ")+c.text);
    }
    // Newlines are CSV record delimiters; test all six original leading ASCII
    // whitespace characters through an explicitly quoted cell instead.
    for (const char whitespace : std::array<char,6>{' ','\t','\v','\f','\r','\n'})
    {
        const std::string cell="\""+std::string(1,whitespace)+"+65535rest\"";
        auto table=Table(cell,cell); RoundSettings output; ConfigError error;
        Check(ReadRoundSettings(table,0,output,error) && Same(output,255,-1),
            "quoted CSV cell retains original integer getter leading whitespace");
    }
}

void Rejections()
{
    constexpr std::array<const char*,6> overflows{
        "2147483648","-2147483649","+2147483648","99999999999999999999999tail",
        "-99999999999999999999999tail","  2147483648suffix"};
    for (const char* text : overflows)
    for (int column=10; column<=11; ++column)
    {
        auto table=column==10?Table(text,"3"):Table("5",text);
        RoundSettings output{211,-111}; ConfigError error{999,"stale"};
        Check(!ReadRoundSettings(table,0,output,error) && Same(output,211,-111),
            "int32 overflow rejects entire result without publishing even the valid first field");
        Check(error.byteOffset==table.source.records[7].byteOffset && !error.message.empty(),
            "numeric failure reports the actual source record byte offset");
        Check(ReadRoundSettings(table,1,output,error) && Same(output,6,4) &&
            error.byteOffset==0 && error.message.empty(), "valid next row clears prior failed-read diagnostic");
    }
    for (int invalid=0; invalid<8; ++invalid)
    {
        auto table=Table(); std::size_t row=0;
        if (invalid==0) table.headerRecord=5;
        if (invalid==1) row=2;
        if (invalid==2) row=std::numeric_limits<std::size_t>::max();
        if (invalid==3) table.rows[0].recordIndex=8;
        if (invalid==4) table.rows[1].recordIndex=7, row=1;
        if (invalid==5) table.source.records.resize(7);
        if (invalid==6) table.rows.clear();
        if (invalid==7) table.source.records.clear();
        RoundSettings output{211,-111}; ConfigError error{999,"stale"};
        Check(!ReadRoundSettings(table,row,output,error) && Same(output,211,-111) &&
            error.byteOffset==0 && !error.message.empty(),
            "invalid round/source identity is rejected transactionally");
    }
}

void RewardRoundMetadata()
{
    struct Example { const char* text; std::int32_t value; };
    constexpr Example values[]{{" ",0},{"0",0},{"2",2},{"-1",-1},{"  +7tail",7},
        {"abc",0},{"1.9",1},{"65536",65536},{"2147483647",INT32_MAX},{"-2147483648",INT32_MIN}};
    for (const auto& example : values)
    {
        auto text = Csv("91,71\n");
        text.replace(text.find("RewardRound,0"), std::string("RewardRound,0").size(),
            std::string("RewardRound,") + example.text);
        RoundTable table; ConfigError error;
        Check(ReadRounds(text,table,error), "reward metadata sample passes the real round CSV parser");
        std::int32_t output = 987; error = {99,"stale"};
        Check(ReadRewardRound(table,output,error) && output == example.value && error.byteOffset == 0 && error.message.empty(),
            "reward round reads signed int32 metadata row5/column1 with original numeric-prefix semantics");
    }
    auto table = Table();
    table.source.records[5].fields = {"renamed", "17"};
    std::int32_t output = -8; ConfigError error;
    Check(ReadRewardRound(table,output,error) && output == 17,
        "reward metadata uses physical row and column rather than a label search or current round index");
    for (bool missing : {false,true})
    {
        table.source.records[5].fields = {"RewardRound",""};
        if (missing) table.source.records[5].fields.resize(1);
        output = -8; error = {99,"stale"};
        Check(!ReadRewardRound(table,output,error) && output == -8 &&
            error.byteOffset == table.source.records[5].byteOffset && !error.message.empty(),
            "missing or empty reward cell rejects without inventing an original getter result");
    }
    for (const char* overflow : {"2147483648","-2147483649","999999999999999999999"})
    {
        table.source.records[5].fields = {"RewardRound",overflow}; output = -8; error = {99,"stale"};
        Check(!ReadRewardRound(table,output,error) && output == -8 &&
            error.byteOffset == table.source.records[5].byteOffset && !error.message.empty(),
            "reward overflow rejects without publishing a narrowed or partial output");
    }
    for (bool wrongHeader : {false,true})
    {
        auto invalid = Table();
        if (wrongHeader) invalid.headerRecord = 5; else invalid.source.records.resize(5);
        output = -8; error = {99,"stale"};
        Check(!ReadRewardRound(invalid,output,error) && output == -8 && error.byteOffset == 0 && !error.message.empty(),
            "missing metadata identity fails before reading a nonexistent row");
    }
}

std::string File(const char* path)
{
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error(std::string("cannot read input: ")+path);
    return {std::istreambuf_iterator<char>(stream),{}};
}
int Actual(const char* encryptedPath,const char* decodedPath)
{
    const auto encoded=File(encryptedPath), decoded=File(decodedPath);
    RoundTable encryptedTable, decodedTable; ConfigError error;
    const bool encryptedOk=ReadRoundResource(std::as_bytes(std::span(encoded.data(),encoded.size())),encryptedTable,error);
    Check(encryptedOk,"production encrypted round resource reader accepts actual common input");
    const bool decodedOk=ReadRounds(decoded,decodedTable,error);
    Check(decodedOk,"production round parser accepts independently saved prior decoded input");
    if (!encryptedOk || !decodedOk) return 1;
    Check(encryptedTable.source.sourceBytes==decodedTable.source.sourceBytes,
        "common decoding agrees byte-for-byte with independently saved decoded resource");
    for (const auto* table : {&encryptedTable,&decodedTable})
    {
        Check(table->headerRecord==6 && table->rows.size()==2,"actual table has two rounds after physical header6");
        for (std::size_t row=0; row<2; ++row)
        {
            if (row>=table->rows.size()) { Check(false,"actual row unavailable"); continue; }
            RoundSettings output{211,-111};
            Check(table->rows[row].recordIndex==row+7,"actual round source row provenance");
            Check(table->rows[row].round==static_cast<int>(row+1),"actual round number1/2");
            Check(table->rows[row].waves==std::vector<int>{row==0?1:0},"actual wave groups1/0 differ from spawn groups1/2");
            Check(ReadRoundSettings(*table,row,output,error) && Same(output,row==0?5:6,row==0?1:2),
                "actual BGM/PlayerSpawn are exactly5/1 and6/2");
            std::cout<<"actual row="<<row<<" round="<<table->rows[row].round<<" music="
                <<static_cast<unsigned>(output.music)<<" playerSpawn="<<output.playerSpawn<<'\n';
        }
    }
    Check(encoded==File(encryptedPath) && decoded==File(decodedPath),"both external resource inputs remain unchanged");
    std::cout<<"round_settings actual checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
}
int main(int argc,char** argv)
{
    try
    {
        if (argc==4 && std::string_view(argv[1])=="--actual") return Actual(argv[2],argv[3]);
        if (argc!=1) throw std::runtime_error("usage: no arguments, or --actual encrypted.cso decoded.csv");
        PositionalFields(); IntegerGetter(); Rejections(); RewardRoundMetadata();
    }
    catch (const std::exception& error) { std::cerr<<"EXCEPTION: "<<error.what()<<'\n'; return 2; }
    std::cout<<"round_settings checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
