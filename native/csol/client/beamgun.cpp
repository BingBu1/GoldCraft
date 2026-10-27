// SPDX-License-Identifier: GPL-3.0-or-later
#include "beamgun.hpp"
#include "weapon_inventory.hpp"
#include "camera.hpp"
#include "../beamgun_prediction.hpp"
#include "../beamgun_effects.hpp"
#include "../beamgun_beam.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <triangleapi.h>
#include <in_buttons.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <ostream>
#include <limits>
#include <climits>

namespace csol::client
{
namespace
{
WeaponCurrent current;
BeamWeaponState predicted;
bool havePrediction{};
int presentedWeapon{}, presentedModel{};
int predictionCalls{}, draws{}, effectMessages{}, rejectedEffects{};
int predictedReserve{};
struct Sprite { int handle{}; wrect_t rect{}; };
Sprite digits[10], ammo, energy;
SCREENINFO screen{};
std::array<int, 33> jumping{};
int effectGeneration = 1;
std::array<bool,33> tracerSide{};
struct Tracer
{
    model_t* sprite{};
    std::array<float,3> start{}, velocity{};
    double birth{}, life{};
};
std::array<Tracer,256> tracers;
struct ChargedBeam
{
    model_t* sprite{};
    BeamPoint start{},end{};
    float birth{},die{},width{},frequency{};
    double lastFrame{-1};
};
std::array<ChargedBeam,64> chargedBeams;
struct OwnedEffect { TEMPENTITY* temp{};model_t* model{};int generation{};float birth{};bool follow{}; };
std::array<OwnedEffect,256> ownedEffects;
int ownedGeneration{};

void UpdateEffect(TEMPENTITY* entity, float, float now);
bool Owns(const OwnedEffect& effect, double now)
{
    return effect.temp && effect.temp->callback==UpdateEffect && effect.temp->entity.model==effect.model &&
        effect.temp->entity.curstate.iuser2==effect.generation && effect.temp->entity.curstate.animtime==effect.birth &&
        !(effect.temp->flags&FTENT_NOMODEL) && now>=effect.birth && now<effect.temp->die;
}
void ForgetEffects()
{
    for(auto& effect:ownedEffects)if(effect.temp){ForgetSceneEntity(&effect.temp->entity);effect={};}
}

void Follow(TEMPENTITY* entity, float, float now)
{
    const int owner = entity->clientIndex;
    auto* player = owner > 0 && owner <= 32 ? engine.GetEntityByIndex(owner) : nullptr;
    if (!player || !player->model || (player->curstate.effects & EF_NODRAW) ||
        (entity->entity.curstate.iuser1 && (jumping[owner] != entity->entity.curstate.iuser1 ||
            !player->curstate.weaponmodel || (player->curstate.solid == 0 && owner != view.viewentity))))
    { entity->die = now; entity->flags |= FTENT_NOMODEL; return; }
    std::copy_n(player->origin, 3, entity->entity.origin);
    std::copy_n(player->angles, 3, entity->entity.angles);
}
void UpdateEffect(TEMPENTITY* entity, float dt, float now)
{
    for(auto& effect:ownedEffects)if(effect.temp==entity)
    {
        if(!Owns(effect,now)){ForgetSceneEntity(&entity->entity);effect={};return;}
        if(effect.follow)Follow(entity,dt,now);
        return;
    }
    entity->die=now;entity->flags|=FTENT_NOMODEL;
}
int ModelIndex(const char* name)
{
    int index = 0;
    return engine.CL_LoadModel && engine.CL_LoadModel(name, &index) ? index : 0;
}
void EffectModel(const BeamEffect& effect, const char* suffix, float life, bool follow, bool atImpact=false, float scale=1)
{
    char name[128]; std::snprintf(name, sizeof(name), "models/ef_%s_%s.mdl", effect.weapon == BeamgunLEId ? "beamgunle" : "beamgun", suffix);
    const int index = ModelIndex(name);
    if (!index || !engine.pEfxAPI || !engine.pEfxAPI->R_TempModel) return;
    float zero[3]{}, angles[3]{};
    auto origin = atImpact ? effect.end : effect.origin;
    if (!atImpact) if (auto* player = engine.GetEntityByIndex(effect.owner)) std::copy_n(player->angles, 3, angles);
    auto* entity = engine.pEfxAPI->R_TempModel(origin.data(), zero, angles, life, index, 0);
    if (!entity) return;
    entity->flags = FTENT_PERSIST | FTENT_CLIENTCUSTOM;
    entity->entity.curstate.framerate = 1;
    entity->entity.curstate.renderamt = 255;
    entity->entity.curstate.scale = scale;
    entity->entity.curstate.animtime = engine.GetClientTime();
    entity->entity.curstate.rendermode = kRenderTransTexture;
    entity->callback = UpdateEffect;
    const double now=engine.GetClientTime();
    for(auto& effect:ownedEffects)if(effect.temp==entity || !Owns(effect,now))
    {if(effect.temp)ForgetSceneEntity(&effect.temp->entity);effect={};}
    auto slot=std::find_if(ownedEffects.begin(),ownedEffects.end(),[](const OwnedEffect& e){return !e.temp;});
    if(slot==ownedEffects.end()){entity->die=float(now);entity->flags|=FTENT_NOMODEL;return;}
    ownedGeneration=ownedGeneration==INT_MAX?1:ownedGeneration+1;
    entity->entity.curstate.iuser2=ownedGeneration;
    *slot={entity,entity->entity.model,ownedGeneration,entity->entity.curstate.animtime,follow};
    if (follow)
    {
        entity->clientIndex = effect.owner;
        entity->flags |= FTENT_CLIENTCUSTOM;
        entity->entity.curstate.iuser1 = effect.mode == 4 && effect.phase == 1 ? jumping[effect.owner] : 0;
    }
}
void Impact(const BeamEffect& effect, const char* suffix, float scale, float life=.83f, int renderMode=kRenderTransAdd)
{
    char path[128]; std::snprintf(path, sizeof(path), "sprites/ef_%s_%s.spr", effect.weapon == BeamgunLEId ? "beamgunle" : "beamgun", suffix);
    const int index = ModelIndex(path);
    if (!index || !engine.pEfxAPI || !engine.pEfxAPI->R_DefaultSprite) return;
    auto end = effect.end;
    if (auto* sprite = engine.pEfxAPI->R_DefaultSprite(end.data(), index, 30))
    {
        sprite->flags = FTENT_PERSIST | FTENT_SPRANIMATE;
        sprite->entity.curstate.scale = scale; sprite->entity.curstate.rendermode = renderMode;
        sprite->entity.curstate.renderamt = 255;
        sprite->die = engine.GetClientTime() + life;
    }
}
void Beam(const BeamEffect& effect, const std::array<float, 3>& start, const std::array<float, 3>& end, float width, float life)
{
    char path[128]; std::snprintf(path, sizeof(path), "sprites/ef_%s_charge_shootA.spr", effect.weapon == BeamgunLEId ? "beamgunle" : "beamgun");
    int index{};
    auto* sprite=engine.CL_LoadModel?engine.CL_LoadModel(path,&index):nullptr;
    if(!sprite || sprite->type!=mod_sprite || sprite->numframes<1)return;
    auto a = start, b = end;
    // Original callback reads attachment0 (+0xc94) of its selected render
    // entity. Local first-person uses the viewmodel, remote uses the player.
    auto* local=engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    auto* entity=local && local->index==effect.owner && !CameraIsThirdPerson() && engine.GetViewModel ?
        engine.GetViewModel() : engine.GetEntityByIndex(effect.owner);
    if(entity && entity->model && std::any_of(std::begin(entity->attachment[0]),std::end(entity->attachment[0]),[](float v){return v!=0;}))
        std::copy_n(entity->attachment[0],3,a.begin());
    const double now=engine.GetClientTime();
    auto slot=std::find_if(chargedBeams.begin(),chargedBeams.end(),[&](const ChargedBeam& beam){
        return !beam.sprite || now<beam.birth || now>=beam.die;
    });
    if(slot!=chargedBeams.end())*slot={sprite,a,b,float(now),float(now+life),width};
}
void Sound(const BeamEffect& effect, const char* name, const std::array<float,3>& point, int channel=0, int flags=0, bool randomPitch=false)
{
    auto origin=point;
    if(engine.pEventAPI && engine.pEventAPI->EV_PlaySound)
        engine.pEventAPI->EV_PlaySound(effect.owner,origin.data(),channel,name,1,.52f,flags,
            randomPitch && engine.pfnRandomLong ? engine.pfnRandomLong(94,109) : 100);
    else if(engine.pfnPlaySoundByNameAtLocation)
        engine.pfnPlaySoundByNameAtLocation(const_cast<char*>(name),1,origin.data());
}
void WingmanTracers(const BeamEffect& effect)
{
    auto* owner=engine.GetEntityByIndex(effect.owner);
    if(!owner || !engine.CL_LoadModel)return;
    char path[128];std::snprintf(path,sizeof(path),"sprites/ef_%s_wingman.spr",effect.weapon==BeamgunLEId?"beamgunle":"beamgun");
    int index{};auto* sprite=engine.CL_LoadModel(path,&index);if(!sprite)return;
    const double now=engine.GetClientTime();
    const float yaw=owner->angles[1]*0.01745329252f;
    for(int i=0;i<effect.count;++i)
    {
        auto* target=engine.GetEntityByIndex(effect.targets[i]);if(!target || !target->model)continue;
        auto start=effect.origin;
        const float side=tracerSide[effect.owner]?-10.f:10.f;
        tracerSide[effect.owner]=!tracerSide[effect.owner];
        start[0]-=std::sin(yaw)*side;start[1]+=std::cos(yaw)*side;start[2]+=40;
        std::array<float,3> velocity{};float distance=0;
        for(int n=0;n<3;++n){velocity[n]=target->origin[n]-start[n];distance+=velocity[n]*velocity[n];}
        distance=std::sqrt(distance);if(distance<.01f)continue;
        for(auto& v:velocity)v*=2000/distance;
        auto slot=std::find_if(tracers.begin(),tracers.end(),[&](const Tracer& t){return !t.sprite || now<t.birth || now>=t.birth+t.life;});
        if(slot!=tracers.end())*slot={sprite,start,velocity,now,distance/2000};
        if(i<2)Sound(effect,"weapons/beamgun-5.wav",start,0,0x800,true);
    }
}
int ReceiveEffect(const char*, int size, void* data)
{
    BeamEffect effect;
    if (!data || size != BeamEffectBytes || !DecodeBeamEffect({static_cast<const std::byte*>(data), std::size_t(size)}, effect))
    { ++rejectedEffects; return 0; }
    ++effectMessages;
    const char* sound = nullptr;
    if (effect.mode == 0)
    {
        sound = "weapons/beamgun-1.wav";
        if (effect.phase == 1) { Impact(effect, "shootA_hit", 1.2f); Sound(effect,"weapons/beamgun-1_exp1.wav",effect.end); }
        else if (effect.phase == 4) { Beam(effect, effect.origin, effect.end, 40, 0.5f); Impact(effect, "chargeshootAhit", 1.8f); Sound(effect,"weapons/beamgun-1_exp2.wav",effect.end); }
    }
    else if (effect.mode == 1) { EffectModel(effect, "shootB", 0.2f, true); sound = "weapons/beamgun-3.wav"; }
    else if (effect.mode == 2)
    {
        EffectModel(effect, "shootB2_landx", 1.03f, false);
        if(effect.phase==1)
        {
            EffectModel(effect,"shootB2_exp",.4f,false,true,.8f);
            Impact(effect,"shootB2_exp2",.8f,.4f,kRenderTransAlpha);
            Sound(effect,"weapons/beamgun-2.wav",effect.end,0,0,true);
        }
    }
    else if (effect.mode == 3)
    { Beam(effect, effect.origin, effect.end, 40, 0.5f); Impact(effect, "chargeshootAhit", 2.7f); sound = "weapons/beamgun-4.wav"; Sound(effect,"weapons/beamgun-4_exp.wav",effect.end); }
    else if (effect.mode == 4)
    {
        jumping[effect.owner] = effect.phase == 1 ? effectGeneration++ : 0;
        if (effect.phase == 1) { EffectModel(effect, "jump", std::numeric_limits<float>::max(), true); sound = "weapons/beamgun_jump.wav"; }
        else if (effect.phase == 3) { EffectModel(effect, "jump_land", 1.37f, true); sound = "weapons/beamgun_jump_end.wav"; }
    }
    else if (effect.mode == 5)
    {
        WingmanTracers(effect);
    }
    else if (effect.mode == 6) { EffectModel(effect, "shootB2_land", 1.37f, true); }
    if (sound) Sound(effect,sound,effect.origin,effect.mode==0?1:0);
    return 1;
}
void LoadSprites()
{
    screen.iSize = sizeof(screen); engine.pfnGetScreenInfo(&screen);
    constexpr int left[]{0,23,47,70,95,119,144,169,192,216};
    const int font = engine.pfnSPR_Load("sprites/csol/640hud7.spr");
    for (int i = 0; i < 10; ++i) digits[i] = {font, {left[i],left[i]+20,0,25}};
    // The native HUD supplies its 320-resolution font on small viewports.
    // Keep the CSO atlas fallback if that legacy font is absent.
    if (screen.iWidth < 640)
    {
        int count = 0;
        auto* list = engine.pfnSPR_GetList("sprites/hud.txt", &count);
        Sprite lowResolution[10]{};
        for (int i = 0; list && i < count; ++i) if (list[i].iRes == 320)
            for (int digit = 0; digit < 10; ++digit)
            {
                char name[24]; std::snprintf(name, sizeof(name), "number_%d", digit);
                if (std::strcmp(list[i].szName, name)) continue;
                char path[160]; std::snprintf(path, sizeof(path), "sprites/%s.spr", list[i].szSprite);
                lowResolution[digit] = {engine.pfnSPR_Load(path), list[i].rc};
            }
        if (std::all_of(std::begin(lowResolution), std::end(lowResolution), [](const Sprite& digit){return digit.handle != 0;}))
            std::copy(std::begin(lowResolution), std::end(lowResolution), std::begin(digits));
    }
    ammo = {engine.pfnSPR_Load("sprites/csol/640hud266.spr"), {1,25,231,255}};
    energy = {engine.pfnSPR_Load("sprites/csol/640hud224.spr"), {49,73,231,255}};
}
void Number(int value, int x, int y, bool blue = false)
{
    char text[16]; std::snprintf(text, sizeof(text), "%d", std::clamp(value, 0, 9999));
    const int width = digits[0].rect.right - digits[0].rect.left;
    x += std::max(0, 3 - int(std::strlen(text))) * width;
    for (const char* digit = text; *digit; ++digit)
    {
        const auto& sprite = digits[*digit - '0'];
        engine.pfnSPR_Set(sprite.handle, blue ? 114 : 255, blue ? 197 : 160, blue ? 255 : 0);
        engine.pfnSPR_DrawAdditive(0, x, y, &sprite.rect); x += width;
    }
}
}

void InitializeBeamgun() { engine.pfnHookUserMsg(BeamEffectMessage, ReceiveEffect); }
void ResetBeamgun() { ForgetEffects();current = {}; predicted = {}; predictedReserve = 0; havePrediction = false; jumping = {}; presentedWeapon = presentedModel = 0; tracerSide = {}; tracers = {}; chargedBeams = {}; }
void BeamgunVideoInit() { ForgetEffects();ResetBeamScope(); for (auto& digit : digits) digit = {}; ammo = energy = {}; screen = {}; jumping = {}; tracers = {}; tracerSide = {}; chargedBeams = {}; }
unsigned BeamEffectIdentity(const cl_entity_t* entity)
{
    const double now=engine.GetClientTime();
    for(const auto& effect:ownedEffects)if(effect.temp && &effect.temp->entity==entity && Owns(effect,now))return effect.generation;
    return 0;
}
void PruneBeamEffects()
{
    const double now=engine.GetClientTime();
    for(auto& effect:ownedEffects)if(effect.temp && !Owns(effect,now))
    {ForgetSceneEntity(&effect.temp->entity);effect={};}
}
void ReceiveBeamgunState(const WeaponCurrent& value)
{
    if (current.id != value.id || !value.state) presentedWeapon = presentedModel = 0;
    current = value; havePrediction = false;
}

int BeamgunViewSkin(const cl_entity_t* entity)
{
    if (!entity || !entity->model || !current.state || !BeamWeaponId(current.id)) return -1;
    const char* expected = current.id == BeamgunLEId ? "models/v_beamgunle.mdl" : "models/v_beamgun.mdl";
    if (_stricmp(entity->model->name, expected)) return -1;
    const int value = havePrediction && engine.pfnGetCvarFloat("cl_lw") != 0 ? predicted.energy : current.secondary;
    return value >= 100 ? 1 : 0;
}

void DrawBeamgunTracers()
{
    auto* api=engine.pTriAPI;
    if(!api)return;
    const double now=engine.GetClientTime();
    bool drawing=false;
    // Read the current render view once. Renderer supplies these cached
    // matrices through TriAPI; no synchronous GL matrix query is needed.
    if(api->GetMatrix && std::any_of(chargedBeams.begin(),chargedBeams.end(),[](const auto& b){return b.sprite!=nullptr;}))
    {
        float world[16]{},projection[16]{},matrix[16]{};
        api->GetMatrix(GL_MODELVIEW_MATRIX,world);api->GetMatrix(GL_PROJECTION_MATRIX,projection);
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)for(int k=0;k<4;++k)
            matrix[column*4+row]+=world[column*4+k]*projection[k*4+row];
        const BeamPoint right{world[0],world[4],world[8]},up{world[1],world[5],world[9]};
        const auto project=[&](const BeamPoint& point){
            std::array<float,2> screen{};
            const float w=point[0]*matrix[3]+point[1]*matrix[7]+point[2]*matrix[11]+matrix[15];
            const float reciprocal=w!=0 ? float(1.0/double(w)) : 1.f;
            for(int n=0;n<2;++n){screen[n]=point[0]*matrix[n]+point[1]*matrix[4+n]+point[2]*matrix[8+n]+matrix[12+n];screen[n]*=reciprocal;}
            return screen;
        };
        for(auto& beam:chargedBeams)
        {
            if(!beam.sprite)continue;
            if(now<beam.birth || now>=beam.die){beam={};continue;}
            // The engine advances frequency once per render frame. Secondary
            // views must use the same age, rather than consume the fade again.
            if(beam.lastFrame!=now){beam.frequency+=std::max(0.f,view.frametime);beam.lastFrame=now;}
            const float duration=float(double(beam.die)-now+beam.frequency);
            const float fade=duration>0 ? 1.f-beam.frequency/duration : 0.f;
            // Original 0x200 starts the 40 FPS animation at creation time.
            const int frame=int((now-double(beam.birth))*40)%beam.sprite->numframes;
            if(!api->SpriteTexture(beam.sprite,frame))continue;
            api->RenderMode(kRenderTransAdd);api->CullFace(TRI_NONE);api->Begin(TRI_QUADS);
            EmitBeamStrip(beam.start,beam.end,beam.width,right,up,project,[&](const BeamVertex& vertex){
                // CSO's fixed-function GL clamps primary color before texture
                // modulation; the host's shader-based TriAPI keeps HDR floats.
                api->Color4f(1,1,1,std::clamp(20*fade*vertex.shade,0.f,1.f));
                api->TexCoord2f(vertex.u,vertex.v);
                api->Vertex3f(vertex.position[0],vertex.position[1],vertex.position[2]);
            });
            api->End();drawing=true;
        }
    }
    for(auto& tracer:tracers)
    {
        if(!tracer.sprite)continue;
        const double age=now-tracer.birth,remaining=tracer.life-age;
        if(age<0 || remaining<=0){tracer={};continue;}
        std::array<float,3> start{},end{},screenStart{},screenEnd{};
        // Original type12: length=min(remaining,0.1)*ramp(0.4), then
        // trim the first25%. Width table5 * allocator width multiplier1.
        const float length=float(std::min(remaining,.1)*.4);
        for(int n=0;n<3;++n){start[n]=tracer.start[n]+tracer.velocity[n]*float(age);end[n]=start[n]+tracer.velocity[n]*length;start[n]+=(end[n]-start[n])*.25f;}
        if(api->WorldToScreen(start.data(),screenStart.data()) || api->WorldToScreen(end.data(),screenEnd.data()))continue;
        const float dx=screenEnd[0]-screenStart[0],dy=screenEnd[1]-screenStart[1];
        const float distance=std::hypot(dx,dy);if(distance<1e-6f)continue;
        std::array<float,3> normal{};
        for(int n=0;n<3;++n)normal[n]=5*(view.up[n]*dx-view.right[n]*dy)/distance;
        if(!api->SpriteTexture(tracer.sprite,0))continue;
        api->RenderMode(kRenderTransAdd);api->CullFace(TRI_NONE);api->Color4f(1,1,1,1);
        api->Begin(TRI_QUADS);
        for(int c=0;c<4;++c)
        {
            const auto& center=c==1||c==2?end:start;
            const float sign=c<2?1.f:-1.f;
            api->TexCoord2f(c==1||c==2?1.f:0.f,c>=2?1.f:0.f);
            api->Vertex3f(center[0]+sign*normal[0],center[1]+sign*normal[1],center[2]+sign*normal[2]);
        }
        api->End();drawing=true;
    }
    if(drawing){api->RenderMode(kRenderNormal);api->CullFace(TRI_FRONT);}
}

