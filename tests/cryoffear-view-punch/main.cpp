// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/view_punch.hpp"
#include <event_args.h>
#include <ref_params.h>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fcntl.h>
#include <io.h>
using namespace guest::cof::client;
static_assert(sizeof(void*) == 4);
static_assert(offsetof(event_args_t, origin) == 8);
static_assert(offsetof(ref_params_t, viewangles) == 0xC);
static_assert(offsetof(ref_params_t, frametime) == 0x3C);
static_assert(offsetof(ref_params_t, punchangle) == 0xA0);
static_assert(offsetof(ref_params_t, nextView) == 0xE0);
namespace
{
void Check(bool value, int line) { if (!value) { std::fprintf(stderr,"FAIL %d\n",line); std::abort(); } }
#define CHECK(x) Check((x),__LINE__)
struct Input { float values[13]; int clients, camera, mode; };
struct Output { float values[9]; PunchResult event, stage; };
static_assert(sizeof(Input)==64 && sizeof(Output)==44);
Output Run(const Input& input)
{
    ViewPunchState state;
    event_args_t event{};
    ref_params_t view{};
    std::memcpy(state.angle.data(),input.values,12);
    std::memcpy(state.velocity.data(),input.values+3,12);
    std::memcpy(event.origin,input.values+6,12);
    std::memcpy(view.viewangles,input.values+9,12);
    view.frametime=input.values[12];
    Output output{};
    output.event=ViewPunchEvent(state,event,input.camera!=0,input.mode);
    output.stage=ViewPunchAtStage(state,view,input.clients);
    std::memcpy(output.values,state.angle.data(),12);
    std::memcpy(output.values+3,state.velocity.data(),12);
    std::memcpy(output.values+6,view.viewangles,12);
    return output;
}
}
int main(int argc,char** argv)
{
    if(argc==2 && std::strcmp(argv[1],"--oracle")==0)
    {
        _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
        Input input;
        while(std::fread(&input,sizeof(input),1,stdin)==1)
        {
            const auto output=Run(input);
            if(std::fwrite(&output,sizeof(output),1,stdout)!=1)return 2;
        }
        return std::ferror(stdin)?3:0;
    }
    ViewPunchState state{};
    event_args_t event{}; event.origin[0]=2;event.origin[1]=-1;event.origin[2]=.5F;
    CHECK(ViewPunchEvent(state,event,true,0)==PunchResult::CameraBlocked);
    CHECK(state.velocity[0]==0);
    CHECK(ViewPunchEvent(state,event,true,1)==PunchResult::Applied);
    CHECK(state.velocity[0]==-40 && state.velocity[1]==-20 && state.velocity[2]==10);
    ref_params_t view{}; view.viewangles[0]=11;view.frametime=.01F;
    view.punchangle[0]=7;
    CHECK(ViewPunchAtStage(state,view,32)==PunchResult::Applied);
    CHECK(view.viewangles[0]==11); // Adds old zero angle, not advanced angle or engine punch.
    CHECK(state.angle[0]==-40.0F*view.frametime && view.punchangle[0]==7);
    const auto saved=state; const auto savedView=view;
    event.origin[1]=std::numeric_limits<float>::infinity();
    CHECK(ViewPunchEvent(state,event,false,0)==PunchResult::InvalidInput);
    CHECK(std::memcmp(&saved,&state,sizeof(state))==0);
    view.frametime=std::numeric_limits<float>::quiet_NaN();
    const auto invalidView=view;
    CHECK(ViewPunchAtStage(state,view,32)==PunchResult::InvalidInput);
    CHECK(std::memcmp(&saved,&state,sizeof(state))==0);
    CHECK(std::memcmp(&invalidView,&view,sizeof(view))==0);
    view=savedView;
    state={{1,2,3},{4,5,6}};
    CHECK(ViewPunchAtStage(state,view,1)==PunchResult::Applied);
    CHECK(state.velocity[0]==4 && state.velocity[1]==5 && state.velocity[2]==6);
    std::puts("viewpunch SDK event/stage, gate, ordering, nonfinite atomic rejection and SP velocity checks passed");
}
