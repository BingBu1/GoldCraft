#include <cassert>
#include <cstdio>
#include <initializer_list>
struct entvars_t {int iuser1{};};
struct CBaseEntity {entvars_t vars;entvars_t* pev=&vars;bool player=true;bool IsPlayer(){return player;}};
struct CBasePlayer:CBaseEntity {bool alive{};CBaseEntity* m_hObserverTarget{};bool IsAlive(){return alive;}};
constexpr int OBS_ROAMING=3;
#include "../../native/server/observer_view.inc"
int main(){
    CBasePlayer viewer,target;viewer.m_hObserverTarget=&target;
    unsigned checks=0;
    for(int mode=0;mode<=6;++mode)for(bool alive:{false,true}){
        viewer.pev->iuser1=mode;viewer.alive=alive;
        assert(ActorPresentationView(&viewer)==(!alive&&mode&&mode!=OBS_ROAMING?target.pev:viewer.pev));++checks;
    }
    viewer.alive=false;viewer.pev->iuser1=4;
    viewer.m_hObserverTarget=nullptr;assert(ActorPresentationView(&viewer)==viewer.pev);++checks;
    viewer.m_hObserverTarget=&viewer;assert(ActorPresentationView(&viewer)==viewer.pev);++checks;
    target.player=false;viewer.m_hObserverTarget=&target;assert(ActorPresentationView(&viewer)==viewer.pev);++checks;
    std::printf("%u production observer anchor checks passed\n",checks);
}
