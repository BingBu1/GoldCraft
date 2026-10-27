// SPDX-License-Identifier: GPL-3.0-or-later
#include "texture_model.hpp"
#include <cstdint>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace csol {
namespace {
constexpr size_t maxBytes=128*1024*1024;
void Require(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void Range(std::span<const std::byte> data,size_t offset,size_t size){Require(offset<=data.size() && size<=data.size()-offset,"T.mdl range outside file");}
int Word(std::span<const std::byte> data,size_t offset){Range(data,offset,4);int value{};memcpy(&value,data.data()+offset,4);return value;}
void Put(std::vector<std::byte>& data,size_t offset,int value){Range(data,offset,4);memcpy(data.data()+offset,&value,4);}
std::span<const std::byte> Header(std::span<const std::byte> data){
    Require(data.size()>=244 && data.size()<=maxBytes,"invalid T.mdl input size");
    Require(Word(data,0)==0x54534449 && Word(data,4)==10,"T.mdl requires decoded IDST v10");
    const int length=Word(data,72);Require(length>=244 && size_t(length)<=data.size(),"invalid T.mdl declared length");return data.first(length);
}
int Append(std::vector<std::byte>& out,std::span<const std::byte> data){
    Require(out.size()<=maxBytes && data.size()<=maxBytes-out.size(),"combined texture model exceeds budget");const int offset=int(out.size());out.insert(out.end(),data.begin(),data.end());return offset;
}
}
std::string StudioTextureModel(const std::string& model){
    auto extension=model.size()>=4?model.substr(model.size()-4):std::string{};
    for(auto& c:extension)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(extension!=".mdl")throw std::runtime_error("invalid texture model filename");
    return model.substr(0,model.size()-4)+"T.mdl";
}
std::vector<std::byte> AppendStudioTextures(std::span<const std::byte> model,std::span<const std::byte> textures){
    model=Header(model);textures=Header(textures);
    Require(Word(model,184)==0,"model already has a texture table");
    const int count=Word(textures,180),index=Word(textures,184),skinRefs=Word(textures,192),families=Word(textures,196),skinIndex=Word(textures,200);
    Require(count>0 && count<=4096 && index>=244,"invalid external texture table");
    Require(skinRefs>0 && skinRefs<=4096 && families>0 && families<=4096 && skinIndex>=244,"invalid external skin table");
    Range(textures,size_t(index),size_t(count)*80);Range(textures,size_t(skinIndex),size_t(skinRefs)*families*2);
    const auto skin=textures.subspan(skinIndex,size_t(skinRefs)*families*2);
    for(size_t i=0;i<skin.size();i+=2){int16_t value{};memcpy(&value,skin.data()+i,2);Require(value>=0 && value<count,"external skin texture index out of range");}
    std::vector<std::byte> out(model.begin(),model.end());out.resize((out.size()+3)&~size_t(3));
    Put(out,180,count);Put(out,184,Append(out,textures.subspan(index,size_t(count)*80)));
    Put(out,192,skinRefs);Put(out,196,families);Put(out,200,Append(out,skin));
    out.resize((out.size()+3)&~size_t(3));Put(out,188,int(out.size()));
    for(int i=0;i<count;++i){
        const size_t record=size_t(index)+i*80;
        const int width=Word(textures,record+68),height=Word(textures,record+72),image=Word(textures,record+76);
        Require(width>0 && width<=4096 && height>0 && height<=4096 && image>=244,"invalid external texture image");
        const size_t bytes=size_t(width)*height+768;Range(textures,size_t(image),bytes);
        const int destination=Append(out,textures.subspan(image,bytes));Put(out,size_t(Word(out,184))+i*80+76,destination);
    }
    Put(out,72,int(out.size()));return out;
}
}
