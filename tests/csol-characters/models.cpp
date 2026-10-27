// SPDX-License-Identifier: GPL-3.0-or-later
// Every published character passes the real client asset and OpenGL upload path.
#include "../../native/csol/client/assets.hpp"
#include "../../native/csol/client/characters.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <set>
namespace fs=std::filesystem;
namespace fixture {fs::path root;}
namespace csol::client {
cl_enginefunc_t engine{};
const Character* ViewCharacter(){return nullptr;}
bool Exists(const char* p){return fs::is_regular_file(fixture::root/p);}
std::vector<std::byte> ReadResource(const char* p){std::ifstream f(fixture::root/p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(p);auto n=f.tellg();std::vector<std::byte>b(size_t(n),std::byte{});f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);return b;}
model_t* LoadEngineModel(const char* p){if(!Exists(p))throw std::runtime_error(p);return nullptr;}
}
void Console(const char* format,...){char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);throw std::runtime_error(text);}
int main(int argc,char**argv){using namespace csol;using namespace csol::client;try{
    if(argc!=2)return 1;fixture::root=fs::path(argv[1]);if(!glfwInit())return 2;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(32,32,"Character asset test",nullptr,nullptr);if(!window)return 3;glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)return 4;while(glGetError()){}
    engine.Con_Printf=Console;auto data=ReadResource("models/csol/characters.json");auto catalog=ParseCharacters({reinterpret_cast<char*>(data.data()),data.size()});
    std::set<std::string> models;unsigned materials=0,sequences=0;
    for(const auto& c:catalog){if(!models.insert(c.model).second)continue;model_t model{};strcpy_s(model.name,c.ModelPath().c_str());
        try{auto* a=GetAsset(&model);if(!a||!a->vao||a->Header().numseq<350)throw std::runtime_error("incomplete character");
            materials+=a->materials.size();sequences+=a->Header().numseq;if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GL error");ClearAssets();}
        catch(const std::exception& e){throw std::runtime_error(c.model+": "+e.what());}
    }
    glfwDestroyWindow(window);glfwTerminate();std::cout<<"Character asset GPU path: "<<models.size()<<" models, "<<materials<<" materials, "<<sequences<<" sequences passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
