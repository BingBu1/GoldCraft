"""Test the actual character menu parser/controller with a recording VGUI surface.

No game process or shared build is used. Font widths and engine input are mocked;
these checks do not replace live font, mouse capture or OpenGL validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = r'''
#include "native/csol/client/runtime.hpp"
#include <VGUI/ISurface.h>
#include <Color.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <fstream>
#include <iostream>
#include <map>
namespace fixture {
int width=1116,height=780,checks{},sent{},randomResult{},randomMax=-1;
std::string command;
std::vector<std::array<int,4>> rectangles;
std::vector<std::wstring> texts;
std::map<vgui::HFont,int> fonts;
void Check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
void Rect(int x0,int y0,int x1,int y1){Check(x0>=0&&y0>=0&&x1>=x0&&y1>=y0&&x1<=width+1&&y1<=height+1,"paint rectangle outside viewport");rectangles.push_back({x0,y0,x1,y1});}
}
namespace vgui {
class RecordingSurface final:public ISurface {
public:
SURFACE_METHODS
};
}
#include "native/csol/client/characters.cpp"
namespace csol::client {
metahook_api_t* api{};mh_interface_t* interfaces{};cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};
std::vector<std::byte> ReadResource(const char*){throw std::runtime_error("unexpected resource load in fixture");}
void WeaponInventoryCreateMove(float,usercmd_t*,int){}
}
namespace csol {Image DecodeTGA(std::span<const std::byte>){throw std::runtime_error("unexpected decode in fixture");}}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
std::string Read(const char* path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("missing input");return {std::istreambuf_iterator<char>(file),{}};}
std::string Serialize(const rapidjson::Document& d){rapidjson::StringBuffer b;rapidjson::Writer<rapidjson::StringBuffer> w(b);d.Accept(w);return {b.GetString(),b.GetSize()};}
int main(int argc,char** argv){using namespace csol;using namespace csol::client;using fixture::Check;try{
    Check(argc==3,"two input paths required");const auto bytes=Read(argv[1]);original=ParseCharacterMenu(bytes);catalog=ParseCharacters(Read(argv[2]));
    Check(original.fullScreenFrame&&original.width==640&&original.height==448,"modern viewport");
    Check(original.descriptions.size()==catalog.size(),"every catalog description");
    Check(original.descriptions.at(140).defaultCaps==std::optional(std::array<int,5>{22,27,27,20,19}),"item defaults, no increment or copied player modifications");
    Check(!original.descriptions.at(168).defaultCaps,"unmapped class has no invented caps");
    for(int version:{1,2,3}){rapidjson::Document d;d.Parse(bytes.data(),bytes.size());d["version"].SetInt(version);d.RemoveMember("layout");d.RemoveMember("stats");
        if(version<3){d.RemoveMember("slotBackground");d.RemoveMember("keyboard");}if(version<2){d.RemoveMember("pin");d.RemoveMember("currentLabel");}
        auto old=ParseCharacterMenu(Serialize(d));Check(!old.fullScreenFrame&&!old.descriptions.at(140).defaultCaps,"legacy parser compatibility");}
    const auto reject=[&](auto mutation){rapidjson::Document d;d.Parse(bytes.data(),bytes.size());mutation(d);bool failed=false;try{ParseCharacterMenu(Serialize(d));}catch(...){failed=true;}Check(failed,"invalid presentation accepted");};
    reject([](auto& d){d["version"].SetInt(5);});reject([](auto& d){d["info"][0].SetInt(600);});
    reject([](auto& d){d["stats"]["bars"][0][0].SetInt(639);});reject([](auto& d){d["layout"]["closeButton"][2].SetInt(0);});
    reject([](auto& d){d["descriptions"]["140"]["defaultCaps"][0].SetInt(256);});
    reject([](auto& d){d["descriptions"]["140"]["defaultCaps"][0].SetInt(-1);});
    reject([](auto& d){d["descriptions"]["140"]["image"][2].SetInt(400);});reject([](auto& d){d.RemoveMember("stats");});
    reject([](auto& d){d["keyboard"]["path"].SetString("gfx/csol/ui/../escape.tga");});
    reject([](auto& d){d["stats"]["maximum"].SetInt(0);});
    rapidjson::Document zero;zero.Parse(bytes.data(),bytes.size());for(auto& cap:zero["descriptions"]["140"]["defaultCaps"].GetArray())cap.SetInt(0);
    Check(ParseCharacterMenu(Serialize(zero)).descriptions.at(140).defaultCaps==std::optional(std::array<int,5>{}),"explicit zero cap retained");
    vgui::RecordingSurface recorder;surface=&recorder;originalMode=true;loaded=true;originalLoaded=true;
    engine.pfnServerCmd=[](const char* text){++fixture::sent;fixture::command=text;return 1;};
    engine.pfnRandomLong=[](int low,int high){fixture::randomMax=high;return std::clamp(fixture::randomResult,low,high);};
    for(const auto& [id,d]:original.descriptions){descriptions[id]=Wide(d.text);portraits[id]=int(id);}
    const auto cache=[&](const MenuTexture& t){if(!t.path.empty())menuTextures[t.path]=1;};
    for(const auto& t:original.frame)cache(t);for(const auto& row:original.buttons)for(const auto& t:row)cache(t);
    for(const auto& t:original.close)cache(t);for(const auto& t:original.slotBackground)cache(t);cache(original.keyboard);cache(original.pin);
    for(int catalogTeam:{1,2}){team=catalogTeam;page=0;const auto rows=Filtered();Check(rows.size()>9&&PageSize()==9,"nine row page");
        SelectRow(0);Check(chosen==rows[0]->id&&fixture::command=="csol_character_select "+std::to_string(chosen)+"\n","first slot selection");
        SelectRow(8);Check(chosen==rows[8]->id,"ninth slot selection");
        fixture::randomResult=int(rows.size())-1;SelectRow(9);Check(chosen==rows.back()->id&&fixture::randomMax==int(rows.size())-1,"zero selects from full filtered catalog");
        ChangePage(1);Check(page==1&&chosen==rows[9]->id,"next page starts at ninth offset");
        ChangePage(1000);Check(page==int((rows.size()-1)/9),"last page clamp");
        const int absent=int(rows.size())-page*9;if(absent<9){const int before=fixture::sent;SelectRow(absent);Check(fixture::sent==before,"empty last page slot cannot select");}
        ChangePage(-1000);Check(page==0,"first page clamp");}
    for(const auto viewport:{std::array{640,480},std::array{1116,780},std::array{1280,720},std::array{1920,1080}}){
        fixture::width=viewport[0];fixture::height=viewport[1];Layout();
        for(int i=0;i<10;++i){const auto& r=original.slots[i];Check(OriginalHit(r.x+r.w/2.f,r.y+r.h/2.f)==i,"slot hit geometry");}
        for(int i=0;i<2;++i){const auto& r=original.teams[i];Check(OriginalHit(r.x+r.w/2.f,r.y+r.h/2.f)==10+i,"team hit geometry");}
        const auto& close=original.closeButton;Check(OriginalHit(close.x+close.w/2.f,close.y+close.h/2.f)==14,"close hit geometry");
        team=1;const auto rows=Filtered();for(size_t i=0;i<rows.size();++i)if(rows[i]->id==140)page=int(i/9);chosen=140;
        fixture::rectangles.clear();fixture::texts.clear();PaintOriginal();
        Check(std::count(fixture::texts.begin(),fixture::texts.end(),L"Max Level")==1,"heading painted once");
        Check(std::count(fixture::texts.begin(),fixture::texts.end(),L"自动选择")==1,"auto row painted once");
        for(const auto* label:{L"生命值",L"攻击力",L"移动速度",L"防弹衣",L"备弹"})Check(std::count(fixture::texts.begin(),fixture::texts.end(),label)==1,"five level labels");
        for(const auto value:{22,27,20,19})Check(std::find(fixture::texts.begin(),fixture::texts.end(),std::to_wstring(value))!=fixture::texts.end(),"default cap text painted");
        for(int i=0;i<5;++i){const auto& r=original.stats.bars[i];const int w=r.w*(*original.descriptions.at(140).defaultCaps)[i]/original.stats.maximum;
            Check(std::find(fixture::rectangles.begin(),fixture::rectangles.end(),std::array{X(r.x),Y(r.y),X(r.x+w),Y(r.y+r.h)})!=fixture::rectangles.end(),"level bar proportional width");}
        // Real scaled click shares its transform with drawing; no alternate hit layout.
        const auto& slot=original.slots[0];const auto expected=rows[page*9]->id;Click(X(slot.x+slot.w/2),Y(slot.y+slot.h/2));Check(chosen==expected,"scaled click selects drawn row");
    }
    std::cout<<"character menu: "<<fixture::checks<<" checks passed; font metrics/engine/VGUI mocked, no live gameplay\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" after "<<fixture::checks<<" checks\n";return 1;}}
'''


def methods():
    header = (ROOT/'external/MetaHookSv-20261007/MetaHook/include/Interface/VGUI/ISurface.h').read_text()
    header = header.split('class ISurface_HL25')[0]
    bodies = {
        'GetEmbeddedPanel': 'return 1;',
        'GetScreenSize': 'wide=fixture::width;tall=fixture::height;',
        'CreateFont': 'static HFont next=1;return next++;',
        'AddGlyphSetToFont': 'fixture::fonts[font]=tall;return true;',
        'GetTextSize': 'tall=fixture::fonts[font];wide=0;for(;*text;++text)wide+=*text<128?tall/2:tall;',
        'DrawFilledRect': 'fixture::Rect(x0,y0,x1,y1);',
        'DrawTexturedRect': 'fixture::Rect(x0,y0,x1,y1);',
        'DrawPrintText': 'fixture::texts.emplace_back(text,textLen);',
    }
    out = []
    for match in re.finditer(r'virtual\s+([^;]+?)\s*=\s*0;', header):
        signature = re.sub(r'\s+', ' ', match[1]).strip()
        name = re.search(r'(\w+)\(', signature)[1]
        body = bodies.get(name, '' if signature.startswith('void ') else 'return {};')
        out.append(signature+' override {'+body+'}')
    return '\n'.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--reference-command', type=Path, required=True)
    parser.add_argument('--menu', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'build') or output.exists():
        raise ValueError('Require a new directory under workspace build')
    output.mkdir(parents=True)
    sources = [ROOT/p for p in ('native/csol/client/characters.cpp','native/csol/character_menu.cpp',
        'native/csol/character_menu.hpp','native/csol/character_catalog.cpp','native/csol/config/characters.json')]
    hashes = {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    cpp = output/'character-menu-fixture.cpp'
    cpp.write_text(FIXTURE.replace('SURFACE_METHODS', methods()), encoding='utf8')
    reference = json.loads(args.reference_command.read_text())['arguments']
    flags = [f for f in reference if not f.startswith('/Fo') and not f.endswith('.cpp')]
    flags += ['/I'+str(ROOT)]
    commands=[]
    for source, name in ((cpp,'fixture'),(ROOT/'native/csol/character_menu.cpp','menu'),(ROOT/'native/csol/character_catalog.cpp','catalog')):
        commands.append([args.clang,*flags,str(source),'/Fo'+str(output/(name+'.obj'))])
    commands.append([args.clang,'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',
        *[str(output/(name+'.obj')) for name in ('fixture','menu','catalog')],'/Fe'+str(output/'menu.exe'),
        '/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1','user32.lib','gdi32.lib'])
    commands.append([str(output/'menu.exe'),str(args.menu.resolve()),str(ROOT/'native/csol/config/characters.json')])
    results=[]
    for index, command in enumerate(commands):
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace')
        (output/f'{index}.log').write_text(result.stdout+result.stderr,encoding='utf8')
        results.append(dict(command=command,returncode=result.returncode))
        if result.returncode:
            print(result.stdout+result.stderr)
            break
    unchanged=all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha for p,sha in hashes.items())
    (output/'evidence.json').write_text(json.dumps(dict(inputs=hashes,unchanged=unchanged,commands=results,
        menuSHA256=hashlib.sha256(args.menu.read_bytes()).hexdigest(),gameplay=False,surfaceAndFontsMocked=True),indent=2),encoding='utf8')
    if result.returncode or not unchanged:raise SystemExit(1)
    print(result.stdout)


if __name__=='__main__':
    main()
