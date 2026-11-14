// SPDX-License-Identifier: GPL-3.0-or-later
#define main ExistingMusicServerTests
#include "server.cpp"
#undef main

int main()
{
    using namespace fixture;
    using namespace csol;
    using namespace csol::scenario;
    if(ExistingMusicServerTests())return 1;
    try
    {
        RoundTable rounds;rounds.headerRecord=6;rounds.source.records.resize(8);rounds.rows.resize(1);
        rounds.rows[0].recordIndex=7;rounds.source.records[7].fields.resize(12);
        rounds.source.records[7].fields[10]="5";
        ResourceError error;loading=true;
        auto access=std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLScenarioMusic music(access);
        const auto firstPath=paths.size();
        Check(music.Precache(rounds,error));
        Check(paths.size()==firstPath+2);
        const auto coldPackets=packets.size();
        Check(!music.SetNight(1,true)&&packets.size()==coldPackets);
        Check(music.Revision()==0);
        music.Reset();
        missingPath="bgm/Play_BGM_Rex.mp3";
        Check(!music.Precache(rounds,error,true)&&error.path==missingPath);
        missingPath.clear();music.Reset();
        const auto preparedPaths=paths.size();
        Check(music.Precache(rounds,error,true));
        Check(paths.size()==preparedPaths+6&&paths.back()=="bgm/BGM_zombie_attack.mp3");
        loading=false;
        const auto firstPacket=packets.size();
        Check(music.SetNight(4,false)&&packets.size()==firstPacket);
        Check(music.Select(5)&&music.Revision()==1);
        Check(packets.back().action==music::Action::Loop);
        Check(music.SetNight(4,true));
        Check(packets.back().path=="bgm/Play_BGM_Rex.mp3"&&packets.back().action==music::Action::PlayThenLoop);
        Check(packets.back().nextPath=="bgm/BGM_Rex_loop.mp3"&&music::Command(packets.back())=="mp3 play \"bgm/Play_BGM_Rex.mp3\"\n");
        const auto nightPackets=packets.size(),nightRevision=packets.back().revision;
        for(int i=0;i<100;++i)Check(music.SetNight(4,true));
        Check(packets.size()==nightPackets&&music.Revision()==1);
        Check(!music.SetNight(0,false)&&!music.SetNight(3,false)&&packets.size()==nightPackets);
        // Map requests genuinely update the retained intent. Night owns only
        // the current output, not the trigger's map selection lifetime.
        Check(music.Select(1)&&music.Revision()==2&&packets.size()==nightPackets);
        Check(music.Fade()&&music.Revision()==3&&packets.size()==nightPackets);
        Check(music.Select(5)&&music.Revision()==4&&packets.size()==nightPackets);
        CBasePlayer player;
        Check(music.Replay(player));
        Check(destination==MSG_ONE&&packets.back().revision==nightRevision&&
              packets.back().path=="bgm/Play_BGM_Rex.mp3"&&packets.back().action==music::Action::PlayThenLoop&&
              packets.back().nextPath=="bgm/BGM_Rex_loop.mp3");
        Check(music.SetNight(4,false));
        Check(packets.back().path=="bgm/Scenario_Normal.mp3"&&packets.back().action==music::Action::Loop);
        Check(packets.back().revision>nightRevision&&music.Revision()==4);
        const auto released=packets.size();
        Check(music.SetNight(5,false)&&packets.size()==released);
        Check(music.SetNight(5,true)&&packets.size()==released+1);
        Check(music.Fade()&&music.Revision()==5&&packets.size()==released+1);
        Check(music.SetNight(5,false)&&packets.back().action==music::Action::Fade);
        Check(music.SetNight(6,true));
        const auto beforeReset=packets.size();
        music.Reset();
        Check(packets.size()==beforeReset+1&&packets.back().action==music::Action::Stop);
        Check(music.Revision()==0&&!music.SetNight(7,true));
        const auto afterReset=packets.size();
        Check(music.Replay(player)&&packets.size()==afterReset);

        loading=true;
        CCSOLScenarioMusic cancelled(std::make_shared<CSOLScenarioEntityAccess>());
        Check(cancelled.Precache(rounds,error,true));
        Check(cancelled.Select(5));
        bool nested=true;
        onByte=[&]{nested=cancelled.SetNight(1,true);};
        Check(cancelled.SetNight(1,true)&&!nested&&depth==0&&maxDepth==1);
        onEnd=[&]{cancelled.Reset();};
        Check(!cancelled.SetNight(2,true)&&cancelled.Revision()==0);
        Check(!cancelled.SetNight(2,true));
        CCSOLScenarioMusic baseline(std::make_shared<CSOLScenarioEntityAccess>());
        Check(baseline.Precache(rounds,error,true)&&baseline.SetNight(1,true));
        Check(baseline.SetNight(1,false)&&packets.back().action==music::Action::Stop);
        auto retiredAccess=std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLScenarioMusic retired(retiredAccess);
        Check(retired.Precache(rounds,error,true));
        const auto beforeRetire=packets.size();retiredAccess->Revoke();
        Check(!retired.SetNight(1,true)&&packets.size()==beforeRetire);
        CCSOLScenarioMusic phases(std::make_shared<CSOLScenarioEntityAccess>());
        Check(phases.Precache(rounds,error,true)&&phases.Select(5));
        Check(phases.SetDusk(10)&&packets.back().action==music::Action::Play&&
              packets.back().path=="bgm/BGM_zombie_attack.mp3");
        const auto duskPackets=packets.size();
        for(int i=0;i<100;++i)Check(phases.SetDusk(10));
        Check(phases.Select(1)&&packets.size()==duskPackets);
        Check(phases.SetNight(10,true,music::NightChoice::Zsh)&&packets.size()==duskPackets+1);
        Check(packets.back().action==music::Action::Loop&&packets.back().path=="bgm/Zsh_nightbgm.mp3"&&packets.back().nextPath.empty());
        const auto zshPackets=packets.size();
        Check(phases.SetNight(10,true,music::NightChoice::Zsh)&&packets.size()==zshPackets);
        Check(!phases.SetNight(10,true,music::NightChoice::Rex)&&packets.size()==zshPackets);
        Check(!phases.SetDusk(10)&&packets.size()==zshPackets);
        Check(phases.Replay(player)&&packets.back().path=="bgm/Zsh_nightbgm.mp3");
        Check(phases.SetNight(11,false)&&packets.back().path=="bgm/Scenario_Ready.mp3");
        Check(phases.SetDusk(11)&&packets.back().path=="bgm/BGM_zombie_attack.mp3");
        Check(phases.SetNight(11,false)&&packets.back().path=="bgm/Scenario_Ready.mp3");
        std::cout<<"PASS BGM actual night "<<checks<<" checks; transport/authority remain fixtures\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
