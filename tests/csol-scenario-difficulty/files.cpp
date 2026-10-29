// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.hpp"
using namespace difficulty_test;
using namespace csol::scenario;
namespace
{
void DifficultyFiles()
{
    Files f;DifficultyResources d;ResourceError error;ConfigError cell;
    float value=9;
    Check(d.Parameter(15,2,value,cell)&&value==1,"uninitialized owner has neutral factor");
    Check(!d.Reload("map_session_test",f.Api(),error)&&f.order.empty(),"reload cannot invent initial snapshot");
    f.contents[DefaultPath]=Encrypt(TinyDifficulty);
    for(bool empty:{false,true}) {
        f.order.clear();if(empty)f.contents[MapPath]="";else f.contents.erase(MapPath);
        Require(d.BeginMap("map_session_test",2,f.Api(),error),"missing or zero-byte map falls back to complete default");
        const std::vector<std::string> expected=empty?std::vector<std::string>{std::string("load:")+MapPath,std::string("free:")+MapPath,std::string("load:")+DefaultPath,std::string("free:")+DefaultPath}:std::vector<std::string>{std::string("load:")+MapPath,std::string("load:")+DefaultPath,std::string("free:")+DefaultPath};
        Check(f.order==expected&&d.SourcePath()==DefaultPath&&d.SelectedRow()==2,"exact lookup/release order and selected row");
        Check(d.Parameter(15,2,value,cell)&&value==3,"physical second row selected despite ID one");
    }
    f.contents[MapPath]=Encrypt("h,n,v\r\n2,local,9\r\n");f.order.clear();
    Require(d.Reload("map_session_test",f.Api(),error),"reload replaces whole table");
    value=77;Check(d.SelectedRow()==2&&!d.Parameter(15,2,value,cell)&&value==77&&f.order==std::vector<std::string>{std::string("load:")+MapPath,std::string("free:")+MapPath},"missing selected row does not merge default or change snapshot");
    Require(d.BeginMap("map_session_test",1,f.Api(),error),"explicit new selection");Check(d.Parameter(48,2,value,cell)&&value==9,"new snapshot uses map row");
    for(const auto& bad:std::vector<std::string>{"x",Encrypt(std::string(8,'\0')),Encrypt("\"open")}) {
        f.contents[MapPath]=bad;f.order.clear();value=33;
        Check(!d.Reload("map_session_test",f.Api(),error)&&d.State()==DifficultyState::Failed&&!d.Table()&&d.SourcePath().empty(),"bad present resource clears old table and fails");
        Check(f.order==std::vector<std::string>{std::string("load:")+MapPath,std::string("free:")+MapPath},"malformed present file never falls back");
        Check(!d.Parameter(15,2,value,cell)&&value==33&&d.Parameter(1,2,value,cell)&&value==1,"failed mode15 cannot masquerade as neutral; unrelated mode remains neutral");
    }
    f.contents.clear();f.order.clear();Check(!d.BeginMap("map_session_test",1,f.Api(),error)&&d.State()==DifficultyState::Failed&&!error.detail.message.empty(),"both files absent fails explicitly");
    f.contents[MapPath]=Encrypt(TinyDifficulty);
    for(int badLength:{-1,1,8*1024*1024+8}) {
        f.reportedLength=badLength;f.order.clear();
        Check(!d.BeginMap("map_session_test",1,f.Api(),error)&&f.buffers.empty()&&f.order.size()==2,"bad length releases exactly once before failure");
    }
    f.reportedLength.reset();f.order.clear();
    for(const char* map:{"","../outside","a/b","a.b"})Check(!d.BeginMap(map,1,f.Api(),error)&&f.order.empty(),"invalid basename rejects before callbacks");
    Check(!d.BeginMap("map_session_test",1,{nullptr,Files::Free},error)&&f.order.empty(),"missing load interface rejects");
    Check(!d.BeginMap("map_session_test",1,{Files::Load,nullptr},error)&&f.order.empty(),"missing release interface rejects");
    int callbacks{};
    const auto callback=[&](std::string_view) {
        ++callbacks;const auto before=f.order.size();ResourceError nested;ConfigError query;float untouched=45;
        Check(d.State()==DifficultyState::Loading&&!d.Table()&&d.SelectedRow()==2,"callback sees loading snapshot and no published table");
        Check(!d.Clear()&&!d.BeginMap("other",7,f.Api(),nested)&&!d.Reload("other",f.Api(),nested),"all reentrant mutations rejected");
        Check(!d.Parameter(15,2,untouched,query)&&untouched==45&&f.order.size()==before,"loading getter fails without nested resource call");
    };
    f.onLoad=callback;f.onFree=callback;
    Require(d.BeginMap("map_session_test",2,f.Api(),error),"outer load survives denied callback reentry");
    f.onLoad={};f.onFree={};Check(callbacks==2&&d.State()==DifficultyState::Ready&&d.SelectedRow()==2&&error.detail.message.empty(),"published only after release callback returns");
    // Pass a view into the old table to ensure the owner copies before clear.
    f.contents["maps/ZBS_Difficulty_first.cso"]=Encrypt(TinyDifficulty);
    const auto alias=d.Table()->NameToken(1);Require(d.Reload(alias,f.Api(),error),"reload copies aliased map text before discarding prior table");
    Check(d.SourcePath()=="maps/ZBS_Difficulty_first.cso"&&d.SelectedRow()==2,"aliased reload keeps selected difficulty");
    Check(d.Clear()&&d.Clear()&&d.State()==DifficultyState::Empty&&d.SelectedRow()==0,"clear resets owner and is idempotent");
}
void DropFiles()
{
    Files f;DropItemResources d;ResourceError error;
    Require(d.Load(2,f.Api(),error),"absent drop table valid empty");Check(!d.Present()&&d.Rules().rows.empty()&&d.Rules().difficulty==2,"absent drop retains explicit selection");
    f.contents[DropPath]="";f.order.clear();Require(d.Load(1,f.Api(),error),"zero-byte drop valid empty");
    Check(d.Present()&&d.Rules().rows.empty()&&f.order==std::vector<std::string>{std::string("load:")+DropPath,std::string("free:")+DropPath},"zero-byte buffer released and present marked");
    f.contents[DropPath]=Encrypt(TinyDrops);int callbacks{};
    auto callback=[&](std::string_view) {
        ++callbacks;const auto before=f.order.size();ResourceError nested;
        Check(!d.Present()&&d.Rules().rows.empty()&&d.Rules().difficulty==2,"drop callback sees cleared rules and requested selection");
        Check(!d.Clear()&&!d.Load(7,f.Api(),nested)&&f.order.size()==before,"drop mutation callbacks cannot reenter loader");
    };
    f.onLoad=callback;f.onFree=callback;Require(d.Load(2,f.Api(),error),"drop outer operation commits after release");
    f.onLoad={};f.onFree={};Check(callbacks==2&&d.Present()&&d.Rules().Find(1).size()==2&&d.Rules().Find(1)[1].rewardId==12,"same requested difficulty selects correct exact reward");
    for(const auto& bad:std::vector<std::string>{"bad",Encrypt("h\r\n2147483648")}) {
        f.contents[DropPath]=bad;Check(!d.Load(1,f.Api(),error)&&!d.Present()&&d.Rules().rows.empty()&&d.Rules().difficulty==1&&f.buffers.empty(),"bad present drop clears stale rules and releases buffer");
    }
    Check(d.Clear()&&d.Rules().rows.empty()&&d.Rules().difficulty==0,"drop clear resets selected rules");
}
}
int main()
{
    try {DifficultyFiles();DropFiles();}
    catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"difficulty-files "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
