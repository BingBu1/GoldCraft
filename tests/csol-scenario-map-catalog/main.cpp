// SPDX-License-Identifier: GPL-3.0-or-later
// New catalog/automatic-loader integration only. Earlier profile suites are separate.
#include "map_catalog.hpp"
#include "map_resources.hpp"
#include "../csol-scenario-map-profile/fixtures.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>

using namespace csol::scenario;
namespace
{
int checks{},failures{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
std::string Read(const std::string& path)
{std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("missing input "+path);return {std::istreambuf_iterator<char>(file),{}};}
std::string Csv(const std::vector<std::string>& fields)
{
    std::string row;for(std::size_t i=0;i<fields.size();++i)
    {if(i)row+=',';if(fields[i].find_first_of(",\"\r\n")!=std::string::npos){row+='"';for(char c:fields[i]){if(c=='"')row+='"';row+=c;}row+='"';}else row+=fields[i];}
    return row+"\r\n";
}
std::vector<std::string> HeaderFields()
{std::vector<std::string> f(60);f[0]=";map_id";f[1]=" map_name";f[2]=" map_filename";f[58]="SubType";f[59]="\xbd\xc3\xb3\xaa\xb8\xae\xbf\xc0";return f;}
std::string Header(){return Csv(HeaderFields());}
std::string Row(std::string id,std::string name="zs_test",std::string type="1",std::string label="#LABEL")
{std::vector<std::string> f(59);f[0]=std::move(id);f[1]=std::move(label);f[2]=std::move(name);f[58]=std::move(type);return Csv(f);}
std::string Catalog(std::string name="zs_test",int type=1){return Header()+Row("98",std::move(name),std::to_string(type));}
void ParserAndSelection()
{
    MapCatalog catalog;ConfigError error{44,"old"};const auto header=Header();
    const auto raw=header+Row("9","zs_same","1","label,with\"quote")+Row("9","zs_same","4")+
        Row("10","zs_same","1")+"; comment,,,,\r\n\r\n"+Row("11","zs_other","0");
    Check(ReadMapCatalog(raw,catalog,error),"catalog parses fixed columns with comments duplicate IDs and quoted labels");
    Check(error.message.empty()&&error.byteOffset==0,"parse success clears prior diagnostic");
    Check(catalog.source.sourceBytes==raw&&catalog.source.records[0].fields==HeaderFields(),"source header spaces and CP949 bytes preserved exactly");
    Check(catalog.rows.size()==4&&catalog.byId.size()==3,"all data rows retained while numeric ID index selects first");
    Check(catalog.rows[0].recordIndex==1&&catalog.rows[3].recordIndex==6&&catalog.rows[0].nameToken=="label,with\"quote","physical record indices and parsed quoted labels retained");
    Check(catalog.Find(9)==&catalog.rows[0]&&catalog.Find(9)->scenarioType==1,"numeric duplicate ID selects first row");
    Check(!catalog.Find(99),"unknown numeric ID cannot inherit a previous match");
    int type=99;Check(catalog.ResolveScenarioType("zs_same",type,error)&&type==1,"same filename considers only numerically reachable rows, agreeing valid IDs accepted");
    type=77;Check(!catalog.ResolveScenarioType("ZS_SAME",type,error)&&type==77,"host filename match is case sensitive with unchanged failure output");
    Check(!catalog.ResolveScenarioType("zs_same.bsp",type,error)&&type==77,"no extension or path normalization inferred from numeric original lookup");
    MapCatalog conflict;Check(ReadMapCatalog(Header()+Row("8","zs_conflict","1")+Row("9","zs_conflict","4"),conflict,error),"distinct ID conflict is valid catalog data");
    type=33;Check(!conflict.ResolveScenarioType("zs_conflict",type,error)&&type==33&&error.byteOffset==conflict.source.records[2].byteOffset,"host ambiguity rejected at differing reachable row without overwriting caller output");
    Check(conflict.Find(8)->scenarioType==1&&conflict.Find(9)->scenarioType==4,"conflicting filename retains both explicit numeric choices");
    Check(ReadMapCatalog(Header()+Row("1","zs_a","1")+Row("1","zs_unreachable","4"),conflict,error),"unreachable duplicate ID with other name is retained");
    Check(!conflict.ResolveScenarioType("zs_unreachable",type,error),"later duplicate ID cannot make otherwise absent filename reachable");
    Check(ReadMapCatalog(Header(),conflict,error)&&conflict.rows.empty()&&!conflict.ResolveScenarioType("zs_a",type,error),"header-only catalog valid but resolves no map");
}
void NumericAndFailure()
{
    struct Case{const char* id;const char* subtype;int expectedId,expectedType;};
    const Case cases[]{{" +12suffix "," -2.5x ",12,-2},{"2147483647","-2147483648",INT32_MAX,INT32_MIN},
        {"-2147483648","2147483647",INT32_MIN,INT32_MAX},{"0x10","1e2",0,1},{"00","",0,0},
        {"-0"," \t\v\f ",0,0},{"+123","+4extra",123,4}};
    for(const auto& t:cases)
    {MapCatalog c;ConfigError e;Check(ReadMapCatalog(Header()+Row(t.id,"zs_test",t.subtype),c,e),"signed integer prefixes and empty subtype parse");Check(c.rows.size()==1&&c.rows[0].id==t.expectedId&&c.rows[0].scenarioType==t.expectedType,"catalog numeric prefix retains full signed int without narrowing");}
    MapCatalog c;ConfigError e;Check(ReadMapCatalog(Header()+"4,label,zs_short\r\n",c,e)&&c.rows[0].scenarioType==0,"missing column58 defaults to zero as initialized original getter");
    Check(ReadMapCatalog(Catalog(),c,e),"transaction failure fixture valid");const auto before=c.source.sourceBytes;const auto* pointer=c.Find(98);
    std::vector<std::string> bad{"", "not a header\r\n",Header()+Row("","zs_test","1"),Header()+Row("nonnumeric"),Header()+Row("2147483648"),
        Header()+Row("-2147483649"),Header()+Row("+"),Header()+Row("+-1"),Header()+Row("--1"),Header()+Row("1","","1"),
        Header()+Row("1","zs_test","garbage"),Header()+Row("1","zs_test","2147483648"),Header()+Row("1","zs_test","-2147483649"),
        Header()+"\"unterminated",Header()+Row("98")+std::string(1,'\0')};
    for(std::size_t col:{0u,1u,2u,58u}){auto header=HeaderFields();header[col]="wrong";bad.push_back(Csv(header)+Row("1"));}
    for(const auto& bytes:bad)
    {Check(!ReadMapCatalog(bytes,c,e)&&!e.message.empty(),"invalid schema numeric range missing field or CSV rejects");Check(c.source.sourceBytes==before&&c.Find(98)==pointer&&c.rows.size()==1,"parse failure preserves prior catalog bytes rows and numeric selection");}
}
void NumericSelection()
{
    MapCatalog catalog;ConfigError error;
    Check(!catalog.UniqueMapId("zs_test"),"empty catalog has no implicit map identity");
    Check(ReadMapCatalog(Catalog(),catalog,error)&&catalog.UniqueMapId("zs_test")==98,"one positive reachable ID resolves");
    Check(!catalog.UniqueMapId("ZS_TEST")&&!catalog.UniqueMapId("zs_test.bsp")&&!catalog.UniqueMapId(""),"numeric resolution preserves exact filename rules");
    Check(ReadMapCatalog(Header()+Row("98")+Row("98","zs_test","4"),catalog,error)&&catalog.UniqueMapId("zs_test")==98,
        "duplicate numeric ID does not make the unreachable row ambiguous");
    Check(ReadMapCatalog(Header()+Row("98")+Row("98","zs_shadow","4"),catalog,error)&&!catalog.UniqueMapId("zs_shadow"),
        "shadowed filename has no reachable ID");
    Check(ReadMapCatalog(Header()+Row("98")+Row("99"),catalog,error),"same filename distinct IDs parse");
    int type=-1;Check(catalog.ResolveScenarioType("zs_test",type,error)&&type==1&&!catalog.UniqueMapId("zs_test"),
        "agreeing subtype preserves Selection while numeric identity remains absent");
    for(const char* invalid:{"0","-1","-2147483648"})
    {
        Check(ReadMapCatalog(Header()+Row(invalid),catalog,error)&&!catalog.UniqueMapId("zs_test"),"nonpositive numeric selection is not usable");
        Check(ReadMapCatalog(Header()+Row("98")+Row(invalid),catalog,error)&&!catalog.UniqueMapId("zs_test"),"positive row cannot hide reachable nonpositive identity");
    }
    Check(ReadMapCatalog(Header()+Row("2147483647"),catalog,error)&&catalog.UniqueMapId("zs_test")==INT32_MAX,"largest positive int32 map ID retained");
    const auto original=catalog.source.sourceBytes;
    Check(!ReadMapCatalog(Header()+Row("bad"),catalog,error)&&catalog.source.sourceBytes==original&&catalog.UniqueMapId("zs_test")==INT32_MAX,
        "failed parse preserves selected numeric identity with old catalog");
}
std::string Unhex(std::string_view s){std::string result;for(std::size_t i=0;i<s.size();i+=2)result.push_back(static_cast<char>(std::stoi(std::string(s.substr(i,2)),nullptr,16)));return result;}
std::vector<std::string> Paths(std::string_view name,bool catalog=true,bool trigger=true)
{const std::string n(name);std::vector<std::string> p;if(catalog)p.push_back("resource/maplist.csv");p.push_back("maps/zbs_wave_"+n+".cso");p.push_back("maps/zbs_"+n+".cso");p.push_back("maps/ZBS_ZT_"+n+".cso");if(trigger)p.push_back("maps/ZBS_TRG_"+n+".cso");return p;}
struct File{std::string bytes;std::optional<int> length;bool present{true},writeLength{true};explicit File(std::string s={}):bytes(std::move(s)){};};
struct Allocation{std::unique_ptr<unsigned char[]> data;std::string original;};
struct Engine
{
    static Engine* active;std::map<std::string,File> files;std::vector<std::string> loads;std::vector<int> lengths;std::map<void*,Allocation> owned;
    std::vector<std::unique_ptr<unsigned char[]>> retired;std::size_t freed{};bool intact{true},validFree{true},catalogReleasedFirst{true};
    Engine(){if(active)throw std::runtime_error("nested fixture");active=this;}~Engine(){active=nullptr;}
    ResourceFileApi Api(){return {Load,Free};}
    void Configure(std::string name="zs_test",int type=1)
    {files["resource/maplist.csv"]=File(Catalog(name,type));auto p=Paths(name,false);files[p[0]]=File(Unhex(fixtures::waveA));files[p[1]]=File(Unhex(fixtures::roundA));files[p[2]]=File(Unhex(fixtures::typeA));files[p[3]]=File(Unhex(fixtures::triggerA));}
    void Reset(){Check(owned.empty(),"all prior allocations freed");loads.clear();lengths.clear();freed=0;intact=validFree=catalogReleasedFirst=true;}
    static unsigned char* Load(const char* path,int* length)
    {
        auto& e=*active;e.loads.emplace_back(path);e.lengths.push_back(*length);
        if(e.loads.size()>1)e.catalogReleasedFirst&=e.owned.empty();
        const auto it=e.files.find(path);if(it==e.files.end())return nullptr;const auto& f=it->second;
        if(f.writeLength)*length=f.length.value_or(static_cast<int>(f.bytes.size()));if(!f.present)return nullptr;
        auto bytes=std::make_unique<unsigned char[]>(f.bytes.size()+1);std::memcpy(bytes.get(),f.bytes.data(),f.bytes.size());bytes[f.bytes.size()]=0;
        auto* raw=bytes.get();e.owned.emplace(raw,Allocation{std::move(bytes),f.bytes+'\0'});return raw;
    }
    static void Free(void* p)
    {auto& e=*active;++e.freed;const auto at=e.owned.find(p);if(at==e.owned.end()){e.validFree=false;return;}auto& a=at->second;e.intact&=std::memcmp(p,a.original.data(),a.original.size())==0;std::memset(p,0xdd,a.original.size());e.retired.push_back(std::move(a.data));e.owned.erase(at);}
};
Engine* Engine::active{};
void Calls(const Engine& e,const std::vector<std::string>& expected,std::size_t frees)
{Check(e.loads==expected,"only exact ordered map-relative paths requested");Check(e.freed==frees&&e.owned.empty()&&e.validFree,"nonnull buffers freed exactly once");Check(e.intact&&e.catalogReleasedFirst,"buffers untouched and catalog freed before any map table load");Check(std::all_of(e.lengths.begin(),e.lengths.end(),[](int n){return n==0;}),"reported lengths initialized on each load");}
void SelectionIdentityLifecycle()
{
    Engine engine;engine.Configure();MapResources resources;ResourceError error;
    Check(resources.BeginMap("zs_test",engine.Api(),error,1)&&!resources.Tables()->mapId,"legacy explicit subtype has no guessed numeric identity");
    for(int id:{1,98,INT32_MAX})
    {
        engine.Reset();Check(resources.BeginMap(resources.MapName(),engine.Api(),error,1,id)&&resources.Tables()->mapId==id,
            "explicit positive ID commits with alias-safe BeginMap");Calls(engine,Paths("zs_test",false),4);
        const auto* working=resources.Tables();const auto wave=working->waves.source.sourceBytes;
        engine.Reset();engine.files[Paths("zs_test",false).front()].present=false;
        Check(!resources.Reload(engine.Api(),error)&&resources.Tables()==working&&working->mapId==id&&working->waves.source.sourceBytes==wave,
            "failed reload preserves working tables and exact selection identity");
        Calls(engine,{Paths("zs_test",false).front()},0);
        engine.Reset();engine.files[Paths("zs_test",false).front()].present=true;engine.files["resource/maplist.csv"]=File("invalid external catalog");
        Check(resources.Reload(engine.Api(),error)&&resources.Tables()->mapId==id,"successful reload preserves explicit identity without catalog access");
        Calls(engine,Paths("zs_test",false),4);
    }
    for(int invalid:{0,-1,INT32_MIN})
    {
        engine.Reset();Check(!resources.BeginMap("zs_test",engine.Api(),error,1,invalid)&&!resources.Tables()&&engine.loads.empty()&&!error.detail.message.empty(),
            "nonpositive explicit ID clears old session before any file load");
        Check(!resources.Reload(engine.Api(),error)&&engine.loads.empty(),"invalid identity cannot be repaired by implicit reload");
        Check(resources.BeginMap("zs_test",engine.Api(),error,1,98),"valid explicit setup restores test session");
    }
    engine.Reset();engine.files["resource/maplist.csv"]=File(Catalog());
    Check(resources.BeginMapFromCatalog("zs_test",engine.Api(),error)&&resources.Tables()->mapId==98,"catalog selects actual unique numeric map ID");
    engine.Reset();engine.files["resource/maplist.csv"]=File(Header()+Row("98")+Row("99"));
    Check(resources.BeginMapFromCatalog(resources.MapName(),engine.Api(),error)&&resources.Tables()->scenarioType==1&&!resources.Tables()->mapId,
        "ambiguous new session preserves compatible subtype but clears old numeric ID");Calls(engine,Paths("zs_test"),5);
    engine.Reset();engine.files["resource/maplist.csv"]=File(Catalog());
    Check(resources.Reload(engine.Api(),error)&&!resources.Tables()->mapId,"ordinary reload cannot silently resolve changed external catalog");Calls(engine,Paths("zs_test",false),4);
    engine.Reset();Check(resources.BeginMapFromCatalog("zs_test",engine.Api(),error)&&resources.Tables()->mapId==98,"new catalog session resolves current numeric choice");
    const auto trigger=Paths("zs_test",false).back();engine.Reset();engine.files[trigger].present=false;
    Check(!resources.BeginMap("zs_test",engine.Api(),error,1,143)&&!resources.Tables(),"failed new map cannot retain old table identity");
    engine.Reset();engine.files[trigger].present=true;
    Check(resources.Reload(engine.Api(),error)&&resources.Tables()->mapId==143,"table repair retains the successfully selected new ID");
    engine.Reset();Check(resources.BeginMap("zs_test",engine.Api(),error,1)&&!resources.Tables()->mapId,"later subtype-only BeginMap clears previous explicit ID");
    resources.Clear();engine.Reset();Check(!resources.Reload(engine.Api(),error)&&!resources.Tables()&&resources.MapName().empty()&&engine.loads.empty(),"Clear removes identity and entire session");
    Check(resources.BeginMap("zs_test",engine.Api(),error,1,98),"invalid-name fixture has numeric identity");engine.Reset();
    Check(!resources.BeginMap("../zs_test",engine.Api(),error,1,98)&&!resources.Tables()&&engine.loads.empty(),"invalid new map name drops numeric session without IO");
    Check(!resources.Reload(engine.Api(),error)&&engine.loads.empty(),"invalid-name setup cannot reuse old numeric map ID");
}
void CatalogLoadLifecycle()
{
    for(int type:{0,1,3,4,10,-7,65537})
    {
        Engine e;e.Configure("zs_test",type);MapResources m;ResourceError error{"old",{99,"old"}};const bool trg=type==1||type==3||type==4||type==10;
        Check(m.BeginMapFromCatalog("zs_test",e.Api(),error),"catalog subtype selects required three or four tables");Calls(e,Paths("zs_test",true,trg),trg?5:4);
        Check(m.Tables()&&m.Tables()->scenarioType==type&&m.Tables()->triggerRules.has_value()==trg&&error.path.empty()&&error.detail.message.empty(),"resolved profile and optional table commit and old error clears");
        Check(m.Tables()->rounds.source.records[0].fields[0]=="10","round gametype metadata cannot override catalog SubType");
        e.Reset();e.files["resource/maplist.csv"]=File("corrupt");Check(m.Reload(e.Api(),error),"ordinary reload uses session type and does not re-read changed catalog");Calls(e,Paths("zs_test",false,trg),trg?4:3);
        e.Reset();e.files["resource/maplist.csv"]=File(Catalog("zs_test",trg?0:1));Check(m.BeginMapFromCatalog(m.MapName(),e.Api(),error),"BeginMapFromCatalog own MapName view survives clear and resolves catalog again");Calls(e,Paths("zs_test",true,!trg),trg?4:5);
        Check(m.Tables()->scenarioType==(trg?0:1)&&m.MapName()=="zs_test","alias restart applies newly resolved subtype rather than old session");
    }
    Engine e;e.Configure();MapResources m;ResourceError error;
    e.files["resource/maplist.csv"].bytes+=std::string("; trailing comment");
    Check(e.files["resource/maplist.csv"].bytes.size()%8!=0,"plain catalog fixture intentionally not cipher block aligned");
    Check(m.BeginMapFromCatalog("zs_test",e.Api(),error),"plain catalog has no encrypted modulo8 requirement");Calls(e,Paths("zs_test"),5);
    const auto path=Paths("zs_test",false).back();e.Reset();e.files[path].present=false;
    Check(!m.BeginMapFromCatalog(m.MapName(),e.Api(),error)&&!m.Tables()&&m.MapName()=="zs_test"&&error.path==path,"failed fourth file on new session clears prior tables but preserves resolved map");Calls(e,Paths("zs_test"),4);
    e.Reset();e.files[path].present=true;e.files["resource/maplist.csv"]=File("corrupt");
    Check(m.Reload(e.Api(),error)&&m.Tables()->scenarioType==1&&m.Tables()->triggerRules,"after fourth-file repair ordinary reload retains successfully resolved subtype");Calls(e,Paths("zs_test",false),4);
    m.Clear();e.Reset();Check(!m.Reload(e.Api(),error)&&e.loads.empty()&&m.MapName().empty()&&!m.Tables(),"Clear removes map and resolved type without touching filesystem");
}
void CatalogFailures()
{
    for(int fault=0;fault<14;++fault)
    {
        Engine e;e.Configure();MapResources m;ResourceError error;Check(m.BeginMap("zs_test",e.Api(),error,1),"failure fixture has prior valid session");e.Reset();auto& f=e.files["resource/maplist.csv"];
        if(fault==0)f.present=false;if(fault==1)f.length=0;if(fault==2)f.length=-1;if(fault==3)f.length=8*1024*1024+1;if(fault==4)f.writeLength=false;
        if(fault==5)f.bytes="\"unterminated";if(fault==6)f.bytes="bad header";if(fault==7)f.bytes=Catalog("zs_missing");
        if(fault==8)f.bytes=Header()+Row("1","zs_test","1")+Row("2","zs_test","4");if(fault==9)f.bytes=Header()+Row("1","","1");
        if(fault==10)f.bytes=Header()+Row("1","zs_test","oops");if(fault==11)f.bytes+='\0';if(fault==12)f.length=static_cast<int>(f.bytes.size()+1);if(fault==13)f.length=2;
        Check(!m.BeginMapFromCatalog("zs_test",e.Api(),error)&&error.path=="resource/maplist.csv"&&!error.detail.message.empty(),"catalog load/schema/resolve failure reported at catalog path");
        Calls(e,{"resource/maplist.csv"},fault==0?0:1);Check(!m.Tables()&&m.MapName()=="zs_test","failed new catalog clears old tables but records valid requested map");
        e.Reset();e.files["resource/maplist.csv"]=File(Catalog());Check(!m.Reload(e.Api(),error)&&e.loads.empty()&&!m.Tables()&&error.path=="resource/maplist.csv","unresolved type prevents implicit subtype-zero reload even after catalog repaired");
        Check(m.BeginMapFromCatalog("zs_test",e.Api(),error)&&m.Tables()->scenarioType==1,"explicit new catalog session retries resolution and commits valid tables");
    }
    for(int fault=0;fault<4;++fault)
    {
        Engine e;e.Configure();MapResources m;ResourceError error;Check(m.BeginMap("zs_test",e.Api(),error,1),"API failure fixture has old session");e.Reset();auto api=e.Api();if(fault==0)api.load=nullptr;if(fault==1)api.release=nullptr;
        const std::string name=fault==2?"../zs_test":fault==3?std::string(64,'a'):"zs_test";
        Check(!m.BeginMapFromCatalog(name,api,error)&&!m.Tables()&&e.loads.empty(),"invalid map/API clears old session before any engine call");
        Check(!m.Reload(e.Api(),error)&&e.loads.empty(),"invalid setup cannot reopen old map profile");
    }
}
struct Reader
{
    std::string bytes;std::size_t at{};
    std::uint32_t U32(){if(at+4>bytes.size())throw std::runtime_error("truncated expected binary");std::uint32_t v{};std::memcpy(&v,bytes.data()+at,4);at+=4;return v;}
    std::int32_t I32(){return std::bit_cast<std::int32_t>(U32());}
    std::string Text(){auto n=U32();if(n>bytes.size()-at)throw std::runtime_error("truncated expected text");auto v=bytes.substr(at,n);at+=n;return v;}
};
void Actual(const std::string& common,const std::string& fixture)
{
    const auto raw=Read(common+"/resource/maplist.csv");MapCatalog catalog;ConfigError error;
    Check(ReadMapCatalog(raw,catalog,error),"actual common catalog parses");Check(catalog.source.sourceBytes==raw,"actual CP949 catalog raw bytes retained");
    Reader expected{Read(fixture)};Check(expected.bytes.substr(0,4)=="MCAT","independent catalog oracle magic");expected.at=4;
    const auto recordCount=expected.U32();Check(catalog.source.records.size()==recordCount&&recordCount==270,"actual header and269data record count");
    for(std::uint32_t i=0;i<recordCount;++i)
    {const auto offset=expected.U32(),count=expected.U32();std::vector<std::string> fields;for(std::uint32_t n=0;n<count;++n)fields.push_back(expected.Text());Check(catalog.source.records.at(i).byteOffset==offset&&catalog.source.records.at(i).fields==fields,"all actual record fields and physical offsets equal independent byte CSV oracle");}
    const auto rowCount=expected.U32();Check(catalog.rows.size()==rowCount&&rowCount==269&&catalog.byId.size()==269,"actual269unique numeric map IDs retained");
    std::map<std::string,std::vector<int>> originalIds;
    for(std::uint32_t i=0;i<rowCount;++i)
    {const auto record=expected.U32();const auto id=expected.I32(),type=expected.I32();const auto label=expected.Text(),file=expected.Text();const auto& row=catalog.rows.at(i);
     Check(row.recordIndex==record&&row.id==id&&row.scenarioType==type&&row.nameToken==label&&row.fileName==file,"all269typed catalog rows match independent expectation");Check(catalog.Find(id)==&row,"actual numeric ID resolves correct row without flattening duplicate filenames");originalIds[file].push_back(id);}
    for(const auto& [file,ids]:originalIds)
        Check(catalog.UniqueMapId(file)==(ids.size()==1&&ids.front()>0?std::optional<int>{ids.front()}:std::nullopt),
            "actual unique/ambiguous map IDs match independent catalog rows");
    const auto names=expected.U32();for(std::uint32_t i=0;i<names;++i){const auto name=expected.Text();const auto type=expected.I32();int out=991;Check(catalog.ResolveScenarioType(name,out,error)&&out==type,"all actual distinct filename groups agree and resolve expected subtype");}
    const std::array<std::tuple<const char*,int,int>,7> first{{{"zs_lostcity",74,0},{"zs_2gate",75,0},{"zs_trap",76,0},{"zs_lastclue",77,1},{"zs_chaos",85,4},{"zs_deadend",98,1},{"zs_nightmare",101,1}}};
    for(const auto& [name,id,type]:first){int out=-999;Check(catalog.Find(id)&&catalog.Find(id)->fileName==name&&catalog.Find(id)->scenarioType==type&&catalog.ResolveScenarioType(name,out,error)&&out==type,"first-season seven original mapIDs filenames/subtypes match static contract");}
    Check(expected.at==expected.bytes.size(),"oracle consumed exactly no trailing fields");
    Engine engine;engine.files["resource/maplist.csv"]=File(raw);auto paths=Paths("zs_deadend",false);for(const auto& p:paths)engine.files[p]=File(Read(common+'/'+p));
    MapResources resources;ResourceError resourceError;Check(resources.BeginMapFromCatalog("zs_deadend",engine.Api(),resourceError),"actual common catalog automatically loads deadend four tables");Calls(engine,Paths("zs_deadend"),5);
    const auto* table=resources.Tables();Check(table&&table->mapName=="zs_deadend"&&table->scenarioType==1&&table->triggerRules&&table->mapId==98,"actual deadend map98 subtype1 committed with TRG");
    if(table)Check(table->waves.rows.size()==288&&table->rounds.rows.size()==2&&table->monsterTypes.rows.size()==38&&table->triggerRules->rows.size()==29,"actual common four-table288/2/38/29 counts");
    engine.Reset();engine.files["resource/maplist.csv"]=File("changed catalog must not be read");Check(resources.Reload(engine.Api(),resourceError)&&resources.Tables()->scenarioType==1&&resources.Tables()->mapId==98,"actual catalog session reload holds resolved subtype and map98");Calls(engine,Paths("zs_deadend",false),4);
}
}
int main(int argc,char** argv)
{
    try{if(argc==4&&std::string_view(argv[1])=="--actual")Actual(argv[2],argv[3]);else if(argc==1){ParserAndSelection();NumericAndFailure();NumericSelection();CatalogLoadLifecycle();SelectionIdentityLifecycle();CatalogFailures();}else throw std::runtime_error("usage: test [--actual common expected.bin]");}
    catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION: "<<e.what()<<'\n';}
    std::cout<<"catalog checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
