    // These are actual matched ReGameDLL classes created through exports,
    // not local substitutes with the same C++ names.
    for(const char* name:{"info_target","func_breakable","env_explosion","trigger_hurt",
        "func_door","func_water","func_train","func_tank","func_tanklaser","func_tankrocket",
        "func_tankmortar","func_tankcontrols"})
    {
        auto* ed=sdk_test::Engine::Create(sdk_test::Engine::String(name));
        auto* entity=CBaseEntity::Instance(ed);value=-1;error={};
        Check(read(*entity,value,error)&&value==0&&error.message.empty(),
            "actual SDK map source inherits original zero contract");
        Host host(runtime);host.source=entity;DamageState state;
        std::array<std::uint8_t,32> context{};
        Check(ApplyDamageCallback(*FindSkill(23),state,150,context,host,error)&&host.stopped==0,
            "map entity damage preserves cloak and does not reject map");
        sdk_test::Engine::Remove(ed);
    }
    for(const char* name:{"grenade","item_airtank","monster_mortar"})
    {
        auto* ed=sdk_test::Engine::Create(sdk_test::Engine::String(name));
        auto* entity=CBaseEntity::Instance(ed);value=-1;error={};
        sdk_test::Require(dynamic_cast<CGrenade*>(entity)!=nullptr,"real grenade inheritance");
        Check(read(*entity,value,error)&&value==WEAPON_HEGRENADE&&error.message.empty(),
            "actual SDK grenade family preserves original source4");
        Host host(runtime);host.source=entity;DamageState state;
        std::array<std::uint8_t,32> context{};context[20]=1;
        Check(ApplyDamageCallback(*FindSkill(23),state,150,context,host,error)&&host.stopped==1,
            "grenade source4 still ends key11 even with context20");
        sdk_test::Engine::Remove(ed);
    }
    CBasePlayerWeapon unsupported;
    struct UnverifiedUsp final:CUSP{} unsupportedDerived;
    for(CBasePlayerWeapon* entity:{&unsupported,static_cast<CBasePlayerWeapon*>(&unsupportedDerived)})
    {
        entity->m_iId=WEAPON_USP;value=-1;error={};
        Check(!read(*entity,value,error)&&error.message=="scenario source entity has no verified source adapter",
            "unverified SDK weapon cannot fall through to generic zero");
    }
    auto* uspEd=sdk_test::Engine::Create(sdk_test::Engine::String("weapon_usp"));
    auto* usp=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(uspEd));
    sdk_test::Require(usp!=nullptr,"real native weapon export");
    usp->m_iId=WEAPON_USP;value=-1;error={};
    Check(read(*usp,value,error)&&value==WEAPON_USP&&error.message.empty(),
        "verified native weapon still returns its real source ID");
    usp->m_iId=CSOL_WEAPON_K3;value=-1;error={};
    Check(!read(*usp,value,error)&&!error.message.empty(),"native type ID mismatch stays rejected");
    sdk_test::Engine::Remove(uspEd);
    struct ExplicitGrenade final:CGrenade,CSOLWeaponDamageSource
    { int DamageWeaponId()const override{return 726;} } explicitGrenade;
    value=-1;error={};
    Check(read(explicitGrenade,value,error)&&value==726&&error.message.empty(),
        "explicit grenade-derived source adapter precedes original grenade default");
