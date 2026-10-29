// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace death_events_test
{
namespace
{
std::vector<int> Used(const EventEngine& engine)
{ std::vector<int> result; for (const auto& call : engine.uses) result.push_back(call.index); return result; }
void UseTuple(const EventEngine& engine)
{
    for (const auto& call : engine.uses)
        Check(!call.activator && !call.caller && call.type == USE_SET && static_cast<int>(call.type) == 2 &&
            call.value == 0.0f && !std::signbit(call.value), "exact virtual Use null/null/SET/positive-zero tuple");
}
void Cancel(Fixture& f, CCSOLScenarioMonster& monster, int kind)
{
    switch (kind) {
    case 0: Require(f.requests.Reset(), "callback resets dispatcher generation"); break;
    case 1: Require(f.map.BeginRound(0, 10, 0, f.events), "callback restarts same map and round index"); break;
    case 2: f.Reborrow(monster); break;
    case 3: ++monster.edict()->serialnumber; break;
    case 4: Require(f.pool.Shutdown(), "callback shuts down pool"); break;
    case 5: f.DestroyMapStorage(); break;
    default: throw std::runtime_error("unknown cancellation case");
    }
}
}

void TargetCases()
{
    struct Suffix { int priority, id, overrideId, expected; };
    for (const auto c : {Suffix{0,40,-1,-1}, {0,40,0,-1}, {0,40,7,7}, {1,40,-1,40},
            {1,40,0,40}, {2,40,7,7}, {1,-1,-1,-1}, {1,0,-1,0}, {2,2147483647,-1,2147483647}}) {
        Fixture f; auto& monster = f.Monster(c.priority,c.id,c.overrideId); CCSOLMonsterDeathStages death(monster);
        f.engine.AddTarget(500, ("mm_zb_die_" + std::to_string(c.expected)).c_str()); f.engine.ClearCapture();
        Check(f.targets.Fire(death,f.error) && f.error.message.empty(), "target suffix dispatch succeeds");
        Check(Used(f.engine) == (c.expected < 0 ? std::vector<int>{} : std::vector<int>{500}), "original suffix gates select expected target");
        Check(c.expected >= 0 || f.engine.Queried('f').empty(), "absent suffix performs no engine search"); UseTuple(f.engine);
    }
    {
        Fixture f; auto& monster=f.Monster(1,40); CCSOLMonsterDeathStages death(monster);
        f.engine.AddTarget(530,"mm_zb_die_40"); f.engine.AddTarget(510,"mm_zb_die_40");
        f.engine.AddTarget(520,"MM_ZB_DIE_40"); auto& marked=f.engine.AddTarget(540,"mm_zb_die_40"); marked.pev->flags|=FL_KILLME;
        f.engine.ClearCapture(); Check(f.targets.Fire(death,f.error),"multi-target direct traversal");
        Check(Used(f.engine)==std::vector<int>{510,530,540},"dynamic numeric order, exact case and no KILLME filter");
        Check(f.engine.Queried('f')==std::vector<int>{0,510,530,540} && f.engine.Queried('l').empty(),"raw cursor resumes without INDEXENT reconstruction"); UseTuple(f.engine);
    }
    {
        Fixture f; auto& monster=f.Monster(1,40); CCSOLMonsterDeathStages death(monster);
        auto& first=f.engine.AddTarget(500,"mm_zb_die_40"); auto* removed=f.engine.AddTarget(600,"mm_zb_die_40").edict();
        auto* renamed=f.engine.AddTarget(650,"mm_zb_die_40").edict();
        first.action=[&] {
            const_cast<csol::scenario::MonsterType*>(monster.Type())->column19=99;
            f.engine.Dispose(removed); renamed->v.targetname=Engine::AllocString("changed");
            f.engine.AddTarget(490,"mm_zb_die_40"); f.engine.AddTarget(700,"mm_zb_die_40"); f.engine.AddTarget(710,"mm_zb_die_99");
        };
        f.engine.ClearCapture(); Check(f.targets.Fire(death,f.error),"mutable successor set");
        Check(Used(f.engine)==std::vector<int>{500,700},"removed renamed and earlier targets excluded; new higher target included");
        for(const auto& row:f.engine.trace) if(row.kind=='f') Check(row.name=="mm_zb_die_40","formatted target name remains fixed after callback");
    }
    for(bool replace:{false,true}) {
        Fixture f; auto& monster=f.Monster(1,40); CCSOLMonsterDeathStages death(monster);
        auto& first=f.engine.AddTarget(500,"mm_zb_die_40"); auto* ed=first.edict(); f.engine.AddTarget(600,"mm_zb_die_40");
        first.action=[&,ed] {
            f.engine.Dispose(ed,!replace);
            if(replace) GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&ed->v));
        };
        f.engine.ClearCapture(); Check(f.targets.Fire(death,f.error),"synchronously destroyed current private is safe");
        Check(Used(f.engine)==std::vector<int>{500,600} && f.engine.Queried('f')==std::vector<int>{0,500,600},"old private never needed for next raw cursor");
    }
    for(bool sentinel:{false,true}) {
        Fixture f; auto& monster=f.Monster(1,40); CCSOLMonsterDeathStages death(monster); f.engine.worldSentinel=sentinel;
        f.engine.ClearCapture(); Check(f.targets.Fire(death,f.error) && f.engine.uses.empty(),"null and world sentinels end successfully");
    }
    {
        Fixture f; auto& monster=f.Monster(1,40); CCSOLMonsterDeathStages death(monster);
        auto* empty=f.engine.Raw("test_empty",500); empty->v.targetname=Engine::AllocString("mm_zb_die_40");
        f.engine.AddTarget(600,"mm_zb_die_40"); f.engine.ClearCapture();
        Check(f.targets.Fire(death,f.error) && f.engine.uses.empty() && f.engine.Queried('f')==std::vector<int>{0},"empty private ends rather than skips");
    }
    for(int fault=0;fault<4;++fault) {
        Fixture f; auto& monster=f.Monster(1,40); CCSOLMonsterDeathStages death(monster);
        auto* ed=f.engine.AddTarget(500,"mm_zb_die_40").edict();
        if(fault==0) f.engine.findOverride=[ed](edict_t*,const char*){return ed;};
        if(fault==1) {ed->free=TRUE;f.engine.findOverride=[ed](edict_t*,const char*){return ed;};}
        if(fault==2) f.engine.indexReplies[ed]=f.engine.globals.maxEntities;
        if(fault==3) f.engine.indexReplies[ed]=-1;
        f.engine.ClearCapture(); Check(!f.targets.Fire(death,f.error) && !f.error.message.empty(),"invalid engine target order or capacity refused");
        Check(f.engine.uses.size()==(fault==0?1u:0u),"invalid target never used beyond valid prefix");
    }
    for(int missing=0;missing<3;++missing) {
        Fixture f;auto& monster=f.Monster(1,40);CCSOLMonsterDeathStages death(monster);f.engine.AddTarget(500,"mm_zb_die_40");
        const auto complete=g_engfuncs;auto* globals=gpGlobals;
        if(missing==0)g_engfuncs.pfnFindEntityByString=nullptr;
        if(missing==1)g_engfuncs.pfnIndexOfEdict=nullptr;
        if(missing==2)gpGlobals=nullptr;
        f.engine.ClearCapture();Check(!f.targets.Fire(death,f.error)&&f.engine.trace.empty()&&f.engine.uses.empty(),"missing target lookup service stops before engine calls");
        g_engfuncs=complete;gpGlobals=globals;
    }
    {
        Fixture f; auto& outer=f.Monster(1,40); auto& inner=f.Monster(1,50);
        CCSOLMonsterDeathStages a(outer),b(inner); auto& first=f.engine.AddTarget(500,"mm_zb_die_40");
        f.engine.AddTarget(600,"mm_zb_die_40"); f.engine.AddTarget(550,"mm_zb_die_50");
        first.action=[&]{csol::scenario::ConfigError nested;Check(f.targets.Fire(b,nested),"nested target traversal succeeds with own cursor");};
        f.engine.ClearCapture(); Check(f.targets.Fire(a,f.error),"outer target traversal resumes");
        Check(Used(f.engine)==std::vector<int>{500,550,600},"nested names and cursors are independent");
    }
}

