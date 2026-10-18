// Reuse the matched SDK, extracted production PM functions and hull callbacks.
#define main legacy_motion_fixture_main
#include "status_motion_tests.cpp"
#undef main
#include "goldcraft/native_water.hpp"
#include <fstream>
#include <limits>

using WaterScope=goldcraft::native_water::MovementScope<playermove_t>;
using WaterState=goldcraft::native_water::State;
void waterStep(playermove_t& m,const WaterState* state){
    pmove=&m;WaterScope scope(m,state);
    if(!WaterScope::water(PM_WaterMove))PM_WaterMove();
    for(int i=0;i<3;++i)m.velocity[i]-=m.basevelocity[i];
}
int main(int argc,char** argv)try{
    using namespace goldcraft::native_water;
    struct Moves{playermove_t m,old,split,normal;};auto store=std::make_unique<Moves>();
    auto& m=store->m;auto& old=store->old;auto& split=store->split;auto& normal=store->normal;
    WaterState s{1,1,1,2,3,4,enabled|dolphins};
    auto ready=[&](){setup(m);m.waterlevel=3;m.watertype=CONTENTS_WATER;m.origin[2]=1000;m.onground=-1;m.frametime=.05f;nativeHeight=100000;nativeKind=CONTENTS_WATER;ceiling=100000;};
    ready();m.velocity[0]=400;m.velocity[2]=100;old=m;waterStep(m,&s);
    pmove=&old;goldcraft::status_motion::State legacy{1,1,1,2,3,4,goldcraft::status_motion::dolphins_grace};{Scope scope(old,&legacy);Scope::water(PM_WaterMove);}
    check(std::abs(m.velocity[0]-384)<.001f,"vanilla horizontal drag");
    check(std::abs(m.velocity[2]-76.8f)<.001f,"vertical drag remains .8 then gravity");
    check(std::abs(old.velocity[2]-m.velocity[2])>10,"old isotropic water negative control");
    check(m.origin[0]==20&&m.origin[2]==1005,"move occurs before drag through real PM mover");
    check(m.movevars==&movement&&m.cmd.forwardmove==0&&movement.friction==4&&movement.accelerate==10,"move vars restored");
    ready();m.cmd.forwardmove=250;waterStep(m,&s);check(std::abs(m.velocity[0]-12.8f*.96f)<.001f,"vanilla .02 input acceleration");
    ready();m.cmd.forwardmove=m.cmd.sidemove=250;waterStep(m,&s);
    check(std::abs(std::hypot(m.velocity[0],m.velocity[1])-12.8f*.96f)<.001f,"diagonal input normalized");
    for(float dt:{.001f,.008f,.016f,.05f,.1f,.255f}){
        ready();m.frametime=dt;m.velocity[0]=200;m.velocity[2]=-100;m.cmd.forwardmove=250;split=m;
        waterStep(m,&s);split.frametime=dt/2;waterStep(split,&s);waterStep(split,&s);
        check(std::abs(m.velocity[0]-split.velocity[0])<.003f&&std::abs(m.velocity[2]-split.velocity[2])<.003f,"command duration composes velocity");
    }
    ready();m.basevelocity[0]=30;m.velocity[0]=400;waterStep(m,&s);
    check(std::abs(m.velocity[0]-384)<.001f&&std::abs(m.origin[0]-21.5f)<.001f,"native conveyor preserved without extra drag");
    ready();s.flags|=slow_falling;m.velocity[2]=-100;waterStep(m,&s);check(std::abs(m.velocity[2]+80.4f)<.001f,"slowfall water gravity");s.flags=enabled|dolphins;
    s.efficiency=1;s.speed=.1f;s.swim=2;
    auto c=coefficients(s,true,false);check(c.drag==.96f&&std::abs(c.acceleration-.2f)<.0001f,"efficiency and swim speed compose before dolphin drag");
    c=coefficients(s,false,false);check(std::abs(c.acceleration-.12f)<.0001f,"airborne efficiency halved");
    s.flags=enabled;c=coefficients(s,true,false);check(std::abs(c.drag-.54600006f)<.000001f,"vanilla efficient water drag");
    s={1,1,1,2,3,4,enabled|dolphins};
    for(int blocked=0;blocked<11;++blocked){
        ready();m.velocity[0]=100;
        if(blocked==0)m.dead=1;if(blocked==1)m.deadflag=1;if(blocked==2)m.spectator=1;if(blocked==3)m.iuser1=1;
        if(blocked==4)m.movetype=MOVETYPE_NOCLIP;if(blocked==5)m.flags|=1<<24;if(blocked==6)m.flags|=1<<12;
        if(blocked==7)m.waterlevel=1;if(blocked==8)m.watertype=CONTENTS_LAVA;if(blocked==9)m.waterjumptime=100;
        if(blocked==10)s.flags=0;normal=m;waterStep(m,&s);waterStep(normal,nullptr);
        check(m.velocity[0]==normal.velocity[0]&&m.velocity[2]==normal.velocity[2],"excluded/native state untouched");
    }
    s.flags=enabled|dolphins;ready();ceiling=1050;m.velocity[2]=500;waterStep(m,&s);check(m.origin[2]<=1014,"ceiling hull collision retained");
    s.flags=enabled;s.drag=std::numeric_limits<float>::denorm_min();ready();m.frametime=.255f;m.cmd.forwardmove=250;waterStep(m,&s);
    check(std::isfinite(m.velocity[0])&&std::isfinite(m.velocity[2])&&m.velocity[1]==0,"extreme Mod drag remains finite under native velocity limits");
    s.drag=.8f;s.flags=enabled|dolphins;
    // Reader and receiver reject malformed or expired state before movement.
    auto packet=[&](){goldcraft::Writer w;w.u64(s.epoch);w.u64(s.revision);for(auto n:{s.slot,s.serial,s.spawn,s.life,s.flags})w.u32(n);for(auto v:{s.drag,s.speed,s.efficiency,s.swim,s.gravity})w.f32(v);return w.data;};
    auto bytes=packet();check(bytes.size()==56&&decode(bytes).flags==s.flags,"fixed water layout");
    for(std::size_t n=0;n<bytes.size();++n){bool rejected=false;try{decode(std::span(bytes).first(n));}catch(const goldcraft::ProtocolError&){rejected=true;}check(rejected,"short state rejected");}
    for(float invalid:{NAN,INFINITY,-.01f,1.01f}){s.efficiency=invalid;bool rejected=false;try{decode(packet());}catch(const goldcraft::ProtocolError&){rejected=true;}check(rejected,"bad efficiency rejected");}s.efficiency=0;
    Receiver receiver;check(receiver.accept(s,1)&&!receiver.accept(s,1.1),"duplicate does not renew lease");
    check(receiver.active(1,1,2,4,1.34)!=nullptr&&!receiver.active(1,1,2,4,1.35),"lease expiry");
    check(!receiver.active(2,1,2,4,1.1)&&!receiver.active(1,2,2,4,1.1)&&!receiver.active(1,1,3,4,1.1)&&!receiver.active(1,1,2,5,1.1),"identity isolation");
    if(argc==2){
        std::ifstream oracle(argv[1]);check(bool(oracle),"real NeoForge oracle opened");unsigned compared=0,ground;float x,y,z,expected[3];
        while(oracle>>s.flags>>s.drag>>s.speed>>s.efficiency>>s.swim>>s.gravity>>ground>>x>>y>>z>>expected[0]>>expected[1]>>expected[2]){
            ready();m.onground=ground?0:-1;m.velocity[0]=x;m.velocity[1]=y;m.velocity[2]=z;m.cmd.forwardmove=250;
            waterStep(m,&s);
            for(int axis=0;axis<3;++axis)if(std::abs(m.velocity[axis]-expected[axis])>.005f){
                std::cerr<<"oracle case "<<compared<<" axis "<<axis<<" actual "<<m.velocity[axis]<<" expected "<<expected[axis]<<'\n';
                check(false,"native result differs from actual vanilla travel");
            }
            check(true,"actual vanilla travel velocity matched");++compared;
        }
        check(compared>=62,"all active vanilla reference combinations compared");
    }
    std::cout<<checks<<" native water checks: vanilla coefficients, real PM water mover, lifetime and input/collision preservation\n";return 0;
}catch(const std::exception& e){std::cerr<<"native water check "<<checks<<" failed: "<<e.what()<<'\n';return 1;}
