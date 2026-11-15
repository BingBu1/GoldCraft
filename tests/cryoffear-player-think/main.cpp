// SPDX-License-Identifier: GPL-3.0-or-later
#define main M16CompositionMain
#include "../cryoffear-m16-weapon/main.cpp"
#undef main

namespace
{
std::vector<std::uint32_t> taskEvents;
unsigned taskCalls{},reschedule{};
float rescheduleDelay{};
bool OracleDispatch(void*,CBasePlayer& owner,PlayerThinkKind kind) noexcept
{
    taskEvents.insert(taskEvents.end(),{unsigned(kind)+1,Bits(playerThink.Deadline()),
        Bits(playerThink.InternalDeadline()),Bits(globals.time)});
    if(++taskCalls==1&&reschedule)
        Check(playerThink.Schedule(owner,PlayerThinkKind(reschedule-1),rescheduleDelay,1,17));
    return true;
}
void Snapshot(std::vector<std::uint32_t>& out,PlayerThinkKind kind)
{
    out.insert(out.end(),{Bits(playerThink.Deadline()),Bits(playerThink.InternalDeadline()),
        Bits(globals.time),unsigned(kind)+1,taskCalls});
}
}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    char* compositionArgs[]={argv[0],argv[1]};M16CompositionMain(2,compositionArgs);
    std::ifstream cases(argv[2]),expected(argv[3]);Check(cases.good()&&expected.good());
    std::string line,answer;unsigned rows{},mismatches{};
    while(std::getline(cases,line))
    {
        Check(bool(std::getline(expected,answer)));std::istringstream in(line),want(answer);
        std::uint64_t r[9]{};for(auto& v:r)Check(bool(in>>std::hex>>v));
        Initialize();globals.time=Float(std::uint32_t(r[0]));
        player.pev->flags=int(r[4]);taskCalls=0;taskEvents.clear();reschedule=unsigned(r[5]);
        rescheduleDelay=Float(std::uint32_t(r[6]));
        Check(playerThink.Bind(player,{nullptr,TaskCurrent,OracleDispatch,1,17,3}));
        Check(playerThink.Schedule(player,PlayerThinkKind::Pushback,Float(std::uint32_t(r[1])),1,17));
        std::vector<std::uint32_t> actual;auto kind=PlayerThinkKind::Pushback;Snapshot(actual,kind);
        for(unsigned at:{2U,7U})
        {
            const auto result=playerThink.Run(Float(std::uint32_t(r[at])),std::bit_cast<double>(r[at+1]));
            Check(result==PlayerThinkResult::Deferred||result==PlayerThinkResult::Dispatched);
            if(reschedule&&taskCalls)kind=PlayerThinkKind(reschedule-1);
            Snapshot(actual,kind);
        }
        actual.insert(actual.end(),taskEvents.begin(),taskEvents.end());
        std::vector<std::uint32_t> target;std::uint32_t v{};while(want>>std::hex>>v)target.push_back(v);
        if(actual!=target){++mismatches;if(mismatches<5)std::cerr<<"scheduler mismatch row "<<rows<<'\n';}
        ++rows;
    }
    Check(!std::getline(expected,answer)&&rows>1000&&mismatches==0);
    // Host-specific authority/invalid-input protection, separately from oracle.
    Initialize();Ready();Check(playerThink.Schedule(player,PlayerThinkKind::Pushback,.23F,1,17));
    const float due=playerThink.Deadline();
    Check(playerThink.Run(-1,10)==PlayerThinkResult::InvalidInput);
    Check(playerThink.Run(0,NAN)==PlayerThinkResult::InvalidInput);
    Check(!playerThink.Schedule(player,PlayerThinkKind::Pushback,INFINITY,1,17)&&playerThink.Deadline()==due);
    Check(!playerThink.Schedule(player,PlayerThinkKind(2),.1F,1,17));
    Check(playerThink.Bind(player,{nullptr,TaskCurrent,[](void*,CBasePlayer&,PlayerThinkKind) noexcept
        {playerThink.Invalidate();return true;},1,17,3}));
    Check(playerThink.Schedule(player,PlayerThinkKind::Pushback,.1F,1,17));
    Check(playerThink.Run(0,20)==PlayerThinkResult::Cancelled);
    std::cout<<rows<<" original scheduler cases; "<<checks<<" SDK checks; zero mismatches\n";
}