void NoticeCases()
{
    {
        Fixture f; auto& monster=f.Monster(); CCSOLMonsterDeathStages death(monster); const int index=Engine::Index(monster.edict());
        f.engine.globals.maxClients=10;
        f.engine.Client(1).m_iTeam=CT; auto& dead=f.engine.Client(2); dead.m_iTeam=TERRORIST;dead.pev->health=0;dead.pev->deadflag=DEAD_DEAD;
        auto& spectator=f.engine.Client(3);spectator.m_iTeam=SPECTATOR;spectator.pev->flags|=FL_DORMANT;
        f.engine.AddTarget(4,"foreign");f.engine.Raw("empty",5);f.engine.Client(6).edict()->free=TRUE;
        auto& noPev=f.engine.Client(7);auto* noPevEd=noPev.edict();noPev.pev=nullptr;
        auto& wrong=f.engine.Client(8);auto* wrongEd=wrong.edict();wrongEd->v.pContainingEntity=&f.engine.world;
        auto& final=f.engine.Client(10);final.pev->health=-1;final.pev->deadflag=DEAD_DEAD;final.pev->flags|=FL_DORMANT;final.m_iTeam=UNASSIGNED;
        f.engine.ClearCapture();Check(f.notice.Send(death,f.error),"sparse player traversal");
        noPev.pev=&noPevEd->v;wrongEd->v.pContainingEntity=wrongEd;
        Check(f.engine.Recipients()==std::vector<int>{1,2,3,10},"dead teams spectators dormant receive; invalid slots skipped");
        Check(f.engine.Queried('l')==std::vector<int>{1,2,3,4,5,6,7,8,9,10},"ascending slots including holes");
        for(const auto& packet:f.engine.packets)Check(DeathPacket(packet,packet.slot,index),"real notice exact per-recipient payload");
    }
    for(int kind=0;kind<4;++kind) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);
        auto* first=f.engine.Client(1).edict();f.engine.Client(2);f.engine.globals.maxClients=2;
        if(kind==0)f.engine.zeroOffsets.insert(1);
        bool changed=false;f.engine.onQuery=[&](char q,int slot){if(kind && q=='o' && slot==1 && !changed){changed=true;
            if(kind==1)f.engine.Dispose(first);
            if(kind==2)++first->serialnumber;
            if(kind==3){f.engine.Dispose(first,false);GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&first->v));}
        }};
        f.engine.ClearCapture();Check(f.notice.Send(death,f.error)&&f.engine.Recipients()==std::vector<int>{2},"zero offset removed recycled or foreign recipient skips to next slot");
    }
    for(bool grow:{false,true}) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);for(int i=1;i<=3;++i)f.engine.Client(i);
        f.engine.globals.maxClients=grow?1:3;bool changed=false;
        f.engine.onMessage=[&](char q){if(q=='e'&&!changed){changed=true;f.engine.globals.maxClients=grow?3:0;}};
        f.engine.ClearCapture();Check(f.notice.Send(death,f.error),"dynamic maxClients change accepted");
        Check(f.engine.Recipients()==(grow?std::vector<int>{1,2,3}:std::vector<int>{1}),"upper bound reread after each recipient");
    }
    for(int maximum:{-1,33,65536}) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);f.engine.globals.maxClients=maximum;f.engine.ClearCapture();
        Check(!f.notice.Send(death,f.error)&&f.engine.Queried('l').empty()&&f.engine.packets.empty(),"corrupt host capacity rejected before slot query");
    }
    for(int maximum:{-1,33,65536}) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);f.engine.Client(1);f.engine.Client(2);f.engine.globals.maxClients=2;
        f.engine.onMessage=[&](char point){if(point=='e')f.engine.globals.maxClients=maximum;};f.engine.ClearCapture();
        Check(!f.notice.Send(death,f.error)&&f.engine.Recipients()==std::vector<int>{1}&&f.engine.Queried('l')==std::vector<int>{1},"corrupt capacity introduced after first packet stops later slot queries");
    }
    {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);f.engine.globals.maxClients=0;f.engine.ClearCapture();
        Check(f.notice.Send(death,f.error)&&f.engine.Queried('l').empty()&&f.engine.packets.empty(),"zero client round completes without packets");
    }
    for(int index:{1,255,256,32767,32768,65535,0,-1,65536}) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);f.engine.Client(1);f.engine.globals.maxClients=1;
        f.engine.indexReplies[monster.edict()]=index;f.engine.ClearCapture();const bool sent=f.notice.Send(death,f.error);
        if(index>0&&index<=65535)Check(sent&&f.engine.packets.size()==1&&DeathPacket(f.engine.packets[0],1,index),"notice copied index preserves signed-short wire bits");
        else Check(!sent&&f.engine.packets.empty()&&f.engine.Queried('l').empty(),"invalid death index stops before recipients");
    }
    for(int missing=0;missing<8;++missing) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);f.engine.Client(1);f.engine.globals.maxClients=1;
        const auto complete=g_engfuncs;
        if(missing==0)f.messages={};
        if(missing==1)g_engfuncs.pfnPEntityOfEntIndex=nullptr;
        if(missing==2)g_engfuncs.pfnIndexOfEdict=nullptr;
        if(missing==3)g_engfuncs.pfnEntOffsetOfPEntity=nullptr;
        if(missing==4)g_engfuncs.pfnMessageBegin=nullptr;
        if(missing==5)g_engfuncs.pfnWriteByte=nullptr;
        if(missing==6)g_engfuncs.pfnWriteShort=nullptr;
        if(missing==7)g_engfuncs.pfnMessageEnd=nullptr;
        f.engine.ClearCapture();Check(!f.notice.Send(death,f.error)&&f.engine.packets.empty(),"notice missing registration or required API emits no partial packet");g_engfuncs=complete;
    }
    {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);f.engine.Client(1);f.engine.globals.maxClients=1;
        Require(f.pool.Free(monster),"early return retains death lease");f.engine.ClearCapture();
        Check(f.notice.Send(death,f.error)&&f.engine.packets.size()==1,"same-lease early Free remains a valid captured death");
    }
    for(int invalid=0;invalid<3;++invalid) {
        Fixture f;auto& monster=f.Monster(1,40);CCSOLMonsterDeathStages death(monster);f.engine.Client(1);
        if(invalid==0)Require(f.map.EndMap(),"end map before invalid entry");
        if(invalid==1){Require(!f.requests.EndTimedEvents({f.requests.Generation(),999}),"invalid timed scope sets real dispatcher error");Require(!f.requests.Error().empty(),"dispatcher error published");}
        if(invalid==2)Require(f.pool.RestartRound(),"round restart invalidates death type");
        f.engine.ClearCapture();Check(!f.notice.Send(death,f.error)&&!f.targets.Fire(death,f.error)&&f.engine.trace.empty(),"unavailable map error dispatcher or stale death rejected without engine calls");
    }
}

