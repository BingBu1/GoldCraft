#pragma once
#include "goldcraft/wire.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace goldcraft::fluids {
constexpr int empty=-1,water=-3,lava=-5;
constexpr std::size_t limit=8192,record_bytes=28,max_bytes=20+limit*record_bytes;
constexpr const char* message_name="GCFluid";
constexpr const char* client_capability="_gc_fluid1";
struct Volume {
    std::int32_t contents=empty;
    std::array<float,3> min{},max{};
    bool operator==(const Volume&) const = default;
    bool contains(const float* p) const noexcept {
        for(int a=0;a<3;++a)if(!(p[a]>=min[a]&&p[a]<max[a]))return false;
        return true;
    }
};
struct Snapshot {
    std::uint64_t epoch=0,revision=0;
    std::vector<Volume> volumes;
};
inline Snapshot decode(std::span<const std::uint8_t> bytes) {
    Reader r(bytes);Snapshot s;s.epoch=r.u64();s.revision=r.u64();auto count=r.u32();
    if(!s.epoch||!s.revision||count>limit||r.remaining()!=count*record_bytes)
        throw ProtocolError("Fluid snapshot identity/length");
    s.volumes.reserve(count);
    for(unsigned i=0;i<count;++i){
        Volume v;v.contents=r.i32();for(auto& f:v.min)f=r.f32();for(auto& f:v.max)f=r.f32();
        if(v.contents!=water&&v.contents!=lava)throw ProtocolError("Fluid contents kind");
        for(int a=0;a<3;++a)if(v.min[a]<-16384||v.max[a]>16384||v.min[a]>=v.max[a])
            throw ProtocolError("Fluid volume bounds");
        s.volumes.push_back(v);
    }
    r.finish();return s;
}
inline Bytes encode(const Snapshot& s){
    Writer w;w.u64(s.epoch);w.u64(s.revision);w.u32(static_cast<std::uint32_t>(s.volumes.size()));
    for(const auto& v:s.volumes){w.i32(v.contents);for(auto f:v.min)w.f32(f);for(auto f:v.max)w.f32(f);}
    return std::move(w.data);
}
// Immutable query geometry. Build once on a changed authority snapshot; PM
// queries do not allocate, rebuild, upload or inspect graphics state.
class Field {
    struct Node {std::array<float,3> min{},max{};std::size_t begin=0,end=0,left=0,right=0;};
    Snapshot state_;
    std::vector<Node> nodes_;
    std::vector<std::size_t> order_;
    std::size_t build(std::size_t begin,std::size_t end){
        const auto index=nodes_.size();nodes_.emplace_back();Node n;n.begin=begin;n.end=end;
        n.min.fill(std::numeric_limits<float>::infinity());n.max.fill(-std::numeric_limits<float>::infinity());
        for(auto i=begin;i<end;++i)for(int a=0;a<3;++a){const auto& v=state_.volumes[order_[i]];n.min[a]=std::min(n.min[a],v.min[a]);n.max[a]=std::max(n.max[a],v.max[a]);}
        if(end-begin>8){
            int axis=0;for(int a=1;a<3;++a)if(n.max[a]-n.min[a]>n.max[axis]-n.min[axis])axis=a;
            const auto middle=(begin+end)/2;
            std::nth_element(order_.begin()+begin,order_.begin()+middle,order_.begin()+end,[&](auto x,auto y){const auto& a=state_.volumes[x];const auto& b=state_.volumes[y];return a.min[axis]+a.max[axis]<b.min[axis]+b.max[axis];});
            n.left=build(begin,middle);n.right=build(middle,end);
        }
        nodes_[index]=n;return index;
    }
    int query(std::size_t index,const float* p) const noexcept {
        const auto& n=nodes_[index];for(int a=0;a<3;++a)if(!(p[a]>=n.min[a]&&p[a]<n.max[a]))return empty;
        if(n.left){auto a=query(n.left,p);if(a==lava)return a;auto b=query(n.right,p);return b==lava?b:a!=empty?a:b;}
        int result=empty;for(auto i=n.begin;i<n.end;++i){const auto& v=state_.volumes[order_[i]];if(v.contains(p)){if(v.contents==lava)return lava;result=water;}}
        return result;
    }
public:
    const Snapshot& state() const noexcept{return state_;}
    void clear() noexcept{state_={};nodes_.clear();order_.clear();}
    bool assign(Snapshot next){
        if(next.epoch==state_.epoch&&next.revision<state_.revision)return false;
        if(next.epoch==state_.epoch&&next.revision==state_.revision&&next.volumes!=state_.volumes)
            throw ProtocolError("Fluid revision reused with changed contents");
        if(next.volumes==state_.volumes){state_.epoch=next.epoch;state_.revision=next.revision;return true;}
        Field candidate;candidate.state_=std::move(next);candidate.order_.resize(candidate.state_.volumes.size());
        std::iota(candidate.order_.begin(),candidate.order_.end(),0);candidate.nodes_.reserve(candidate.order_.size()*2);
        if(!candidate.order_.empty())candidate.build(0,candidate.order_.size());
        *this=std::move(candidate);return true;
    }
    int contents(const float* p) const noexcept {return nodes_.empty()?empty:query(0,p);}
};
// Reliable GoldSrc user-message fragments. Never expose a partially received
// fluid snapshot to prediction. Zero total is a lease for the applied revision.
class Receiver {
    std::uint64_t epoch_=0,revision_=0;
    std::uint32_t total_=0;
    Bytes pending_;
public:
    Field field;
    double refreshed=0;
    void clear(){epoch_=revision_=0;total_=0;pending_.clear();field.clear();refreshed=0;}
    bool accept(std::uint64_t world,std::span<const std::uint8_t> bytes,double now){
        Reader r(bytes);auto epoch=r.u64(),revision=r.u64();auto offset=r.u32(),total=r.u32();
        if(epoch!=world||!world)return false;
        if(!revision)throw ProtocolError("Fluid fragment revision");
        if(!total){r.finish();if(offset||field.state().epoch!=world||field.state().revision!=revision)return false;refreshed=now;return true;}
        if(total<20||total>max_bytes||offset>=total||!r.remaining()||r.remaining()>168||r.remaining()>total-offset)
            throw ProtocolError("Fluid fragment bounds");
        if(revision<field.state().revision)return false;
        if(!offset){epoch_=epoch;revision_=revision;total_=total;pending_.clear();pending_.reserve(total);}
        if(epoch_!=epoch||revision_!=revision||total_!=total||pending_.size()!=offset)
            throw ProtocolError("Fluid fragment order");
        auto body=r.bytes(r.remaining());pending_.insert(pending_.end(),body.begin(),body.end());
        if(pending_.size()!=total)return false;
        auto next=decode(pending_);if(next.epoch!=epoch||next.revision!=revision)throw ProtocolError("Fluid fragment identity");
        field.assign(std::move(next));refreshed=now;pending_.clear();return true;
    }
    const Field* active(std::uint64_t world,double now) const noexcept {
        return world&&field.state().epoch==world&&now>=refreshed&&now-refreshed<1.0?&field:nullptr;
    }
};
// Nest this *inside* the carved-map callback adapter, so native map contents
// (including mined holes and native water) are resolved before MC liquid.
template<class PM> class MovementScope {
    PM& pm_;
    decltype(PM::PM_PointContents) point_;
    decltype(PM::PM_TruePointContents) true_;
    decltype(PM::PM_PointContents) observed_point_;
    decltype(PM::PM_TruePointContents) observed_true_;
    const Field* field_;
    MovementScope* previous_;
    inline static thread_local MovementScope* current_=nullptr;
    static int point(float* p,int* real){
        auto& s=*current_;const int native=s.point_(p,real);
        if(native!=empty)return native;
        const int value=s.field_->contents(p);if(value!=empty&&real)*real=value;return value;
    }
    static int truth(float* p){auto& s=*current_;const int value=s.true_(p);return value==empty?s.field_->contents(p):value;}
public:
    MovementScope(PM& pm,const Field* field):pm_(pm),point_(pm.PM_PointContents),true_(pm.PM_TruePointContents),observed_point_(point_),observed_true_(true_),field_(field),previous_(current_){
        if(!point_||!true_)field_=nullptr;
        // Same-thread reentrancy must not recursively call our own wrapper.
        for(auto* outer=previous_;outer;outer=outer->previous_)if(&outer->pm_==&pm_&&outer->field_){
            if(point_==point)point_=outer->point_;if(true_==truth)true_=outer->true_;
        }
        current_=this;pm_.PM_PointContents=field_?point:point_;pm_.PM_TruePointContents=field_?truth:true_;
    }
    ~MovementScope(){pm_.PM_PointContents=observed_point_;pm_.PM_TruePointContents=observed_true_;current_=previous_;}
    MovementScope(const MovementScope&)=delete;
};
}
