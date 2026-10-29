// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "interface.h"
#include "../../native/csol/server/game_rules_gate.h"
#include "../../native/csol/server/game_mode.h"
#include "../../native/include/goldcraft/csol_game_mode_api.hpp"
#include "resources.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace sdk_test
{
inline int checks{}, failures{};
inline void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
inline void Require(bool value, const char* label)
{ Check(value, label); if (!value) throw std::runtime_error(label); }
// Generated from the frozen enginefuncs_t declarations. Every unsupported
// engine edge reports its real name; no guessed generic zero-return ABI.
#include "engine_edge_names.inc"
template<int Id, class Function> struct Unsupported;
template<int Id, class R, class... A> struct Unsupported<Id, R(*)(A...)>
{ static R Call(A...) { throw std::runtime_error(std::string("unsupported engine edge: ") + engineEdgeNames[Id]); } };
template<int Id, class R, class... A> struct Unsupported<Id, R(*)(A..., ...)>
{ static R Call(A..., ...) { throw std::runtime_error(std::string("unsupported variadic engine edge: ") + engineEdgeNames[Id]); } };

class Probe : public CBaseEntity
{
public:
    std::array<unsigned char,128> sentinel{};
    int spawns{}, activates{}, thinks{}, touches{}, uses{}, blocks{};
    Probe() { sentinel.fill(0x6d); }
    void Spawn() override { ++spawns; }
    void Activate() override { ++activates; }
    void Think() override { ++thinks; }
    void Touch(CBaseEntity*) override { ++touches; }
    void Use(CBaseEntity*, CBaseEntity*, USE_TYPE, float) override { ++uses; }
    void Blocked(CBaseEntity*) override { ++blocks; }
};

struct Engine
{
    static constexpr int Capacity = 512;
    inline static Engine* self{};
    globalvars_t globals{};
    std::array<edict_t, Capacity> edicts{};
    std::array<char, 1024 * 1024> strings{};
    std::size_t nextString{1};
    DLL_FUNCTIONS api{};
    NEW_DLL_FUNCTIONS newApi{};
    struct PrivateBlock { std::unique_ptr<unsigned char[]> bytes; std::size_t size; };
    struct CvarValue { cvar_t storage{}; cvar_t* registered{}; std::string name, text; };
    std::map<edict_t*,PrivateBlock> privateBlocks;
    std::map<std::string,CvarValue> cvars;
    std::map<std::string,std::string> files{session_test::Resources()};
    std::map<void*,std::unique_ptr<unsigned char[]>> buffers;
    std::map<std::string,int> models, messages;
    std::vector<std::string> events;
    std::function<void()> onModel, onAllocate, onExecute;
    // Controlled engine boundaries; the SDK owns Spawn/Touch/Think/removal.
    std::function<void(const edict_t*)> onOffset;
    std::function<void(int, edict_t*)> onIndexLookup;
    std::function<void(edict_t*, const char*, const char*)> onFind;
    std::function<void(edict_t*)> onSize, onOrigin;
    std::function<void(edict_t*, TraceResult&)> onTrace;
    std::function<void(edict_t*, const char*)> onSound;
    // Observation only: real OnFree has returned, engine private memory has
    // not yet been released. Never run the retirement collector at this edge.
    std::function<void(edict_t*)> afterFreeCallback;
    std::array<unsigned char,16> pvs{}, pas{};
    Vector pvsOrigin{}, pasOrigin{};
    std::array<char,512> info{};
    int nextEntity{8}, allocations{}, releases{}, groupCalls{}, lastGroupMask{}, lastGroupOp{}, unknownExports{};
    explicit Engine()
    {
        Require(!self, "single fixture engine"); self = this; gpGlobals = &globals;
        globals.maxClients = 4; globals.maxEntities = Capacity; globals.deathmatch = 1; globals.time = 10;
        globals.pStringBase = strings.data(); globals.mapname = String("map_session_test");
        for (int i = 0; i < Capacity; ++i) { edicts[i].free = TRUE; edicts[i].serialnumber = 1; edicts[i].v.pContainingEntity = &edicts[i]; }
        edicts[0].free = FALSE;
        g_engfuncs = {};
#include "engine_edge_defaults.inc"
        g_engfuncs.pfnPvAllocEntPrivateData = Allocate;
        g_engfuncs.pfnPvEntPrivateData = [](edict_t* ed) { return ed->pvPrivateData; };
        g_engfuncs.pfnFreeEntPrivateData = FreePrivate;
        g_engfuncs.pfnAllocString = String;
        g_engfuncs.pfnSzFromIndex = [](int index) { return gpGlobals->pStringBase + index; };
        g_engfuncs.pfnIndexOfEdict = Index;
        g_engfuncs.pfnEntOffsetOfPEntity = [](const edict_t* ed) {
            const int offset=ed ? Index(ed)*static_cast<int>(sizeof(edict_t)) : 0;
            const auto callback=self->onOffset;if(callback)callback(ed);return offset;
        };
        g_engfuncs.pfnPEntityOfEntOffset = [](int offset) { return At(offset / static_cast<int>(sizeof(edict_t))); };
        g_engfuncs.pfnPEntityOfEntIndex = [](int index) {
            auto* ed=At(index);const auto callback=self->onIndexLookup;if(callback)callback(index,ed);return ed;
        };
        g_engfuncs.pfnGetVarsOfEnt = [](edict_t* ed) { return &ed->v; };
        g_engfuncs.pfnFindEntityByVars = [](entvars_t* vars) { return vars->pContainingEntity; };
        g_engfuncs.pfnCreateEntity = [] { return self->Reserve(self->nextEntity++); };
        g_engfuncs.pfnCreateNamedEntity = Create;
        g_engfuncs.pfnRemoveEntity = Remove;
        g_engfuncs.pfnNumberOfEntities = [] { return self->nextEntity; };
        g_engfuncs.pfnSetOrigin = [](edict_t* ed, const float* v) {
            ed->v.origin=Vector(v[0],v[1],v[2]);const auto callback=self->onOrigin;if(callback)callback(ed);
        };
        g_engfuncs.pfnSetSize = [](edict_t* ed, const float* lo, const float* hi) {
            ed->v.mins=Vector(lo[0],lo[1],lo[2]);ed->v.maxs=Vector(hi[0],hi[1],hi[2]);ed->v.size=ed->v.maxs-ed->v.mins;
            const auto callback=self->onSize;if(callback)callback(ed);
        };
        g_engfuncs.pfnTraceLine = [](const float*,const float* end,int,edict_t* skip,TraceResult* trace) {
            const auto callback=self->onTrace;Require(bool(callback),"TraceLine requires explicit controlled world in this SDK fixture");
            *trace={};trace->flFraction=1;trace->vecEndPos=Vector(end[0],end[1],end[2]);
            callback(skip,*trace);
        };
        g_engfuncs.pfnEmitSound = [](edict_t* ed,int channel,const char* sample,float,float,int,int) {
            self->Record("sound:"+std::to_string(channel)+":"+sample);const auto callback=self->onSound;if(callback)callback(ed,sample);
        };
        g_engfuncs.pfnGetFileSize = [](const char* path) {const auto it=self->files.find(path);return it==self->files.end()?-1:static_cast<int>(it->second.size());};
        g_engfuncs.pfnSetModel = [](edict_t* ed, const char* name) { ed->v.model=String(name); ed->v.modelindex=Model(name); };
        g_engfuncs.pfnPrecacheModel = Model; g_engfuncs.pfnModelIndex = Model;
        g_engfuncs.pfnModelFrames = [](int) { return 1; };
        g_engfuncs.pfnPrecacheSound = [](const char*) { return 1; };
        g_engfuncs.pfnPrecacheGeneric = [](const char*) { return 1; };
        g_engfuncs.pfnPrecacheEvent = [](int, const char*) -> unsigned short { return 1; };
        g_engfuncs.pfnRegUserMsg = Message;
        g_engfuncs.pfnLoadFileForMe = Load; g_engfuncs.pfnFreeFile = Free;
        g_engfuncs.pfnCVarRegister = RegisterCvar; g_engfuncs.pfnCvar_RegisterVariable = RegisterCvar;
        g_engfuncs.pfnCVarGetPointer = [](const char* name) { auto& c=self->Var(name); return c.registered ? c.registered : &c.storage; };
        g_engfuncs.pfnCVarGetFloat = [](const char* name) { return std::strtof(self->Var(name).text.c_str(),nullptr); };
        g_engfuncs.pfnCVarGetString = [](const char* name) { return self->Var(name).text.c_str(); };
        g_engfuncs.pfnCVarSetString = [](const char* name,const char* value) { self->Set(name,value); };
        g_engfuncs.pfnCVarSetFloat = [](const char* name,float value) { self->Set(name,std::to_string(value).c_str()); };
        g_engfuncs.pfnCvar_DirectSet = [](cvar_t* c,const char* value) { self->Set(c->name,value); };
        g_engfuncs.pfnIsDedicatedServer = [] { return 1; }; g_engfuncs.pfnIsCareerMatch = [] { return 0; };
        g_engfuncs.pfnGetGameDir = [](char* out) { std::strcpy(out,"cstrike"); };
        g_engfuncs.pfnServerCommand = [](const char* command) { self->Record(std::string("command:")+command); };
        g_engfuncs.pfnServerExecute = [] { self->Record("execute"); if(self->onExecute)self->onExecute(); };
        g_engfuncs.pfnAlertMessage = Alert;
        g_engfuncs.pfnServerPrint = [](const char* value) { self->Record(std::string("print:")+value); };
        g_engfuncs.pfnAddServerCommand = [](const char* name, void(*)()) { self->Record(std::string("register-command:")+name); };
        g_engfuncs.pfnGetInfoKeyBuffer = [](edict_t*) { return self->info.data(); };
        g_engfuncs.pfnInfoKeyValue = [](char*,const char*) -> char* { return const_cast<char*>(""); };
        g_engfuncs.pfnSetKeyValue = [](char*,const char* name,const char* value) { self->Record(std::string("info:")+name+"="+value); };
        g_engfuncs.pfnSetClientKeyValue = [](int,char*,const char*,const char*) {};
        g_engfuncs.pfnMessageBegin = [](int dest,int id,const float*,edict_t*) { self->Record("message:"+std::to_string(dest)+":"+std::to_string(id)); };
        g_engfuncs.pfnMessageEnd = [] {};
        g_engfuncs.pfnWriteByte = g_engfuncs.pfnWriteChar = g_engfuncs.pfnWriteShort = g_engfuncs.pfnWriteLong = g_engfuncs.pfnWriteEntity = [](int) {};
        g_engfuncs.pfnWriteCoord = g_engfuncs.pfnWriteAngle = [](float) {};
        g_engfuncs.pfnWriteString = [](const char*) {};
        g_engfuncs.pfnSetFatPVS = [](float* org) { self->pvsOrigin=Vector(org[0],org[1],org[2]); return self->pvs.data(); };
        g_engfuncs.pfnSetFatPAS = [](float* org) { self->pasOrigin=Vector(org[0],org[1],org[2]); return self->pas.data(); };
        g_engfuncs.pfnSetGroupMask = [](int mask,int op) { ++self->groupCalls; self->lastGroupMask=mask; self->lastGroupOp=op; };
        g_engfuncs.pfnRandomLong = [](int32 low,int32) -> int32 { return low; };
        g_engfuncs.pfnRandomFloat = [](float low,float) { return low; };
        g_engfuncs.pfnFindEntityByString = Find;
        g_engfuncs.pfnLightStyle = [](int style,const char*) { self->Record("lightstyle:"+std::to_string(style)); };
        g_engfuncs.pfnDecalIndex = [](const char*) { return 1; };
        g_engfuncs.pfnForceUnmodified = [](FORCE_TYPE,float*,float*,const char*) {};
        g_engfuncs.pfnGetPlayerUserId = [](edict_t* ed) { return Index(ed); };
        g_engfuncs.pfnGetPlayerAuthId = [](edict_t*) { return "STEAM_ID_LAN"; };
        g_engfuncs.pfnSetClientMaxspeed = [](edict_t* ed,float speed) { ed->v.maxspeed=speed; };
        g_engfuncs.pfnTime = [] { return gpGlobals->time; };
        // This controlled engine has no startup command-line arguments.
        // ReHLDS EngCheckParm also clears next when the token is absent.
        g_engfuncs.pfnEngCheckParm = [](const char* token,char** next) {
            self->Record(std::string("argument:")+token);if(next)*next=nullptr;return 0;
        };
        Set("skill","1"); Set("mp_roundtime","5"); Set("mp_freezetime","6"); Set("mp_c4timer","35");
        Set("mp_limitteams","2"); Set("mp_windifference","1"); Set("tutor_enable","0");
        Require(GetEntityAPI(&api,INTERFACE_VERSION) == 1,"real DLL_FUNCTIONS obtained");
        int version=NEW_DLL_FUNCTIONS_VERSION; Require(GetNewDLLFunctions(&newApi,&version)==1,"real NEW_DLL_FUNCTIONS obtained");
        CSOL_InitGameModes();
    }
    void Record(std::string event) { events.push_back(std::move(event)); }
    CvarValue& Var(const char* name)
    {
        auto [it, added]=cvars.try_emplace(name); auto& c=it->second;
        if(added){c.name=name;c.text="0";c.storage.name=c.name.data();c.storage.string=c.text.data();}
        return c;
    }
    void Set(const char* name,const char* value)
    { auto& c=Var(name); c.text=value; auto* p=c.registered?c.registered:&c.storage; p->string=c.text.data();p->value=std::strtof(c.text.c_str(),nullptr); }
    static void RegisterCvar(cvar_t* c)
    { auto& v=self->Var(c->name); v.registered=c; self->Set(c->name,c->string?c->string:""); }
    csol::IGameModeService& Modes()
    { auto* p=static_cast<csol::IGameModeService*>(CreateInterface(csol::GameModeServiceVersion,nullptr));Require(p!=nullptr,"real mode interface exported");return *p; }
    void Prepare(const char* mode,bool confirm=true)
    { Set("csol_game_mode",mode);const int locked=Modes().BeginMap();if(confirm)Require(Modes().ConfirmPluginIsolation(locked),"mode isolation confirmed"); }
    edict_t* Reserve(int index,const char* classname=nullptr)
    {
        Require(index>=0&&index<Capacity,"edict index within full allocation");auto* ed=&edicts[index];
        Require(!ed->pvPrivateData,"reserve cannot overwrite private object");
        const int serial=ed->serialnumber;*ed={};ed->serialnumber=serial;ed->v.pContainingEntity=ed;
        if(classname)ed->v.classname=String(classname);nextEntity=std::max(nextEntity,index+1);return ed;
    }
    static int Index(const edict_t* ed)
    { if(!ed)return 0;Require(ed>=self->edicts.data()&&ed<self->edicts.data()+Capacity,"edict belongs to contiguous engine array");return static_cast<int>(ed-self->edicts.data()); }
    static edict_t* At(int index) { return index>=0&&index<Capacity?&self->edicts[index]:nullptr; }
    static int String(const char* value)
    { auto& e=*self;const auto n=std::strlen(value)+1,pos=e.nextString;Require(pos+n<e.strings.size(),"string pool capacity");std::memcpy(e.strings.data()+pos,value,n);e.nextString+=n;return static_cast<int>(pos); }
    static void* Allocate(edict_t* ed,int32 count)
    {
        auto& e=*self;Require(count>0&&!ed->pvPrivateData,"fresh SDK private allocation");
        auto block=std::make_unique<unsigned char[]>(static_cast<std::size_t>(count)+128);
        std::memset(block.get(),0xa5,static_cast<std::size_t>(count)+128);std::memset(block.get()+64,0,count);
        ed->pvPrivateData=block.get()+64;e.privateBlocks.emplace(ed,PrivateBlock{std::move(block),static_cast<std::size_t>(count)});++e.allocations;
        e.Record("allocate:"+std::to_string(Index(ed))+":"+std::to_string(count));if(e.onAllocate)e.onAllocate();return ed->pvPrivateData;
    }
    static void CheckGuards(edict_t* ed)
    { const auto& b=self->privateBlocks.at(ed);Check(std::all_of(b.bytes.get(),b.bytes.get()+64,[](auto c){return c==0xa5;})&&std::all_of(b.bytes.get()+64+b.size,b.bytes.get()+128+b.size,[](auto c){return c==0xa5;}),"private allocation guards unchanged"); }
    static void FreePrivate(edict_t* ed)
    {
        if(!ed->pvPrivateData)return;CheckGuards(ed);self->newApi.pfnOnFreeEntPrivateData(ed);CheckGuards(ed);
        auto* base=static_cast<CBaseEntity*>(ed->pvPrivateData);Check(base->m_pEntity==nullptr,"real free callback deletes API wrapper");
        const auto callback=self->afterFreeCallback;if(callback)callback(ed);
        ed->pvPrivateData=nullptr;self->privateBlocks.erase(ed);++self->releases;
    }
    static void Remove(edict_t* ed) { FreePrivate(ed);ed->free=TRUE;++ed->serialnumber; }
    static edict_t* Create(int classname)
    {
        auto& e=*self;auto* ed=e.Reserve(e.nextEntity++);ed->v.classname=classname;
        const char* name=STRING(classname);e.Record(std::string("create:")+name);
        using Export=void(*)(entvars_t*);auto fn=reinterpret_cast<Export>(GetProcAddress(GetModuleHandleW(nullptr),name));
        if(!fn){++e.unknownExports;throw std::runtime_error(std::string("missing real entity export: ")+name);}fn(&ed->v);return ed;
    }
    static edict_t* Find(edict_t* start,const char* field,const char* value)
    {
        edict_t* result=&self->edicts[0];
        for(int i=start?Index(start)+1:0;i<self->nextEntity;++i)
        {auto* ed=At(i);if(ed->free)continue;const int str=std::strcmp(field,"classname")==0?ed->v.classname:ed->v.targetname;if(std::strcmp(STRING(str),value)==0){result=ed;break;}}
        // Compute the result before the callback: the test may revoke and
        // protect its storage. Returning its address must not read it again.
        const auto callback=self->onFind;if(callback)callback(result,field,value);return result;
    }
    static int Model(const char* name)
    {auto& e=*self;auto [it,added]=e.models.try_emplace(name,static_cast<int>(e.models.size())+1);e.Record(std::string("model:")+name);if(e.onModel)e.onModel();return it->second;}
    static int Message(const char* name,int)
    {auto& e=*self;e.Record(std::string("register-message:")+name);return e.messages.try_emplace(name,64+static_cast<int>(e.messages.size())).first->second;}
    static unsigned char* Load(const char* path,int* size)
    {auto& e=*self;e.Record(std::string("load:")+path);auto found=e.files.find(path);if(found==e.files.end()){*size=0;return nullptr;}*size=static_cast<int>(found->second.size());auto p=std::make_unique<unsigned char[]>(*size+1);std::memcpy(p.get(),found->second.data(),*size);p[*size]=0;auto* raw=p.get();e.buffers.emplace(raw,std::move(p));return raw;}
    static void Free(void* p){Require(self->buffers.erase(p)==1,"one release per engine file buffer");}
    static void Alert(ALERT_TYPE,const char* format,...)
    {char text[2048];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);self->Record(std::string("alert:")+text);}
    void Clear()
    {
        if(g_bServerActive)api.pfnServerDeactivate();
        FreeGameRules(&g_pGameRules);Require(!g_pGameRules,"real FreeGameRules nulls owner");
        for(auto& ed:edicts)if(ed.pvPrivateData)FreePrivate(&ed);
        // This fixture engine has now returned from every actual SDK free
        // callback and released its private blocks, matching a drain boundary.
        CSOL_CollectRetiredScenarioRules();
        EmptyEntityHashTable();for(auto& ed:edicts){const int serial=ed.serialnumber+1;ed={};ed.free=TRUE;ed.serialnumber=serial;ed.v.pContainingEntity=&ed;}
        edicts[0].free=FALSE;nextEntity=8;
        Check(buffers.empty()&&privateBlocks.empty(),"all owned engine allocations released");
    }
    ~Engine() { if(self){try{Clear();}catch(const std::exception& e){std::cerr<<"cleanup: "<<e.what()<<'\n';++failures;}self=nullptr;} }
};
}
