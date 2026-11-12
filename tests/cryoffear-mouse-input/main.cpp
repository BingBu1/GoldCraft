// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/mouse_input.hpp"
#include <array>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <io.h>

struct Case
{
    std::int32_t x, y, previousX, previousY;
    float filter, hudSensitivity, sensitivity;
    std::int32_t wholeFov, defaultFov;
};
static_assert(sizeof(Case)==36);
static_assert(sizeof(guest::cof::client::MouseOutput)==16);
int main()
{
    if(_setmode(_fileno(stdin),_O_BINARY)<0||_setmode(_fileno(stdout),_O_BINARY)<0)return 4;
    Case input;
    while(std::fread(&input,sizeof(input),1,stdin)==1)
    {
        const auto output=guest::cof::client::ScaleMouse({input.previousX,input.previousY},
            {input.x,input.y,input.filter,input.hudSensitivity,input.sensitivity,input.wholeFov,input.defaultFov});
        if(std::fwrite(&output,sizeof(output),1,stdout)!=1)return 2;
    }
    return std::ferror(stdin)?3:0;
}