void CancellationCases()
{
    for(char point:{'i','l','o','b','w','s','e'})for(int kind=0;kind<6;++kind) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);const int index=Engine::Index(monster.edict());
        f.engine.Client(1);f.engine.Client(2);f.engine.globals.maxClients=2;bool fired=false;std::size_t tail{};
        const auto cancel=[&]{if(fired)return;fired=true;Cancel(f,monster,kind);tail=f.engine.trace.size();};
        f.engine.onQuery=[&](char q,int){if(q==point)cancel();};f.engine.onMessage=[&](char q){if(q==point)cancel();};f.engine.ClearCapture();
        Check(!f.notice.Send(death,f.error)&&fired&&!f.error.message.empty(),"query or packet lifecycle change cancels notice");
        const bool began=point=='b'||point=='w'||point=='s'||point=='e';
        Check(f.engine.packets.size()==(began?1u:0u),"cancellation permits no later recipient packet");
        if(began)Check(DeathPacket(f.engine.packets[0],1,index),"already-open packet completes from captured scalar after cancellation");
        std::string remaining;for(std::size_t i=tail;i<f.engine.trace.size();++i)remaining+=f.engine.trace[i].kind;
        const std::string expected=point=='b'?"wse":point=='w'?"se":point=='s'?"e":"";
        Check(remaining==expected,"no stale engine query after invalidation; only remaining current-packet writes");
    }
    for(char point:{'f','i','u'})for(int kind=0;kind<6;++kind) {
        Fixture f;auto& monster=f.Monster(1,40);CCSOLMonsterDeathStages death(monster);auto& first=f.engine.AddTarget(500,"mm_zb_die_40");f.engine.AddTarget(600,"mm_zb_die_40");
        bool fired=false;std::size_t tail{};const auto cancel=[&]{if(fired)return;fired=true;Cancel(f,monster,kind);tail=f.engine.trace.size();};
        f.engine.onQuery=[&](char q,int){if(q==point)cancel();};if(point=='u')first.action=cancel;
        f.engine.ClearCapture();Check(!f.targets.Fire(death,f.error)&&fired,"target lookup/index/Use invalidation cancels");
        Check(f.engine.uses.size()==(point=='u'?1u:0u)&&f.engine.trace.size()==tail,"target cancellation stops before touching later raw cursor");
    }
    {
        Fixture f;auto& monster=f.Monster();BehaviorStats stats;
        Require(monster.InstallBehavior(std::make_unique<BehaviorProbe>(stats)),"install actual behavior ownership probe");
        CCSOLMonsterDeathStages death(monster);f.engine.Client(1);f.engine.globals.maxClients=1;bool nested=false;
        stats.onDestroy=[&]{csol::scenario::ConfigError error;const auto before=f.engine.trace.size();nested=!f.notice.Send(death,error);Check(f.engine.trace.size()==before,"pool Busy rejects captured notice before engine queries");};
        BehaviorStats replacement;Require(monster.InstallBehavior(std::make_unique<BehaviorProbe>(replacement)),"replacement invokes old behavior destructor while pool Busy");
        Check(nested,"busy callback notice refused");stats.onDestroy={};f.engine.ClearCapture();Check(f.notice.Send(death,f.error),"same lease valid again once busy operation ends");
        // Keep behavior stats alive through destruction, before their stack ends.
        Require(f.pool.Shutdown(),"detach replacement behavior before probe stats expire");
    }
}

