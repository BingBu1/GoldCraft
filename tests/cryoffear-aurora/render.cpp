// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/aurora_render.hpp"
#include <metahook.h>
#include <com_model.h>
#include <triangleapi.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>
using namespace guest::cof::client;
static_assert(sizeof(void*)==4 && offsetof(model_t,numframes)==0x48);
static_assert(offsetof(triangleapi_t,SpriteTexture)==0x2C);
static_assert(offsetof(triangleapi_t,TexCoord2f)==0x18);
namespace
{
struct Input { float values[20]; int state,frames,texture; };
struct Output { float values[25]; int textureFrame,mode,primitive,vertices,ended,status; };
static_assert(sizeof(Input)==92 && sizeof(Output)==124);
Output output;
int textureSuccess;
void Mode(int n){output.mode=n;}
void Begin(int n){output.primitive=n;}
void End(){++output.ended;}
void Color(float r,float g,float b,float a){output.values[1]=r;output.values[2]=g;output.values[3]=b;output.values[4]=a;}
void UV(float u,float v){output.values[17+output.vertices*2]=u;output.values[18+output.vertices*2]=v;}
void Vertex(float* p){std::memcpy(output.values+5+output.vertices*3,p,12);++output.vertices;}
int Texture(model_t*,int frame){output.textureFrame=frame;return textureSuccess;}
triangleapi_t API(){triangleapi_t tri{};tri.RenderMode=Mode;tri.Begin=Begin;tri.End=End;tri.Color4f=Color;
    tri.TexCoord2f=UV;tri.Vertex3fv=Vertex;tri.SpriteTexture=Texture;return tri;}
Output Run(const Input& input,const AuroraDegreeTable& table)
{
    output={};output.textureFrame=-777;output.mode=-777;output.primitive=-777;
    AuroraRenderParticle p;
    std::memcpy(p.position.data(),input.values,12);std::memcpy(p.color.data(),input.values+3,16);
    p.size=input.values[7];p.angle=input.values[8];p.frame=input.values[9];
    std::array<float,3> right,up;
    std::memcpy(right.data(),input.values+10,12);std::memcpy(up.data(),input.values+13,12);
    AuroraRenderFade fade{input.state,input.values[16],input.values[17],input.values[18]};
    model_t sprite{};sprite.numframes=input.frames;textureSuccess=input.texture;
    auto tri=API();
    output.status=static_cast<int>(DrawAuroraParticle(p,table,{true,0,0},fade,right,up,&sprite,&tri));
    output.values[0]=p.frame;
    return output;
}
void Check(bool v,int line){if(!v){std::fprintf(stderr,"FAIL %d\n",line);std::abort();}}
#define CHECK(x) Check((x),__LINE__)
}
int main(int argc,char** argv)
{
    AuroraDegreeTable table;PrepareAuroraDegreeTable(table);
    if(argc==2 && std::strcmp(argv[1],"--oracle")==0)
    {
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);Input input;
        while(std::fread(&input,sizeof(input),1,stdin)==1){const auto result=Run(input,table);
            if(std::fwrite(&result,sizeof(result),1,stdout)!=1)return 2;}
        return 0;
    }
    Input input{{1,2,3,.016F,.02F,.024F,3,20,45,10, 0,1,0, 0,0,1, 0,0,1,0},0,1,1};
    auto result=Run(input,table);
    CHECK(result.status==0 && result.vertices==4 && result.primitive==2 && result.ended==1);
    CHECK(result.values[0]==1 && result.textureFrame==1 && result.mode==5);
    CHECK(result.values[17]==0 && result.values[19]==1 && result.values[22]==1);
    input.texture=0;result=Run(input,table);CHECK(result.status==2 && result.vertices==0);
    input.values[7]=0;result=Run(input,table);CHECK(result.status==1 && result.textureFrame==-777);
    AuroraRenderParticle p{};auto tri=API();model_t sprite{};
    CHECK(DrawAuroraParticle(p,table,{}, {}, {}, {}, &sprite,&tri)==AuroraDrawResult::UnresolvedSeed);
    CHECK(DrawAuroraParticle(p,table,{true,0,1}, {}, {}, {}, &sprite,&tri)==AuroraDrawResult::UnsupportedControl);
    CHECK(FinishAuroraFrame(&tri) && output.mode==0);
    std::puts("Aurora real SDK quad, UV, sprite frame endpoint, failed texture, control seed and mode restoration checks passed");
}
