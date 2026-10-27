// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/texture_model.hpp"
#include "../../native/csol/model_codec.hpp"
#include "../../native/csol/dependencies.hpp"
#include "texture_fixture.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <source_location>

using namespace texture_fixture;
namespace fs=std::filesystem;
int checks{};
void Check(bool good,const std::source_location loc=std::source_location::current()) {
    ++checks;if(!good)throw std::runtime_error("texture model line "+std::to_string(loc.line()));
}
template<class F> void Reject(F&& operation) {
    bool rejected=false;try{operation();}catch(const std::runtime_error&){rejected=true;}Check(rejected);
}
Bytes Read(const fs::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(path.string());
    Bytes bytes(size_t(f.tellg()));f.seekg(0);f.read((char*)bytes.data(),bytes.size());Check(bool(f));return bytes;
}
void UnchangedGeometry(const Bytes& main,const Bytes& joined) {
    Check(joined.size()>main.size());
    for(size_t i=0;i<main.size();++i)
        if((i<72 || i>=76) && (i<180 || i>=204))Check(main[i]==joined[i]);
}
void SameTextures(const Bytes& reference,const Bytes& joined) {
    for(int field:{180,192,196})Check(Word(reference,field)==Word(joined,field));
    const int skins=Word(reference,192)*Word(reference,196)*2;
    Check(!memcmp(reference.data()+Word(reference,200),joined.data()+Word(joined,200),skins));
    for(int i=0;i<Word(reference,180);++i) {
        const int a=Word(reference,184)+i*80,b=Word(joined,184)+i*80;
        Check(!memcmp(reference.data()+a,joined.data()+b,76)); // names, flags, dimensions
        const int size=Word(reference,a+68)*Word(reference,a+72)+768;
        Check(!memcmp(reference.data()+Word(reference,a+76),joined.data()+Word(joined,b+76),size));
    }
}
int main(int argc,char** argv){try {
    Check(argc==2);const fs::path root=argv[1];
    const auto main=Read(root/"models/arcticorange.mdl"),textures=Read(root/"models/arcticorangeT.mdl");
    const auto merged=csol::AppendStudioTextures(main,textures);
    UnchangedGeometry(main,merged);SameTextures(textures,merged);
    for(const char* name:{"models/v_ak47.mdl","models/zombie_plague/v_grenade_fire.mdl"}) {
        const auto source=Read(root/name);const auto split=Split(source);
        const auto joined=csol::AppendStudioTextures(split.model,split.textures);
        UnchangedGeometry(split.model,joined);SameTextures(source,joined);
    }
    Check(csol::StudioTextureModel("models/custom/V_Future.MDL")=="models/custom/V_FutureT.mdl");
    Reject([]{csol::StudioTextureModel("bad");});
    // A nonzero count in a split main header does not imply embedded textures.
    auto countOnly=main;Put(countOnly,180,3);SameTextures(textures,csol::AppendStudioTextures(countOnly,textures));
    Reject([&]{csol::AppendStudioTextures(merged,textures);});
    for(int field:{0,4,72,180,184,192,196,200}) {
        auto bad=textures;Put(bad,field,-1);Reject([&]{csol::AppendStudioTextures(main,bad);});
    }
    for(int field:{68,72,76}) {
        auto bad=textures;Put(bad,Word(bad,184)+field,0x7fffffff);Reject([&]{csol::AppendStudioTextures(main,bad);});
    }
    auto badSkin=textures;badSkin[Word(badSkin,200)]=std::byte{255};badSkin[Word(badSkin,200)+1]=std::byte{127};
    Reject([&]{csol::AppendStudioTextures(main,badSkin);});
    auto truncated=textures;truncated.pop_back();Put(truncated,72,int(truncated.size()));
    Reject([&]{csol::AppendStudioTextures(main,truncated);});
    // Real texture-only files have no bones/sequences. The CSO decoder must not
    // impose the main-model layout on them. Version-tagged synthetic bytes test
    // acceptance only, not cipher correctness or the resulting image content.
    for(int version:{20,21}) {
        auto encoded=textures;Put(encoded,4,version);Check(csol::DecodeStudioModel(encoded));
        Check(Word(encoded,4)==10 && Word(encoded,140)==0 && Word(encoded,164)==0);
        Check(csol::AppendStudioTextures(main,encoded).size()==merged.size());
    }
    std::map<std::string,Bytes> files{{"models/custom/v_split.mdl",main},{"models/custom/v_splitT.mdl",textures}};
    auto read=[&](const std::string& p){auto it=files.find(p);if(it==files.end())throw std::runtime_error("missing fixture "+p);return it->second;};
    auto exists=[&](const std::string& p){return files.contains(p);};
    const auto dependencies=csol::WeaponDependencies("models/custom/v_split.mdl",read,exists);
    Check(dependencies==std::vector<std::string>({"models/custom/v_split.mdl","models/custom/v_splitT.mdl"}));
    // Companion is a texture leaf, not another main model needing sequences.
    files.erase("models/custom/v_splitT.mdl");Reject([&]{csol::WeaponDependencies("models/custom/v_split.mdl",read,exists);});
    files["models/custom/v_splitT.mdl"]=badSkin;Reject([&]{csol::WeaponDependencies("models/custom/v_split.mdl",read,exists);});
    files["models/custom/v_splitT.mdl"]=textures;
    const auto untouched=files;
    csol::WeaponDependencies("models/custom/v_split.mdl",read,exists);Check(files==untouched);
    // Packaging a redirected main under a new name must still find its physical
    // companion after relocation, exactly as the deployed _csol.json describes.
    const std::vector<csol::ModelRedirect> redirects{{"models/custom/v_alias.mdl","models/custom/v_split.mdl","",{}}};
    const auto beforePacking=csol::WeaponDependencies("models/custom/v_alias.mdl",read,exists,redirects);
    Check(beforePacking==std::vector<std::string>({"models/custom/v_alias.mdl","models/custom/v_splitT.mdl"}));
    const auto descriptor=csol::SerializeModelSource(csol::ResolveModelSource("models/custom/v_alias.mdl",read,exists,redirects));
    files["models/custom/v_alias.mdl"]=main;
    files["models/custom/v_alias_csol.json"]={(const std::byte*)descriptor.data(),(const std::byte*)descriptor.data()+descriptor.size()};
    files.erase("models/custom/v_split.mdl");
    const auto afterPacking=csol::WeaponDependencies("models/custom/v_alias.mdl",read,exists);
    Check(afterPacking==std::vector<std::string>({"models/custom/v_alias.mdl","models/custom/v_alias_csol.json","models/custom/v_splitT.mdl"}));
    std::cout<<"PASS "<<checks<<" checks; real split model, 2 split weapon fixtures, dependency closure and corrupt-input rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
