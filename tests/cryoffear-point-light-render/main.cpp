// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/point_light_render.hpp"
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <io.h>
#include <fcntl.h>
using namespace guest::cof::client;
int main(int argc,char** argv){
    _setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stdin),_O_BINARY);
    if(argc==2&&!std::strcmp(argv[1],"--brush")) {
        std::array<float,16> row;
        while(std::fread(row.data(),sizeof(row),1,stdin)==1) {
            PointLightBrush brush{};
            for(unsigned i=0;i<3;++i){brush.origin[i]=row[3+i];brush.forward[i]=row[6+i];brush.right[i]=row[9+i];brush.up[i]=row[12+i];}
            brush.rotated=row[15]!=0.0F;
            const auto local=PointLightBrushOrigin(brush,{row[0],row[1],row[2]});
            std::fwrite(local.data(),sizeof(local),1,stdout);
        }
        return 0;
    }
    std::array<std::uint8_t,128> a1;BuildPointLightAttenuation1D(a1);std::fwrite(a1.data(),1,a1.size(),stdout);
    std::array<std::uint8_t,128*128> a2;BuildPointLightAttenuation2D(a2);std::fwrite(a2.data(),1,a2.size(),stdout);
    std::array<std::uint8_t,32*32*32*3> a3;BuildPointLightAttenuation3D(a3);std::fwrite(a3.data(),1,a3.size(),stdout);
    std::array<std::uint8_t,6*32*32*3> cube;BuildPointLightCube(cube);std::fwrite(cube.data(),1,cube.size(),stdout);
    for(unsigned i=0;i<1024;++i){std::array<float,4> g;if(std::fread(g.data(),sizeof(g),1,stdin)!=1)return 2;float result=PointLightColor(g[0],{g[1],g[2],g[3]});std::fwrite(&result,4,1,stdout);}
    std::array<float,12> row;
    while(std::fread(row.data(),sizeof(row),1,stdin)==1){PointLightBasis b{};for(unsigned i=0;i<3;++i){b.normal[i]=row[i];b.tangent[i]=row[i+3];b.bitangent[i]=row[i+6];}const auto matrix=PointLightMatrix(b,{row[9],row[10],row[11]});std::fwrite(matrix.data(),sizeof(matrix),1,stdout);PointLight light{};light.origin={row[9],row[10],row[11]};light.radius=row[0];for(unsigned axis=0;axis<3;++axis){const auto plane=PointLightPlane(axis,light);std::fwrite(plane.data(),sizeof(plane),1,stdout);}}
}
