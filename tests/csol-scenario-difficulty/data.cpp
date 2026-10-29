// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.hpp"
#include "actual_expected.hpp"
#include <climits>
#include <cmath>
using namespace difficulty_test;
using namespace csol::scenario;

namespace
{
void Scanner()
{
    struct Case { std::string text; std::vector<std::vector<std::string>> rows; };
    const std::vector<Case> cases{
        {"a,b\r\nc,d\r\n",{{"a","b"},{"c","d"}}},
        {"a,b\nc,d",{{"a","b\nc","d"}}},
        {"a,b\rc,d",{{"a","b\rc","d"}}},
        {"a\r\r\nb",{{"a\r"},{"b"}}},
        {"\r\n\r\nx,",{{""},{""},{"x",""}}},
        {"\xef\xbb\xbf",{{"\xef\xbb\xbf"}}},
        {"\xef\xbb\xbf" "1,2",{{"\xef\xbb\xbf" "1","2"}}},
        {"\"a,b\"tail,c",{{"a,btail","c"}}},
        {"a\"b,c",{{"a\"b","c"}}},
        {"\"a\"\"b\",\"c\r\nd\"\r\n",{{"a\"b","c\r\nd"}}},
        {"\"\",",{{"",""}}},
    };
    DifficultyTable table;ConfigError error;
    for(const auto& c:cases) {
        Require(ReadDifficultyTable(c.text,table,error),"original scanner defined input accepted");
        Check(table.source.sourceBytes==c.text&&table.source.records.size()==c.rows.size(),"raw bytes and physical row count preserved");
        for(std::size_t i=0;i<c.rows.size()&&i<table.source.records.size();++i)
            Check(table.source.records[i].fields==c.rows[i],"CRLF chunks and fields match literal expected cells");
    }
    Require(ReadDifficultyTable(TinyDifficulty,table,error),"transaction seed");
    const auto seed=table.source.sourceBytes;
    for(const auto& bad:std::vector<std::string>{"",std::string("a\0b",3),"\"open",std::string(65537,'x'),std::string(8*1024*1024+1,'x')})
        Check(!ReadDifficultyTable(bad,table,error)&&table.source.sourceBytes==seed&&!error.message.empty(),"bad parse preserves prior output");
    Check(ReadDifficultyTable(std::string(65536,'x'),table,error),"exact field bound accepted");
    std::string columns(255,',');Check(ReadDifficultyTable(columns,table,error)&&table.source.records[0].fields.size()==256,"exact 256 fields accepted");
    columns+=',';Check(!ReadDifficultyTable(columns,table,error),"257 fields rejected");
}
void Numbers()
{
    struct Number { const char* text; std::uint32_t bits; };
    for(const auto n:{Number{" ",0}, {"word",0}, {"+",0}, {"--1",0}, {"+ 1",0}, {"1.5tail",0x3fc00000},
        {"-.5",0xbf000000},{"  +2e1suffix",0x41a00000},{"-0",0x80000000},{"-1e-99999",0x80000000},
        {"1e-99999",0},{"1e-46",0},{"3.4028234663852886e38",0x7f7fffff}}) {
        DifficultyTable t;ConfigError e;Require(ReadDifficultyTable(n.text,t,e),"numeric cell text retained");
        float value=99;Check(t.Parameter(0,0,value,e)&&Bits(value,n.bits),"CRT decimal prefix converts through float32 expected bits");
    }
    for(const char* s:{"0x1p1","nan","inf","1e99999","3.5e38"}) {
        DifficultyTable t;ConfigError e;Require(ReadDifficultyTable(s,t,e),"unsafe numeric table itself retained");
        float value=123;Check(!t.Parameter(0,0,value,e)&&value==123&&!e.message.empty(),"unsafe getter leaves output unchanged");
    }
    DifficultyTable t;ConfigError e;Require(ReadDifficultyTable("head,word\r\n99,2,\r\n99,3\r\n",t,e),"ID duplicates and irregular columns retained");
    float v=7;Check(t.Parameter(0,1,v,e)&&v==0,"header is real row zero nonnumeric text");
    Check(t.Parameter(1,1,v,e)&&v==2&&t.Parameter(2,1,v,e)&&v==3,"physical row lookup ignores duplicate IDs");
    v=7;Check(!t.Parameter(1,2,v,e)&&v==7&&!t.Parameter(2,2,v,e)&&v==7&&!t.Parameter(999,0,v,e)&&v==7,"empty missing column and missing row preserve output");
}
void DropMerge()
{
    const std::string csv="header\r\n1,0,1,10,0,1,8,7\r\n1,0,2,20,0,2,8,7\r\n1,0,3,30,0,3,8,8\r\n1,2,4,40,0,4,8,7\r\n1,2,5,50,0,5,8,7\r\n1,3,6,60,0,6,8,8\r\n65535,0,0,1,0,1,8,9\r\n";
    DropItemTable t;ConfigError e;Require(ReadDropItemRules(csv,2,t,e),"mixed duplicate reward fixture");
    auto rows=t.Find(1);Check(rows.size()==3&&rows[0].recordIndex==3&&rows[1].recordIndex==4&&rows[2].recordIndex==5,"every matching default removed and all exact duplicates appended in order");
    Check(t.Find(-1).size()==1&&t.Find(-1)[0].dropId==-1&&t.rows.size()==7,"ID stores low signed16 and raw rows remain");
    const auto seed=t.source.sourceBytes;
    Check(!ReadDropItemRules("h\r\n2147483648",2,t,e)&&t.source.sourceBytes==seed,"integer overflow preserves drop output");
    Require(ReadDropItemRules("h\r\n1,0,word,-1e-99999\r\n",3,t,e),"missing numeric fields and ordinary nonnumeric become zero");
    Check(t.rows.size()==1&&Bits(t.rows[0].probability,0x80000000)&&t.rows[0].rewardId==0&&t.rows[0].dropEffect==0,"signed underflow and default integers exact");
    Require(ReadDropItemRules({},2,t,e),"empty drop table valid");Check(t.rows.empty()&&t.byId.empty()&&t.difficulty==2,"empty input replaces table with requested selection");
}
void Actual(const std::filesystem::path& directory)
{
    const auto difficultyBytes=Read(directory/"ZBS_Difficulty_Default.cso");
    const auto dropBytes=Read(directory/"ZBS_DropItem.cso");
    DifficultyTable d;ConfigError e;Require(ReadDifficultyResource(Bytes(difficultyBytes),d,e),"actual default encrypted resource decodes");
    Check(d.source.records.size()==8,"actual physical header plus seven rows");
    for(std::size_t row=1;row<=7;++row) {
        Check(d.source.records[row].fields.size()==27&&d.NameToken(row)==actual_expected::names[row-1],"actual row has all 27 cells and original localization token");
        for(std::size_t col=0;col<27;++col) {float v=999;Check(d.Parameter(row,col,v,e)&&Bits(v,actual_expected::difficulty[row-1][col]),"actual all-column float bits match frozen independent observation");}
    }
    for(int level=0;level<=7;++level) {
        DropItemTable t;Require(ReadDropItemResource(Bytes(dropBytes),level,t,e),"actual drop resource decodes for selection");
        Require(t.rows.size()==actual_expected::drops.size(),"actual drop source has 57 rows");
        for(std::size_t i=0;i<t.rows.size();++i) {
            const auto& x=t.rows[i];const auto& v=actual_expected::drops[i];
            Check(x.recordIndex==v.row&&x.dropId==v.id&&x.difficulty==v.difficulty&&x.dropEffect==v.effect&&Bits(x.probability,v.probability)&&x.ownerType==v.owner&&x.count==v.count&&x.removeSeconds==v.seconds&&x.rewardId==v.reward,"all actual row scalar fields match frozen observation");
        }
        for(const auto& expected:actual_expected::selected)if(expected.difficulty==level) {
            const auto chosen=t.Find(expected.id);std::vector<std::size_t> rows;for(const auto& x:chosen)rows.push_back(x.recordIndex);
            Check(rows==expected.rows,"actual difficulty merge keeps exact ordered record identities");
        }
    }
}
}
int main(int argc,char** argv)
{
    try {
        Require(argc>=2,"one bounded data group required");const std::string mode=argv[1];
        if(mode=="--parser"){Scanner();Numbers();DropMerge();}
        else if(mode=="--actual"){Require(argc==3,"frozen actual resource directory required");Actual(argv[2]);}
        else throw std::runtime_error("unknown data group");
    } catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"difficulty-data "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
