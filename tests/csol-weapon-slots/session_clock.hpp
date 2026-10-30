// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside client.cpp's fixture namespace; all handlers remain production code.
int SessionClockChecks(const WeaponDefinition& knife)
{
    int clockChecks=0,clockFailures=0;
    auto check=[&](bool value,const std::string& label){++clockChecks;if(!value){++clockFailures;std::cerr<<"FAIL "<<label<<'\n';}};
    auto state=[](){std::ostringstream out;WeaponDiagnostics(out);return out.str();};
    auto packet=[&](int id){Send(WeaponCurrentMessage,EncodeWeaponCurrent({id,1,1,29,0}));};
    auto load=[&](){
        level="";now=0;SynchronizeFrameSession();
        level="maps/zs_lastclue.bsp";now=100;SynchronizeFrameSession();
        view={};view.health=100;hud=1;fast=0;
        Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));Legacy();
        WeaponOwnership owned{};owned[28]=owned[220]=true;Owned(owned);packet(220);
        unsigned char mask=2;messages.at("HideWeapon")("HideWeapon",1,&mask);
        sent.clear();
    };
    auto verify=[&](const std::string& label){
        const auto text=state();
        check(FindWeaponDefinition(220)&&FindWeaponDefinition(28),label+" retains sign-on definitions");
        check(CurrentExtendedWeapon().id==220&&ExtendedWeaponActive(),label+" retains current extended weapon");
        check(text.find("\"registered\":1,\"active\":220")!=std::string::npos,label+" retains HUD owner");
        check(text.find("\"hidden\":2")!=std::string::npos&&lastHudMask==3,label+" preserves native and extended hide state");
        check(text.find("\"id\":28,\"defined\":1,\"slot\":0")!=std::string::npos&&
              text.find("\"id\":220,\"defined\":1,\"slot\":1")!=std::string::npos,label+" retains both ownership entries");
        for(int slot=1;slot<=2;++slot){
            sent.clear();commands.at("slot"+std::to_string(slot))();
            usercmd_t cmd{};cmd.buttons=IN_ATTACK;WeaponInventoryCreateMove(.01f,&cmd,1);
            check(!(cmd.buttons&IN_ATTACK)&&sent==(slot==1?"weapon_ak47":"weapon_tknifeex2"),label+" slot"+std::to_string(slot)+" confirms");
        }
        std::cout<<"STATE "<<label<<" {"<<text.substr(1)<<",\"nativeHudMask\":"<<lastHudMask<<"}\n";
    };
    // Time alone is not a session identity: all these samples retain the same
    // map and sign-on state. Larger corrections expose the old one-second reset.
    for(float rollback:{.01f,.5f,1.f,1.5f,8.f,99.f}){
        load();now-=rollback;SynchronizeFrameSession();
        verify("same-level rollback "+std::to_string(rollback));
    }
    load();now=120;packet(220);now=99.5f;SynchronizeFrameSession();
    verify("packet ahead of interpolated frame");
    load();WeaponVideoInit();verify("video reinitialization");
    load();messages.at("ResetHUD")("ResetHUD",0,nullptr);
    check(!ExtendedWeaponActive()&&FindWeaponDefinition(220)&&FindWeaponDefinition(28),"ResetHUD clears current but preserves registrations");
    check(lastHudMask==0,"ResetHUD retracts extended weapon native-HUD hiding");
    WeaponOwnership owned{};owned[28]=owned[220]=true;Owned(owned);packet(220);
    check(ExtendedWeaponActive(),"post-ResetHUD inventory works without new definitions");
    load();level="";SynchronizeFrameSession();
    check(!ExtendedWeaponActive()&&!FindWeaponDefinition(220)&&!FindWeaponDefinition(28),"disconnect level transition retires all definitions");
    check(state().find("\"hidden\":0")!=std::string::npos,"disconnect clears adapter hide state");
    check(lastHudMask==0,"disconnect retracts extended weapon native-HUD hiding");
    level="maps/zs_lastclue.bsp";now=2;SynchronizeFrameSession();
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));Owned(owned);packet(220);
    check(ExtendedWeaponActive()&&!FindWeaponDefinition(28),"same-map reconnect accepts only new sign-on definitions");
    check(lastHudMask==1,"new extended weapon applies its own native-HUD hiding");
    load();level="maps/de_dust2.bsp";now=0;
    WeaponDefinition other=knife;other.id=221;other.name="weapon_session_fixture";
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(other));SynchronizeFrameSession();
    check(FindWeaponDefinition(221)&&!FindWeaponDefinition(220)&&!FindWeaponDefinition(28),"new-map sign-on before HUD_Frame survives frame synchronization");
    std::cout<<"session clock "<<clockChecks<<" checks, "<<clockFailures<<" failures\n";
    return clockFailures?1:0;
}
