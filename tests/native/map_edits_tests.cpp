#include "goldcraft/map_edits.hpp"
#include "goldcraft/map_edit_delivery.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace goldcraft;
using namespace goldcraft::edits;
template<class F> void rejects(F action){bool failed=false;try{action();}catch(const ProtocolError&){failed=true;}assert(failed);}
Box volume(float x=0){return {{x,-16,-32},{x+32,16,32}};}
int main(){
    {
        Ledger indexed;indexed.reset(11);
        const Target first{17,9,3},second{17,10,3},third{18,4,3};
        const std::array<Cut,6> input{{{0,third,volume(96)},{0,first,volume()},
            {0,{},volume()},{0,second,volume(32)},{0,first,volume(64)},{0,first,volume()}}};
        assert(indexed.add_batch(input));assert(indexed.state().cuts.size()==5);
        assert((std::vector<Target>(indexed.targets().begin(),indexed.targets().end())==std::vector<Target>{first,second,third}));
        assert(indexed.state().cuts[0].target==third&&indexed.state().cuts[1].target==first);
        auto candidate=indexed;assert(candidate.targets().data()==indexed.targets().data());
        auto zero=volume();zero.min[0]=-0.0f;assert(!candidate.add(first,zero));
        assert(candidate.remove(first));assert(candidate.targets().size()==2&&indexed.targets().size()==3);
        assert(!candidate.remove(first));assert(candidate.add(first,zero));
        assert(candidate.targets().front()==first);
        indexed.swap(candidate);assert(indexed.state().cuts.back().box==zero);
        assert(candidate.state().cuts.size()==5&&candidate.targets().size()==3);
        assert(indexed.restore());assert(indexed.targets().empty());
        assert(indexed.add_batch(input));assert(indexed.targets().size()==3);
        indexed.reset(12);assert(indexed.targets().empty());
        assert(indexed.add_batch(input));indexed=candidate;assert(indexed.state()==candidate.state());
        assert(indexed.targets().data()==candidate.targets().data());
    }
    {
        Ledger batches; batches.reset(7); Replica replica; replica.reset(7); replica.accept(batches.state());
        std::array<Cut,3> input{{{0,{},volume(0)},{0,{},volume(32)},{0,{},volume(32)}}};
        assert(batches.add_batch(input));
        auto pending=batches.next(1,true); assert(pending->type==Type::map_edit_batch);
        assert(pending->payload.size()==28+2*44);
        assert(batches.next(1)->type==Type::map_edit_snapshot);
        assert(batches.next(2,true)->type==Type::map_edit_snapshot); // No partial explosion checkpoint.
        assert(replica.accept(batch(pending->payload))==Applied::changed&&replica.state()==batches.state());
        assert(replica.accept(batch(pending->payload))==Applied::ignored);
        assert(batches.add({},volume(64)));assert(batches.restore());
        auto tail=batches.since(3,true);assert(tail.size()==2);
        for(const auto& m:tail)assert(replica.accept(delta(m.payload))==Applied::changed);
        assert(replica.state()==batches.state());
        Assembler fragments;fragments.reset(7);std::optional<Message> result;
        for(std::size_t offset=0;offset<pending->payload.size();offset+=fragment_bytes)
            result=fragments.accept(fragment(7,1,*pending,offset));
        assert(result&&result->payload==pending->payload);
        auto bad=batch(pending->payload);bad.cuts.back().id++;rejects([&]{encode(bad);});
        bad=batch(pending->payload);bad.base=UINT64_MAX-1;bad.revision=1;rejects([&]{encode(bad);});
        replica.reset(7);assert(replica.accept(batch(pending->payload))==Applied::need_snapshot);
        batches.reset(8);assert(batch(pending->payload).epoch==7);
        // Encoded history is byte-bounded, including large batches interleaved
        // with clears. Evicted messages remain valid for slow recipients.
        std::vector<Cut> large;large.reserve(50000);
        for(unsigned i=0;i<50000;++i){const float x=-30000.0f+float(i);large.push_back({0,{},volume(x)});}
        assert(batches.add_batch(large));auto retained=batches.next(1,true);
        assert(batches.restore());const auto base=batches.state().revision;
        assert(batches.add_batch(large));assert(batches.next(1,true)->type==Type::map_edit_snapshot);
        assert(batches.next(base,true)->type==Type::map_edit_batch);
        assert(batch(retained->payload).cuts.size()==50000);
    }
    {
        int cursor=0;std::array<int,65> delivered{};
        assert(pump_fragments(32,cursor,[&](int slot){if(slot!=26)return false;++delivered[slot];return true;})==8);
        assert(delivered[26]==8); // Single paired client uses otherwise idle turns.
        delivered={};cursor=0;
        for(int frame=0;frame<8;++frame)
            assert(pump_fragments(32,cursor,[&](int slot){++delivered[slot];return true;})==16);
        for(int slot=1;slot<=32;++slot)assert(delivered[slot]==4);
        int attempts=0;assert(pump_fragments(32,cursor,[&](int){++attempts;return false;})==0&&attempts==32);
        assert(pump_fragments(0,cursor,[](int){assert(false);return false;})==0);
        assert(reliable_room(3990,0,true,false)==1536);
        assert(reliable_room(3990,1500,true,false)==36);
        assert(!reliable_room(3990,1536,true,false));
        assert(!reliable_room(3990,0,true,true));
        assert(!reliable_room(3990,0,false,false));
        assert(!reliable_room(3990,-1,true,false));
        assert(!reliable_room(3990,4000,true,false));
        std::array<int,65> queued{};cursor=0;
        auto send=[&](int slot){if(slot!=1&&slot!=26)return false;
            if(reliable_room(3990,queued[slot],true,false)<188)return false;
            queued[slot]+=188;return true;};
        assert(pump_fragments(32,cursor,send)==16);
        for(int retry=0;retry<100;++retry)assert(pump_fragments(32,cursor,send)==0);
        queued[26]=0;assert(pump_fragments(32,cursor,send)==8&&queued[1]==1504);
    }
    Ledger ledger;Replica peer;ledger.reset(7);peer.reset(7);
    assert(!peer.ready());auto messages=ledger.since(0);assert(messages.size()==1);
    assert(peer.accept(snapshot(messages.front().payload))==Applied::changed&&peer.ready());
    assert(ledger.add({},volume()));assert(!ledger.add({},volume()));
    assert(ledger.add({12,3,2},volume(32)));
    auto deltas=ledger.since(1);assert(deltas.size()==2);
    assert(peer.accept(delta(deltas.back().payload))==Applied::need_snapshot&&!peer.ready());
    assert(peer.accept(delta(deltas.front().payload))==Applied::need_snapshot);
    assert(peer.accept(ledger.state())==Applied::changed&&peer.state()==ledger.state());
    assert(peer.accept(delta(deltas.front().payload))==Applied::ignored);
    assert(!ledger.remove({12,4,2}));assert(!ledger.remove({12,3,3}));assert(ledger.remove({12,3,2}));
    assert(peer.accept(delta(ledger.since(3).front().payload))==Applied::changed&&peer.state().cuts.size()==1);
    auto before=ledger.state();auto bytes=encode(before);assert(snapshot(bytes)==before);
    for(std::size_t i=0;i<bytes.size();++i)rejects([&]{snapshot(std::span(bytes).first(i));});
    auto extra=bytes;extra.push_back(0);rejects([&]{snapshot(extra);});
    Snapshot duplicate{7,3,{{2,{},volume()},{2,{},volume()}}};rejects([&]{encode(duplicate);});
    rejects([&]{ledger.add({0,1,0},volume());});assert(ledger.state()==before);
    auto invalid=volume();invalid.max[0]=std::numeric_limits<float>::infinity();rejects([&]{ledger.add({},invalid);});
    assert(ledger.restore());assert(!ledger.restore());
    assert(peer.accept(delta(ledger.since(4).front().payload))==Applied::changed&&peer.state().cuts.empty());
    assert(peer.accept(before)==Applied::ignored); // An old full state cannot undo restoration.
    peer.invalidate();assert(peer.accept(before)==Applied::ignored&&!peer.ready());
    assert(peer.accept(ledger.state())==Applied::changed&&peer.ready());
    ledger.reset(8);assert(ledger.state().revision==1&&ledger.state().cuts.empty());
    assert(peer.accept(ledger.state())==Applied::ignored);peer.reset(8);assert(!peer.ready());
    assert(peer.accept(ledger.state())==Applied::changed);
    for(unsigned i=0;i<300;++i)assert(ledger.add({},volume(static_cast<float>(i))));
    assert(ledger.since(1).front().type==Type::map_edit_snapshot);
    assert(ledger.since(300).size()==1&&ledger.since(300).front().type==Type::map_edit_delta);
    assert(ledger.since(999).front().type==Type::map_edit_snapshot);
    assert(ledger.since(301).empty());
    {
        // Candidate commits must never mutate a slow peer's in-flight bytes.
        Ledger sending; sending.reset(9);
        std::vector<Cut> cuts;
        for(unsigned i=0;i<1331;++i)cuts.push_back({0,{},volume(float(i))});
        assert(sending.add_batch(cuts));
        auto pending=sending.next(0);const auto original=pending->payload;
        for(unsigned peer_id=0;peer_id<32;++peer_id)assert(sending.next(0)==pending);
        assert(!sending.next(sending.state().revision));
        assert(!sending.add_batch(cuts)&&sending.next(0)==pending);
        auto malformed=cuts;malformed.back().target={0,1,0};
        rejects([&]{sending.add_batch(malformed);});assert(sending.next(0)==pending);
        Ledger candidate=sending;
        assert(candidate.add({},volume(2000)));
        const auto queued=candidate.next(sending.state().revision);
        assert(queued->type==Type::map_edit_delta);
        assert(candidate.next(sending.state().revision)==queued);
        assert(sending.next(0)==pending&&candidate.next(0)!=pending);
        sending.swap(candidate);
        assert(candidate.next(0)==pending);
        Assembler slow;slow.reset(9);std::optional<Message> result;
        for(std::size_t offset=0;offset<pending->payload.size();offset+=fragment_bytes){
            if(offset==fragment_bytes)assert(sending.restore());
            result=slow.accept(fragment(9,1,*pending,offset));
        }
        assert(result&&result->payload==original);
        Replica caught_up;caught_up.reset(9);caught_up.accept(snapshot(result->payload));
        unsigned changes=0;
        while(auto message=sending.next(caught_up.state().revision)){
            assert(message->type==Type::map_edit_delta);
            assert(caught_up.accept(delta(message->payload))==Applied::changed);++changes;
        }
        assert(changes==2&&caught_up.state()==sending.state()&&caught_up.state().cuts.empty());
        // Rollover falls back to the current snapshot without invalidating
        // references to evicted messages or crossing map epochs.
        for(unsigned i=0;i<300;++i)assert(sending.add({},volume(float(i))));
        assert(sending.next(1332)->type==Type::map_edit_snapshot);
        assert(delta(queued->payload).revision==1333);
        auto retained=sending.next(0);
        sending.reset(10);assert(snapshot(sending.next(0)->payload)==sending.state());
        assert(snapshot(retained->payload).epoch==9&&pending->payload==original);
        sending.reset(0);assert(!sending.next(0)&&!sending.next(1));
    }
    Message full{Type::map_edit_snapshot,encode(ledger.state())};
    Assembler assembly;assembly.reset(8);std::optional<Message> complete;
    for(std::size_t offset=0;offset<full.payload.size();offset+=fragment_bytes){
        const auto chunk=fragment(8,1,full,offset);assert(chunk.size()<=192);
        complete=assembly.accept(chunk);if(offset+fragment_bytes<full.payload.size())assert(!complete);
    }
    assert(complete&&complete->payload==full.payload);assert(!assembly.accept(fragment(8,1,full,0)));
    // Actual wire fragments/assemblers, two clients: stalled reliable output
    // stays bounded while the other reaches the complete authoritative state.
    Snapshot blast{8,1332,{}};
    for(std::uint64_t id=2;id<=1332;++id)blast.cuts.push_back({id,{},volume(float(id))});
    const Message explosion{Type::map_edit_snapshot,encode(blast)};
    std::array<Assembler,33> receiving;
    std::array<std::size_t,33> offsets{};
    std::array<unsigned,33> queue_bytes{};
    std::array<bool,33> finished{};
    for(int slot:{1,26})receiving[slot].reset(8);
    int cursor=0,fast_completed=0;
    for(int frame=1;frame<=150&&(!finished[1]||!finished[26]);++frame){
        queue_bytes[26]=0;if(frame>100)queue_bytes[1]=0;
        pump_fragments(32,cursor,[&](int slot){
            if((slot!=1&&slot!=26)||finished[slot])return false;
            const auto bytes=fragment(8,50,explosion,offsets[slot]);
            if(reliable_room(3990,queue_bytes[slot],true,false)<bytes.size()+2)return false;
            queue_bytes[slot]+=static_cast<unsigned>(bytes.size()+2);
            offsets[slot]+=std::min(fragment_bytes,explosion.payload.size()-offsets[slot]);
            if(auto result=receiving[slot].accept(bytes)){
                assert(snapshot(result->payload)==blast);finished[slot]=true;
                if(slot==26)fast_completed=frame;
            }
            return true;
        });
        assert(queue_bytes[1]<=1536&&queue_bytes[26]<=1536);
        if(frame==100)assert(!finished[1]&&finished[26]);
    }
    assert(finished[1]&&finished[26]&&fast_completed<=46);
    std::printf("1331-cut snapshot: fast client complete in %d simulated frames; stalled peer bounded then completes\n",fast_completed);
    assert(!assembly.accept(fragment(7,2,full,0)));
    assembly.reset(8);assert(!assembly.accept(fragment(8,3,full,0)));
    rejects([&]{assembly.accept(fragment(8,3,full,2*fragment_bytes));});
    assert(!assembly.accept(fragment(8,4,full,0))); // Restart after gap without partial application.
    auto small=Message{Type::map_edit_snapshot,encode(Snapshot{8,400,{}})};
    assert(assembly.accept(fragment(8,5,small,0)));assert(!assembly.accept(fragment(8,4,full,fragment_bytes)));
    assembly.reset(0);assert(!assembly.accept(fragment(8,6,small,0)));
    Replica unsigned_peer;unsigned_peer.reset(8);
    assert(unsigned_peer.accept(Snapshot{8,0x7fffffffffffffffull,{}})==Applied::changed);
    Delta high{8,0x7fffffffffffffffull,0x8000000000000000ull,Operation::add,{},volume()};
    assert(unsigned_peer.accept(delta(encode(high)))==Applied::changed);
    high.base=UINT64_MAX;high.revision=0;rejects([&]{encode(high);});
    Snapshot full_capacity{8,max_cuts+1,{}};
    for(std::uint64_t id=2;id<=max_cuts+1;++id)full_capacity.cuts.push_back({id,{},volume()});
    Replica bounded;bounded.reset(8);bounded.accept(snapshot(encode(full_capacity)));
    rejects([&]{bounded.accept(Delta{8,max_cuts+1,max_cuts+2,Operation::add,{},volume()});});
    assert(bounded.state()==full_capacity);
    auto bad=encode(Delta{8,1,2,Operation::clear,{},{}});bad[24]=4;rejects([&]{delta(bad);});
    for(std::size_t i=0;i<64;++i){
        auto add=encode(Delta{8,1,2,Operation::add,{},volume()});rejects([&]{delta(std::span(add).first(i));});
    }
    std::puts("{\"map_edit_state\":\"passed\",\"native_fragment_bytes_max\":186,\"excavation_enabled\":false}");
}
