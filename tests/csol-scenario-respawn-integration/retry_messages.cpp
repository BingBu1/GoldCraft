// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the actual SDK writer boundary; only the new retry contracts execute.
#define main PriorRevivalMessagesMain
#include "messages.cpp"
#undef main

int main()
{
    try
    {
        Install(); CCSOLScenarioMessages messages;
        Check(!messages.SendRetryCountdown(10)&&!messages.SendRetryAnnouncement(1,1)&&packets.empty(),
            "unregistered retry writers emit no packet");
        Check(messages.Register(),"retry writer channels registered");
        for (int value=-1;value<=10;++value)
        {
            packets.clear();Check(messages.SendRetryCountdown(value),"original retry countdown sent");
            Check(packets.size()==1&&packets[0].destination==MSG_ALL&&packets[0].recipient==nullptr&&
                packets[0].id==ids.at("MPToCL")&&packets[0].writes=="bbb"&&packets[0].bytes.size()==3,
                "retry countdown is exact three-byte broadcast");
            hud::Message decoded;
            Check(hud::DecodeMP(packets[0].bytes,decoded)==hud::ParseResult::Accepted&&
                decoded.kind==hud::Kind::RetryCountdown&&decoded.count==value,"actual client decodes signed retry count");
        }
        for(int player:{1,32})for(int round:{1,255})
        {
            packets.clear();Check(messages.SendRetryAnnouncement(player,round),"retry actor sent");
            Check(packets.size()==1&&packets[0].destination==MSG_ALL&&packets[0].recipient==nullptr&&
                packets[0].id==ids.at("MPToCL")&&packets[0].writes=="bbbb"&&packets[0].bytes.size()==4,
                "retry actor is exact four-byte broadcast");
            hud::Message decoded;
            Check(hud::DecodeMP(packets[0].bytes,decoded)==hud::ParseResult::Accepted&&
                decoded.kind==hud::Kind::RetryAnnouncement&&decoded.index==unsigned(player)&&decoded.count==round,
                "actual client decodes actor and one-based round");
        }
        packets.clear();
        for(int value:{INT_MIN,-2,11,INT_MAX})Check(!messages.SendRetryCountdown(value),"invalid retry value rejected");
        for(int value:{INT_MIN,0,33,INT_MAX})Check(!messages.SendRetryAnnouncement(value,1),"invalid player rejected");
        for(int value:{INT_MIN,0,256,INT_MAX})Check(!messages.SendRetryAnnouncement(1,value),"invalid round rejected");
        Check(packets.empty()&&!opened,"rejections do not open a partial packet");
        const auto full=g_engfuncs;
        for(int missing=0;missing<3;++missing)
        {
            g_engfuncs=full;
            if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;
            if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
            if(missing==2)g_engfuncs.pfnWriteByte=nullptr;
            Check(!messages.SendRetryCountdown(10)&&!messages.SendRetryAnnouncement(1,1)&&packets.empty(),
                "missing engine boundary rejects both retry writers");
        }
        g_engfuncs=full;
        for(bool announcement:{false,true})
        {
            packets.clear();unsigned callbacks{};
            reentry=[&](char){++callbacks;Check(!messages.SendRetryCountdown(1)&&!messages.SendRetryAnnouncement(1,1),
                "both retry writers reject nested Begin/write/End callbacks");};
            const bool result=announcement?messages.SendRetryAnnouncement(1,2):messages.SendRetryCountdown(10);
            reentry={};Check(result&&packets.size()==1&&callbacks>3&&!opened,"outer retry packet survives reentry");
            Check(messages.SendRetryCountdown(-1),"retry scope releases guard after complete packet");
        }
        packets.clear();
        reentry=[&](char type){if(type=='m')messages=CCSOLScenarioMessages{};};
        Check(messages.SendRetryAnnouncement(1,2),"message state can retire after broadcast starts");reentry={};
        Check(packets.size()==1&&packets[0].id==ids.at("MPToCL")&&packets[0].bytes.size()==4,
            "inflight retry uses captured channel identity");
        Check(!messages.SendRetryCountdown(10),"retired object cannot start next retry");
        std::cout<<"retry integration writer checks="<<checks<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<checks<<": "<<error.what()<<'\n';return 1;}
}
