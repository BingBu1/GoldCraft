#include "steam_async_state.hpp"
#include <algorithm>
#include <thread>

namespace goldcraft::steam_async {
using namespace std::chrono_literals;
int State::request_avatar(SteamId id, unsigned size, bool relationship_only) {
    if(!id || size>2 || stopping.load())return 0;
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock){
        ++counters.lock_misses;
        for(auto& value:client_avatars_)if(value&&value->id==id&&value->size==size)
            return relationship_only?value->relationship:value->handle;
        return 0;
    }
    Entry* item=nullptr;
    for(auto& entry:avatars_)if(entry.id==id && entry.size==size){item=&entry;break;}
    if(!item){
        for(auto& entry:avatars_)if(!entry.id){item=&entry;break;}
        if(!item){
            for(auto& entry:avatars_)if(!entry.pending && (!item || entry.requested<item->requested))item=&entry;
        }
        if(!item)return 0;
        *item={};item->id=id;item->size=size;item->revision=++revision_;
    }
    const auto now=Clock::now();
    if(!item->pending && (!item->value || now-item->value->updated>(item->value->handle?60s:5s)) &&
       (item->requested==Clock::time_point{} || now-item->requested>1s)){
        item->pending=true;item->requested=now;++counters.avatar_requests;
    }
    auto& value=client_avatars_[item-avatars_.data()];value=item->value;
    if(!value)return 0;
    return relationship_only?value->relationship:value->handle;
}
int State::avatar(SteamId id,unsigned size){return request_avatar(id,size,false);}
int State::relationship(SteamId id){return request_avatar(id,1,true);}
bool State::image_size(int handle,unsigned* width,unsigned* height){
    if(width)*width=0;if(height)*height=0;
    if(!width||!height)return false;
    for(auto& value:client_avatars_)if(value && value->handle==handle && handle>=image_base){
        *width=value->width;*height=value->height;return true;
    }
    return false;
}
bool State::image_rgba(int handle,void* data,int bytes){
    if(!data||bytes<0)return false;
    std::shared_ptr<const Avatar> image;
    for(auto& value:client_avatars_)if(value&&value->handle==handle&&handle>=image_base){image=value;break;}
    if(!image)return false;
    const auto size=image->width*image->height*4;
    if(size>static_cast<unsigned>(bytes))return false;
    std::memcpy(data,image->rgba.data(),size);return true;
}
void State::start_voice(){
    const auto old=desired_.load();
    // The low bit denotes recording; every new press gets a new generation.
    if(!(old&1)){pcm_rate_.store(0);desired_.store(((old+2)&~1u)|1u);}
}
void State::stop_voice(){desired_.fetch_and(~1u);}
void State::discard_old(Clock::time_point now,unsigned generation){
    while(count_){
        const auto& p=packets_[read_];
        if(p->generation==generation && now-p->captured<500ms)break;
        read_=(read_+1)%voice_capacity;--count_;++counters.dropped;
    }
}
VoiceResult State::available(unsigned* compressed,unsigned* pcm,unsigned rate){
    if(compressed)*compressed=0;if(pcm)*pcm=0;
    if(rate)pcm_rate_.store(rate);
    const auto generation=desired_.load();
    if(stopping.load() || !(generation&~1u))return VoiceResult::not_recording;
    if(voice_failed_.load())return VoiceResult::not_initialized;
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock){++counters.lock_misses;return VoiceResult::no_data;}
    discard_old(Clock::now(),generation&~1u);
    if(!count_)return !(generation&1)&&drained_generation_.load()==generation?
        VoiceResult::not_recording:VoiceResult::no_data;
    if(compressed)*compressed=packets_[read_]->compressed;
    if(pcm)*pcm=packets_[read_]->pcm;
    return VoiceResult::ok;
}
VoiceResult State::voice(bool compressed,void* data,unsigned capacity,unsigned* written,
    bool pcm,void* raw,unsigned raw_capacity,unsigned* raw_written,unsigned rate){
    if(written)*written=0;if(raw_written)*raw_written=0;
    if(pcm)pcm_rate_.store(rate);
    if((compressed&&(!data||!written)) || (pcm&&(!raw||!raw_written)))return VoiceResult::data_corrupted;
    const auto generation=desired_.load();
    if(stopping.load() || !(generation&~1u))return VoiceResult::not_recording;
    if(voice_failed_.load())return VoiceResult::not_initialized;
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock){++counters.lock_misses;return VoiceResult::no_data;}
    discard_old(Clock::now(),generation&~1u);
    if(!count_)return !(generation&1)&&drained_generation_.load()==generation?
        VoiceResult::not_recording:VoiceResult::no_data;
    const auto& packet=*packets_[read_];
    if((compressed&&capacity<packet.compressed)||(pcm&&raw_capacity<packet.pcm)){
        if(written)*written=packet.compressed;if(raw_written)*raw_written=packet.pcm;
        return VoiceResult::buffer_too_small;
    }
    if(compressed){std::memcpy(data,packet.data.data(),packet.compressed);*written=packet.compressed;}
    if(pcm && rate==packet.rate){std::memcpy(raw,packet.raw.data(),packet.pcm);*raw_written=packet.pcm;}
    read_=(read_+1)%voice_capacity;--count_;return VoiceResult::ok;
}
void State::run_avatars(Backend& backend){
    if(!backend.initialize())return;
    counters.avatar_ready.store(1);
    while(!stopping.load()){
        backend.poll();
        if(!backend.healthy())break;
        SteamId id=0;unsigned size=0,revision=0;
        {std::lock_guard lock(mutex_);for(auto& e:avatars_)if(e.pending){id=e.id;size=e.size;revision=e.revision;break;}}
        if(!id){std::this_thread::sleep_for(10ms);continue;}
        auto result=std::make_shared<Avatar>();result->id=id;result->size=size;
        backend.avatar(*result); // No lock, engine memory or UI calls across IPC.
        if(stopping.load())break;
        result->updated=Clock::now();
        std::lock_guard lock(mutex_);
        for(auto& e:avatars_)if(e.id==id&&e.size==size&&e.revision==revision){
            e.pending=false;
            if(result->width && result->height && result->width<=184 && result->height<=184){
                // New data gets a new image handle so VGUI refreshes its texture.
                if(next_image_<0x0fffffff)result->handle=image_base+static_cast<int>(++next_image_);
                e.value=std::move(result);
            }else if(result->handle==0){e.value=std::move(result);}
            ++counters.avatar_completed;break;
        }
    }
    counters.avatar_ready.store(0);
}
void State::run_voice(Backend& backend){
    if(!backend.initialize()){voice_failed_.store(true);return;}
    counters.voice_ready.store(1);
    unsigned active=0;bool recording=false;
    auto packet=std::make_unique<VoicePacket>();
    auto capture=[&] {
        const auto rate=pcm_rate_.load();
        packet->compressed=packet->pcm=0;packet->rate=rate;packet->generation=active&~1u;
        packet->captured=Clock::now();
        const auto result=backend.capture(*packet,rate!=0,rate);
        counters.last_voice_result.store(static_cast<unsigned>(result));
        if(result==VoiceResult::not_initialized)voice_failed_.store(true);
        if(result==VoiceResult::ok && packet->compressed<=voice_bytes && packet->pcm<=voice_bytes &&
           (packet->compressed||packet->pcm) && (active&~1u)==(desired_.load()&~1u) && !stopping.load()){
            std::lock_guard lock(mutex_);
            if(count_==voice_capacity){read_=(read_+1)%voice_capacity;--count_;++counters.dropped;}
            auto& slot=packets_[(read_+count_)%voice_capacity];
            std::swap(slot,packet);++count_;++counters.voice_packets;
        }
        if(!packet)packet=std::make_unique<VoicePacket>();
        return result;
    };
    while(!stopping.load()&&!voice_failed_.load()){
        backend.poll();
        if(!backend.healthy()){voice_failed_.store(true);break;}
        auto wanted=desired_.load();
        if(wanted!=active){
            if(recording){
                backend.stop_voice();recording=false;
                // Deliver the final syllable after key release. Only a new
                // press invalidates it; StopVoiceRecording alone does not.
                while(!stopping.load()&&!voice_failed_.load()
                    &&(active&~1u)==(desired_.load()&~1u)){
                    const auto tail=capture();
                    if(tail!=VoiceResult::ok&&tail!=VoiceResult::no_data)break;
                    // NoData is not NotRecording: the final syllable may
                    // arrive later. Never carry it into the next key press.
                    if(tail==VoiceResult::no_data)std::this_thread::sleep_for(5ms);
                }
                drained_generation_.store(active&~1u);
            }
            // The player may have released or pressed K again during IPC.
            wanted=desired_.load();
            active=wanted;
            if((active&1) && !stopping.load()){
                backend.start_voice();recording=true;
            }else drained_generation_.store(active);
        }
        if(!recording){std::this_thread::sleep_for(5ms);continue;}
        capture();
        std::this_thread::sleep_for(5ms);
    }
    if(recording)backend.stop_voice();
    counters.voice_ready.store(0);
}
}