void MessageCases()
{
    const std::array<std::uint8_t,4> wave{27,7,0,128};
    for(bool outerWave:{false,true})for(bool innerWave:{false,true})for(char point:{'b','w','s','e'})for(int reset=0;reset<3;++reset) {
        Fixture f;auto& monster=f.Monster();CCSOLMonsterDeathStages death(monster);auto* recipient=f.engine.Client(1).edict();f.engine.globals.maxClients=1;
        CCSOLScenarioMessages other;Require(other.Register(),"independent message instance registered");
        bool entered=false,nestedResult=true;
        f.engine.onMessage=[&](char q){if(q!=point||entered)return;entered=true;
            auto& target=reset==1?other:f.messages;
            if(reset==2){f.messages={};Require(f.messages.Register(),"callback can reregister object without clearing static packet gate");}
            if(innerWave)nestedResult=target.SendWaveEvent(wave);
            else if(reset==1)nestedResult=target.SendMonsterDeath(recipient,32768);
            else {csol::scenario::ConfigError nested;nestedResult=f.notice.Send(death,nested);}
        };
        f.engine.ClearCapture();const bool outer=outerWave?f.messages.SendWaveEvent(wave):f.notice.Send(death,f.error);
        Check(outer&&entered&&!nestedResult&&f.engine.packets.size()==1,"four-way same/cross/reset instance reentry refused without aborting outer packet");
        const auto& packet=f.engine.packets[0];Check(outerWave?WavePacket(packet,32768):DeathPacket(packet,1,Engine::Index(monster.edict())),"outer packet remains exact after denied reentry");
        f.engine.onMessage={};f.engine.ClearCapture();
        Check(f.messages.SendWaveEvent(wave)&&f.messages.SendMonsterDeath(recipient,65535)&&f.engine.packets.size()==2,"shared packet gate releases for later sequential sends");
        Check(WavePacket(f.engine.packets[0],32768)&&DeathPacket(f.engine.packets[1],1,65535),"recovered packet content and order");
    }
}
}

int main(int argc,char** argv)
{
    using namespace death_events_test;
    try {
        if(argc!=2)throw std::runtime_error("use --targets, --notice, --cancellation or --messages");
        const std::string_view mode=argv[1];
        if(mode=="--targets")TargetCases();
        else if(mode=="--notice")NoticeCases();
        else if(mode=="--cancellation")CancellationCases();
        else if(mode=="--messages")MessageCases();
        else throw std::runtime_error("unknown test group");
    } catch(const std::exception& error) {++failures;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"death-events checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
