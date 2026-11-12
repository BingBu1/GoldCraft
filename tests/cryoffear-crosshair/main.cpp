// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/crosshair.hpp"
#include "../../native/guest/cof/crosshair_protocol.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <io.h>
struct Case
{
    std::int32_t width,height,weapon,aiming,telescope;
    float enabled;
    std::uint32_t scalar;
};
struct Output
{
    std::uint32_t count{};
    std::array<guest::cof::client::CrosshairRect,3> bars{};
};
static_assert(sizeof(Case)==28&&sizeof(Output)==52);
int main(int argc,char** argv)
{
    if(_setmode(_fileno(stdin),_O_BINARY)<0||_setmode(_fileno(stdout),_O_BINARY)<0)return 2;
    if(argc==2&&!std::strcmp(argv[1],"--quantize"))
    {
        float value;
        while(std::fread(&value,sizeof(value),1,stdin)==1)
        {
            std::uint8_t scalar{};
            const std::int32_t result=guest::cof::QuantizeCrosshair(value,scalar)?scalar:-1;
            if(std::fwrite(&result,sizeof(result),1,stdout)!=1)return 3;
        }
        return std::ferror(stdin)?4:0;
    }
    Case input;
    while(std::fread(&input,sizeof(input),1,stdin)==1)
    {
        Output result;
        if(guest::cof::client::PistolCrosshair({input.width,input.height,input.weapon,0,true,true,
            input.aiming!=0,input.telescope!=0,input.enabled,std::uint8_t(input.scalar)},result.bars))result.count=3;
        if(std::fwrite(&result,sizeof(result),1,stdout)!=1)return 3;
    }
    return std::ferror(stdin)?4:0;
}