bool PredictBeamgun(local_state_t* from, local_state_t* to, usercmd_t* cmd, int runFunctions, double time, unsigned seed)
{
    const int id = from->client.m_iId;
    if (!BeamWeaponId(id) || from->weapondata[ActiveWeaponSlot].m_iId != id || !FindWeaponDefinition(id))
    {
        if (runFunctions) { presentedWeapon = presentedModel = 0; havePrediction = false; }
        return false;
    }
    ++predictionCalls;
    const auto source = from->client;
    auto data = from->weapondata[ActiveWeaponSlot];
    auto state = ReadBeamPrediction(data);
    from->client.m_iId = 0;
    downstream.HUD_PostRunCmd(from, to, cmd, runFunctions, time, seed);
    from->client.m_iId = id; to->client.m_iId = id;
    to->client.viewmodel = source.viewmodel; to->client.weaponanim = source.weaponanim;
    to->client.m_flNextAttack = source.m_flNextAttack; to->client.iuser3 = source.iuser3;
    std::copy(std::begin(source.vuser4), std::end(source.vuser4), std::begin(to->client.vuser4));
    to->playerstate.oldbuttons = cmd->buttons;
    bool animated = false;
    const auto apply = [&](const BeamAction& action) {
        if (action.animation >= 0)
        {
            to->client.weaponanim = action.animation;
            animated = true;
        }
        // Movement remains server-authoritative; PM_Move has already run.
        // Predicting velocity here would be applied one command too late.
    };
    const bool alive = source.health > 0 && !source.deadflag;
    if (alive && source.m_flNextAttack <= 0)
    {
        if (state.reloading)
        {
            const int added = std::min(80 - state.clip, std::max(0, int(to->client.vuser4[1])));
            state.clip += added; to->client.vuser4[1] -= added; state.reloading = false;
        }
        const bool canShoot = (source.iuser3 & 1) && !(source.iuser3 & 2);
        if (canShoot)
        {
            const unsigned pressed = (from->playerstate.oldbuttons ^ cmd->buttons) & cmd->buttons;
            const auto full = state.FullCharge(pressed);
            if (full.attack != BeamAttack::None) apply(full);
            else
            {
                apply(state.DelayedAttack()); apply(state.ExpireCharge());
                if (state.dive != BeamDive::None)
                    apply(state.UpdateDive((source.flags & FL_ONGROUND) != 0, from->playerstate.movetype));
                else if ((cmd->buttons & IN_ATTACK2) && state.nextSecondary <= 0) apply(state.Secondary());
                else if ((cmd->buttons & IN_ATTACK) && state.nextPrimary <= 0) apply(state.Primary(true));
                else if (((cmd->buttons & IN_RELOAD) || (!state.clip && !(cmd->buttons & (IN_ATTACK|IN_ATTACK2)))) &&
                    state.clip < 80 && to->client.vuser4[1] > 0 && state.nextPrimary < 0 && state.pending == BeamPending::None)
                {
                    const int animation = state.ReloadAnimation(); state.reloading = true;
                    to->client.m_flNextAttack = 2.3f; apply(state.ReloadStarted(true));
                    to->client.weaponanim = animation;
                    animated = true;
                }
                else apply(state.Idle());
            }
        }
    }
    if (runFunctions && alive)
    {
        auto* model = engine.GetViewModel ? engine.GetViewModel() : nullptr;
        const int animation = to->client.weaponanim;
        // DefaultDeploy may skip SVC_WEAPONANIM for a predicting client. Stock
        // prediction cannot deploy our extended ID; reconcile its authoritative
        // sequence once on entry and after correction, never on an ammo update.
        if (animation >= BeamIdle && animation <= BeamChargeDraw &&
            (animated || presentedWeapon != id || presentedModel != source.viewmodel ||
                (model && model->curstate.sequence != animation)))
            engine.pfnWeaponAnim(animation, model ? model->curstate.body : 0);
        presentedWeapon = id; presentedModel = source.viewmodel;
    }
    else if (runFunctions) presentedWeapon = presentedModel = 0;
    const float dt = cmd->msec / 1000.0f;
    state.Decrement(dt);
    state.nextPrimary = std::max(-0.001f, state.nextPrimary - dt);
    state.nextSecondary = std::max(-0.001f, state.nextSecondary - dt);
    state.idleTime = std::max(-0.001f, state.idleTime - dt);
    to->client.m_flNextAttack = std::max(-0.001f, to->client.m_flNextAttack - dt);
    WriteBeamPrediction(state, data); to->weapondata[ActiveWeaponSlot] = data;
    if (runFunctions) { predicted = state; predictedReserve = int(to->client.vuser4[1]); havePrediction = true; }
    return true;
}

