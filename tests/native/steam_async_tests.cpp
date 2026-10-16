#include "steam_async_state.hpp"
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <vector>
using namespace goldcraft::steam_async;
using namespace std::chrono_literals;

template<class F> void until(F f){
    const auto deadline=Clock::now()+2s;
    while(!f()){assert(Clock::now()<deadline);std::this_thread::sleep_for(1ms);}
}
struct Fake final:Backend {
    std::atomic<unsigned> entered{},starts{},stops{},calls{};
    std::atomic<bool> release{},recording{},send{},bad{},endless_tail{};
    std::atomic<unsigned> tail_polls{};
    bool initialize()override{return true;}
    void avatar(Avatar& a)override{
        ++entered;until([&]{return release.load();});
        a.width=2;a.height=2;a.relationship=3;a.handle=12;
        for(unsigned i=0;i<16;++i)a.rgba[i]=static_cast<unsigned char>(i);
    }
    void start_voice()override{++starts;recording=true;}
    void stop_voice()override{++stops;recording=false;}
    VoiceResult capture(VoicePacket& p,bool pcm,unsigned rate)override{
        if(!recording){
            const auto poll=++tail_polls;
            if(endless_tail)return VoiceResult::no_data;
            if(poll<=2)return VoiceResult::no_data;
            if(poll==3){p.compressed=3;p.data[0]=99;return VoiceResult::ok;}
            return VoiceResult::not_recording;
        }
        ++calls;
        if(!send.exchange(false))return VoiceResult::no_data;
        ++entered;until([&]{return release.load();});
        if(bad)return VoiceResult::data_corrupted;
        p.compressed=3;p.data[0]=1;p.data[1]=2;p.data[2]=3;
        p.pcm=pcm?2:0;p.rate=rate;p.raw[0]=4;p.raw[1]=5;
        return VoiceResult::ok;
    }
};
int main(){
    {
        State s;Fake b;std::thread worker([&]{s.run_avatars(b);});
        assert(s.avatar(123,1)==0);until([&]{return b.entered==1;});
        const auto begin=Clock::now();
        for(unsigned i=0;i<1000;++i){assert(s.avatar(123,1)==0);assert(s.relationship(123)==0);}
        assert(Clock::now()-begin<100ms);assert(b.entered==1);
        b.release=true;until([&]{return s.counters.avatar_completed==1;});
        const auto handle=s.avatar(123,1);assert(handle>image_base);assert(s.relationship(123)==3);
        unsigned w,h;assert(s.image_size(handle,&w,&h)&&w==2&&h==2);
        unsigned char data[20]{};data[16]=0x7f;
        assert(!s.image_rgba(handle,data,15));assert(s.image_rgba(handle,data,16));
        for(unsigned i=0;i<16;++i)assert(data[i]==i);assert(data[16]==0x7f);
        assert(!s.image_size(image_base+500,&w,&h));assert(!s.image_size(handle,nullptr,&h));
        s.stopping=true;worker.join();
    }
    {
        State s;Fake b;b.send=true;
        std::thread worker([&]{s.run_voice(b);});s.start_voice();
        until([&]{return b.entered==1;});
        unsigned n=999,r=999;unsigned char data[8]{},raw[8]{};
        const auto begin=Clock::now();
        for(unsigned i=0;i<1000;++i){
            assert(s.available(&n,&r,11025)==VoiceResult::no_data&&n==0&&r==0);
            assert(s.voice(true,data,8,&n,false,nullptr,0,nullptr,0)==VoiceResult::no_data);
        }
        assert(Clock::now()-begin<100ms);
        // A new press cannot inherit old data, even if a request finishes late.
        // Releasing K alone keeps its final syllable available until drained.
        s.stop_voice();assert(s.available(&n,&r,0)==VoiceResult::no_data);
        s.start_voice();b.release=true;until([&]{return b.starts==2;});
        assert(s.counters.voice_packets==0);assert(b.stops==1);assert(b.tail_polls==0);
        s.available(&n,&r,11025);b.send=true;
        until([&]{return s.counters.voice_packets==1;});
        assert(s.available(&n,&r,11025)==VoiceResult::ok&&n==3&&r==2);
        assert(s.voice(true,data,2,&n,true,raw,8,&r,11025)==VoiceResult::buffer_too_small&&n==3&&r==2);
        assert(s.voice(true,data,8,&n,true,raw,8,&r,11025)==VoiceResult::ok);
        assert(n==3&&r==2&&data[0]==1&&data[2]==3&&raw[0]==4&&raw[1]==5);
        assert(s.available(&n,&r,0)==VoiceResult::no_data);
        b.send=true;until([&]{return s.counters.voice_packets==2;});
        std::this_thread::sleep_for(510ms);
        assert(s.available(&n,&r,0)==VoiceResult::no_data);assert(s.counters.dropped==1);
        // Queue pressure is bounded; incomplete calls never hold the UI lock.
        for(unsigned i=0;i<voice_capacity+3;++i){const auto count=s.counters.voice_packets.load();b.send=true;until([&]{return s.counters.voice_packets>count;});}
        assert(s.counters.dropped>=4);
        s.stop_voice();
        until([&]{
            const auto result=s.voice(true,data,8,&n,false,nullptr,0,nullptr,0);
            return result==VoiceResult::not_recording;
        });
        s.stopping=true;worker.join();assert(b.stops==2);
    }
    {
        State s;Fake b;b.release=true;
        std::thread worker([&]{s.run_voice(b);});s.start_voice();
        until([&]{return b.starts==1;});s.stop_voice();
        unsigned n{},r{};unsigned char data[8]{};
        until([&]{return s.available(&n,&r,0)==VoiceResult::ok;});
        assert(s.voice(true,data,8,&n,false,nullptr,0,nullptr,0)==VoiceResult::ok&&n==3&&data[0]==99);
        until([&]{return s.available(&n,&r,0)==VoiceResult::not_recording;});
        s.stopping=true;worker.join();
    }
    {
        State s;Fake b;b.release=true;b.endless_tail=true;
        std::thread worker([&]{s.run_voice(b);});s.start_voice();
        until([&]{return b.starts==1;});s.stop_voice();
        until([&]{return b.tail_polls>=3;});
        // An unending NoData tail must not starve the next recording. The
        // old generation is already invalid, so waiting for it has no value.
        s.start_voice();until([&]{return b.starts==2;});
        const auto polls=b.tail_polls.load();b.send=true;
        until([&]{return s.counters.voice_packets==1;});
        unsigned n{};unsigned char data[8]{};
        assert(s.voice(true,data,8,&n,false,nullptr,0,nullptr,0)==VoiceResult::ok&&data[0]==1);
        assert(b.tail_polls==polls);
        s.stopping=true;worker.join();
    }
    std::cout<<"Steam async: blocked backend, duplicate requests, image bounds, recording/repress, stale generation, buffers and overflow passed\n";
}
