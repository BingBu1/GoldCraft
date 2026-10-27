// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/player_animation.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    struct Model {studiohdr_t header{};mstudioseqdesc_t sequence[300]{};} model;
    auto& h=model.header;h.length=sizeof(model);h.numseq=300;h.seqindex=offsetof(Model,sequence);
    std::strcpy(model.sequence[267].label,"ref_shoot_skull8");model.sequence[267].numframes=6;
    std::strcpy(model.sequence[269].label,"ref_reload_skull8");model.sequence[269].numframes=88;
    std::strcpy(model.sequence[270].label,"crouch_die");
    std::strcpy(model.sequence[99].label,"saved99");std::strcpy(model.sequence[100].label,"saved100");
    const auto original=model;
    cl_entity_t entity{};entity.curstate.sequence=267;entity.curstate.gaitsequence=4;entity.latched.prevsequence=269;
    entity_state_t player{};player.sequence=267;player.gaitsequence=4;
    int checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("animation check "+std::to_string(checks));};
    try {
        cvar_t cache{};cache.value=1;
        {csol::client::PlayerBoneCacheScope scope(&cache);check(cache.value==0);
            {csol::client::PlayerBoneCacheScope nested(&cache);check(cache.value==0);}
            check(cache.value==0);}
        check(cache.value==1);
        cache.value=0;{csol::client::PlayerBoneCacheScope scope(&cache);check(cache.value==0);}
        check(cache.value==0);
        {csol::client::PlayerBoneCacheScope scope(nullptr);}
        {csol::client::PlayerSequenceScope scope(&h,&entity,&player);
            check(scope.Mapped() && entity.curstate.sequence==100 && player.sequence==100);
            check(entity.latched.prevsequence==99 && player.gaitsequence==4);
            check(model.sequence[100].numframes==6 && model.sequence[99].numframes==88);}
        check(entity.curstate.sequence==267 && player.sequence==267 && entity.latched.prevsequence==269);
        check(std::memcmp(&model,&original,sizeof(model))==0);
        entity.latched.prevsequence=267;
        {csol::client::PlayerSequenceScope scope(&h,&entity,&entity.curstate);check(scope.Mapped() && entity.latched.prevsequence==100);}
        check(entity.curstate.sequence==267 && entity.latched.prevsequence==267 && std::memcmp(&model,&original,sizeof(model))==0);
        for(int previous:{99,100}) {
            entity.latched.prevsequence=previous;
            {csol::client::PlayerSequenceScope scope(&h,&entity,&player);
                check(scope.Mapped() && entity.curstate.sequence!=previous && entity.latched.prevsequence==previous);
                check(std::memcmp(&model.sequence[previous],&original.sequence[previous],sizeof(mstudioseqdesc_t))==0);}
            check(entity.curstate.sequence==267 && std::memcmp(&model,&original,sizeof(model))==0);
        }
        for(int seq:{0,8,9,100,270,300,-1}){entity.curstate.sequence=seq;csol::client::PlayerSequenceScope scope(&h,&entity,&player);check(!scope.Mapped());}
        entity.curstate.sequence=267;h.length=sizeof(studiohdr_t);
        {csol::client::PlayerSequenceScope scope(&h,&entity,&player);check(!scope.Mapped());}
        std::cout<<"PASS player animation scope "<<checks<<" checks\n";
    }catch(const std::exception& e){std::cerr<<e.what();return 1;}
}
