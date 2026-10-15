#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ostream>

namespace goldcraft::timing {
inline double seconds() noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Game-thread only. Recording neither allocates nor writes files. Keep slow
// calls after a later quiet reporting window, with bounded memory usage.
template<std::size_t Stages> struct History {
    static constexpr double threshold_ms=50;
    static constexpr std::size_t capacity=64;
    struct Counter {std::uint64_t count=0,slow=0;double max_ms=0;};
    struct Call {std::size_t stage=0;double elapsed_ms=0,finished=0;int key=-1;};
    std::array<Counter,Stages> counters{};
    std::array<Call,capacity> calls{};
    std::uint64_t slow_calls=0;

    void record(std::size_t stage,double elapsed_ms,double finished,int key=-1) noexcept {
        auto& counter=counters[stage];
        ++counter.count;counter.max_ms=(std::max)(counter.max_ms,elapsed_ms);
        if(elapsed_ms<threshold_ms)return;
        ++counter.slow;
        calls[slow_calls++%capacity]={stage,elapsed_ms,finished,key};
    }
    void write(std::ostream& out,const std::array<const char*,Stages>& names) const {
        out<<"{\"schema\":1,\"thresholdMs\":"<<threshold_ms
            <<",\"capacity\":"<<capacity<<",\"slowCalls\":"<<slow_calls
            <<",\"overwritten\":"<<(slow_calls>capacity?slow_calls-capacity:0)<<",\"stages\":{";
        for(std::size_t i=0;i<Stages;++i){
            if(i)out<<',';
            const auto& counter=counters[i];
            out<<'"'<<names[i]<<"\":{\"count\":"<<counter.count
                <<",\"maxMs\":"<<counter.max_ms<<",\"over50Ms\":"<<counter.slow<<'}';
        }
        out<<"},\"longCalls\":[";
        const auto count=std::min<std::uint64_t>(slow_calls,capacity);
        for(std::uint64_t i=0;i<count;++i){
            if(i)out<<',';
            const auto& call=calls[(slow_calls-count+i)%capacity];
            out<<"{\"stage\":\""<<names[call.stage]<<"\",\"elapsedMs\":"<<call.elapsed_ms
                <<",\"completedSteadySeconds\":"<<call.finished<<",\"key\":"<<call.key<<'}';
        }
        out<<"]}";
    }
};
template<std::size_t Stages> class Scope {
public:
    Scope(History<Stages>& history,std::size_t stage,int key=-1) noexcept
        :history_(history),stage_(stage),key_(key),started_(seconds()){}
    ~Scope(){const double now=seconds();history_.record(stage_,(std::max)(0.0,(now-started_)*1000),now,key_);}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    History<Stages>& history_;std::size_t stage_;int key_;double started_;
};
}
