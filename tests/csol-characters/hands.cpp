// SPDX-License-Identifier: GPL-3.0-or-later
// Run actual hand selection and texture uploads against all published roles.
#include "../../native/csol/client/hands.cpp"
#include "../../native/csol/model_codec.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>
namespace fs=std::filesystem;
namespace fixture {fs::path root;const csol::Character* character{};int checks{},uploads{};}
void Check(bool value){++fixture::checks;if(!value)throw std::runtime_error("hand check "+std::to_string(fixture::checks));}
namespace csol::client {
cl_enginefunc_t engine{};
Asset::~Asset(){}
const Character* ViewCharacter(){return fixture::character;}
bool Exists(const char* p){return fs::is_regular_file(fixture::root/p);}
std::vector<std::byte> ReadResource(const char* p){std::ifstream f(fixture::root/p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(p);auto n=f.tellg();std::vector<std::byte>b(size_t(n),std::byte{});f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);return b;}
GLuint UploadImage(const Image& image){++fixture::uploads;GLuint id;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.Width,image.Height,0,GL_RGBA,GL_UNSIGNED_BYTE,image.RGBA.data());return id;}
}
void Console(const char* format,...){char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);throw std::runtime_error(text);}
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;
    try {
        Check(argc==3);fixture::root=fs::path(argv[1]);
        Check(glfwInit()!=0);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);auto* window=glfwCreateWindow(32,32,"Character hands test",nullptr,nullptr);Check(window!=nullptr);
        glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;Check(glewInit()==GLEW_OK);while(glGetError()){}
        engine.Con_Printf=Console;
        auto data=ReadResource("models/csol/characters.json");auto catalog=ParseCharacters({reinterpret_cast<char*>(data.data()),data.size()});
        // Exercise consecutive swaps, including same style across genders and
        // style0 after a nonzero style. Compare GPU bytes, not handle identity.
        for(const auto& c:catalog){fixture::character=&c;UpdateHands();const int uploadsBefore=fixture::uploads;UpdateHands();Check(fixture::uploads==uploadsBefore);
            for(int i=0;i<5;++i){
                const bool expected=!c.hand.empty() && (c.male?(i!=2&&i!=3):(i==2||i==3));Check(bool(handles[i])==expected);
                if(!expected)continue;
                auto image=DecodeTGA(ReadResource(("gfx/tattoo/"+c.hand+"_"+suffixes[i]).c_str()));
                Check(!tattooStyles.contains(Lower(c.hand)));
                std::vector<unsigned char> gpu(image.RGBA.size());glBindTexture(GL_TEXTURE_2D,handles[i]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,gpu.data());Check(gpu==image.RGBA);
                Material m;m.hand=i;m.originalHeight= i==0?502:i==1||i==2?505:i==3?506:252;GLuint texture=0;float w=1,h=1,uv[2]{};ApplyHand(m,texture,w,h,uv);Check(texture==handles[i]&&w==(i==4?256:512)&&h==w);
            }
        }
        const auto published=fixture::root;fixture::root=fs::path(argv[2]);
        for(const char* name:{"models/v_beamgun.mdl","models/v_beamgunle.mdl","models/v_tknifeex2.mdl"}){
            Asset asset;asset.bytes=ReadResource(name);DecodeStudioModel(asset.bytes);const auto& h=asset.Header();
            for(const auto& texture:asset.Data<mstudiotexture_t>(h.textureindex,h.numtextures)){
                Material m;auto n=Lower(Name(texture.name));if(n.starts_with('#'))n.erase(0,7);
                constexpr const char* labels[]{"cso_highq_hand__long_512.bmp","cso_highq_hand_512.bmp","cso_girl_hand_long.bmp","cso_girl_hand.bmp","hand.bmp"};
                for(int i=0;i<5;++i)if(n==labels[i])m.hand=i;asset.materials.push_back(m);
            }
            FindHandModels(asset);bool found=false;
            for(size_t group=0;group<asset.handModels.size();++group){auto pair=asset.handModels[group];if(pair[0]<0||pair[1]<0)continue;found=true;Check(pair[0]!=pair[1]);
                for(const auto& c:catalog){fixture::character=&c;Check(CharacterHandSubmodel(asset,int(group),-1)==pair[c.male?0:1]);}}
            Check(found);std::cout<<name<<": gender bodyparts verified\n";
        }
        fixture::character=nullptr;UpdateHands();for(auto id:handles)Check(!id);Check(glGetError()==GL_NO_ERROR);ResetHands();glfwDestroyWindow(window);glfwTerminate();
        std::cout<<"Character hands GL checks: "<<fixture::checks<<" passed; uploads "<<fixture::uploads<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