bool BeamgunSecondaryAmmoVisible(int hidden, int intermission)
{
    if (!BeamWeaponId(current.id) || !current.state || (hidden & 5) || intermission || view.health <= 0 || view.spectator ||
        engine.pfnGetCvarFloat("hud_draw") == 0) return false;
    return (havePrediction && engine.pfnGetCvarFloat("cl_lw") != 0 ? predicted.energy : current.secondary) > 0;
}
void DrawBeamgunHUD(int hidden, int intermission)
{
    if (!BeamWeaponId(current.id) || !current.state || (hidden & 5) || intermission || view.health <= 0 || view.spectator ||
        engine.pfnGetCvarFloat("hud_draw") == 0) return;
    if (!digits[0].handle) LoadSprites();
    if (!(hidden & 64)) DrawBeamScope(current.id, screen.iWidth, screen.iHeight,
        havePrediction && predicted.Charged() ? predicted.chargeTime / (predicted.charge == BeamCharge::Beta ? 7.f : 2.5f) : 0);
    if (!digits[0].handle || !ammo.handle || !energy.handle) return;
    const bool usePrediction = havePrediction && engine.pfnGetCvarFloat("cl_lw") != 0;
    const int clip = usePrediction ? predicted.clip : current.clip;
    const int sync = usePrediction ? predicted.energy : current.secondary;
    const int width = digits[0].rect.right - digits[0].rect.left;
    const int height = digits[0].rect.bottom - digits[0].rect.top;
    const int iconWidth = ammo.rect.right - ammo.rect.left;
    const int y = screen.iHeight - height - height / 2;
    const int x = screen.iWidth - 8 * width - iconWidth;
    const int dividerX = x + 3 * width + width / 2;
    const int dividerWidth = width / 10;
    const int reserveX = dividerX + dividerWidth + width / 2;
    Number(clip, x, y); Number(usePrediction ? predictedReserve : current.reserve, reserveX, y);
    engine.pfnFillRGBA(dividerX, y, dividerWidth, height, 255, 160, 0, 255);
    engine.pfnSPR_Set(ammo.handle, 255, 160, 0);
    engine.pfnSPR_DrawAdditive(0, reserveX + 3 * width, y - (ammo.rect.bottom - ammo.rect.top) / 8, &ammo.rect);
    if (sync > 0)
    {
        const int energyX = screen.iWidth - 4 * width - (energy.rect.right - energy.rect.left);
        const int energyY = y - height - height / 4;
        Number(sync, energyX, energyY, true);
        engine.pfnSPR_Set(energy.handle, 114, 197, 255);
        engine.pfnSPR_DrawAdditive(0, energyX + 3 * width, energyY - (energy.rect.bottom - energy.rect.top) / 8, &energy.rect);
    }
    ++draws;
}
void BeamgunDiagnostics(std::ostream& output)
{
    output << ",\"beamgun\":{\"active\":" << current.id << ",\"energy\":" << current.secondary
        << ",\"predictionCalls\":" << predictionCalls << ",\"draws\":" << draws
        << ",\"charge\":" << int(predicted.charge) << ",\"chargeTime\":" << predicted.chargeTime
        << ",\"effects\":" << effectMessages << ",\"rejected\":" << rejectedEffects << '}';
}
}
