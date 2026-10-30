// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.hpp"
#include "characters.hpp"
#include "buy_menu.hpp"
#include "player_animation.hpp"
#include "stock_hands.hpp"
#include "weapons.hpp"
#include "beamgun.hpp"
#include "cannon.hpp"
#include "scenario_smoke.hpp"
#include "camera.hpp"
#include "projectiles.hpp"
#include "gl_state.hpp"
#include "../events.hpp"
#include "../parts.hpp"
#include "../presentation.hpp"
#include <IMetaRenderer.h>
#include <IMetaRendererPostScene.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <unordered_set>

namespace csol::client
{
namespace
{
IMetaRenderer* renderer{};
IMetaRendererPostScene* postRenderer{};
const MetaRendererPostSceneFrame* postFrame{};
void DrawOwnedWorld(const MetaRendererPostSceneFrame& frame) noexcept;
class WorldCallbacks final : public IMetaRendererPostSceneCallbacks
{
public:void DrawPostScene(const MetaRendererPostSceneFrame& frame) noexcept override { DrawOwnedWorld(frame); }
} worldCallbacks;
struct WorldDraw
{
    cl_entity_t* entity{};unsigned identity{};int frame{};bool events{};
    FBO_Container_t* target{};
    std::array<float,16> world{},projection{};
};
std::array<WorldDraw,256> worldDraws;
HGLRC graphicsContext{};
cvar_t* colorSettings[6]{};
GLuint program{},quadVAO{},quadVBO{};
struct Uniforms { GLint world,projection,bones,uv,color,eye,light,lightColor,ambient,shade,fullbright,masked,environment,environmentAdd,gamma,lighting,flatshade; } uniform{};
struct State
{
    Asset* asset{};model_t* model{};int frame=-1,sequence=-1;double time=-1,start{},reviveTime{},action{};
    Bones bones{};std::vector<std::array<float,3>> attachments;
    std::array<float,3> lightDirection{},lightColor{};
    int ambient{},shade{},eventFrame=-1,eventRequestedFrame=-1;
    std::vector<double> sprites;std::vector<FadeState> fades;std::vector<FadeResult> results;
    BodyEffects body;
};
struct Part
{
    model_t* model{};Asset* asset{};PartsDefinition definition;
    std::vector<int> boneMap;
    State state;
    int sequence{},skin{},body{},ammo{},maximum=1;
    double start{};
    bool expanded{};
};
struct PartSet { model_t* primary{};std::array<Part,2> slots; } parts;
struct Effect
{
    const cl_entity_t* owner{};model_t* ownerModel{};model_t* sprite{};SpriteEvent rule;
    int sequence{},first{};double birth{};float angle{};std::array<float,3> position{};
};
std::unordered_map<const cl_entity_t*,State> states;
std::vector<Effect> effects;
std::unordered_set<const model_t*> failures;
struct WorldWeapon
{
    const cl_entity_t* source{};
    model_t* sourceModel{};
    model_t* weaponModel{};
    cl_entity_t entity{};
    unsigned generation{};
    int frame=-1;
    bool attached{};
};
std::array<WorldWeapon,128> worldWeapons;
unsigned worldWeaponGeneration{};
bool HostDrawsLocalHeldWeapon(const cl_entity_t* source)
{
    // Renderer supplies a clipped local body in first person. Post-scene
    // meshes do not carry its bone/near-plane clipping; preserve that path.
    return engine.GetLocalPlayer && source==engine.GetLocalPlayer() && !CameraIsThirdPerson();
}
WorldWeapon* WorldWeaponFor(const cl_entity_t* entity)
{
    for(auto& weapon:worldWeapons)if(&weapon.entity==entity && weapon.source)return &weapon;
    return nullptr;
}
unsigned WorldIdentity(const cl_entity_t* entity)
{
    if(auto* weapon=WorldWeaponFor(entity))
    {
        if(weapon->source->model!=weapon->sourceModel || (weapon->source->curstate.effects&EF_NODRAW))return 0;
        if(!weapon->attached && weapon->source->player && HostDrawsLocalHeldWeapon(weapon->source))return 0;
        if(weapon->attached && (HostDrawsLocalHeldWeapon(weapon->source) ||
            studio.GetModelByIndex(weapon->source->curstate.weaponmodel)!=weapon->weaponModel))return 0;
        return weapon->generation;
    }
    return BeamEffectIdentity(entity);
}
int draws{},eventCount{};
int partMessages{},viewMessages{},rejectedMessages{};
std::array<PendingPresentation<PartPresentation>,2> pendingParts;
PendingPresentation<ViewPresentation> pendingView;
std::optional<ViewPresentation> viewState;
model_t* presentationModel{};
std::filesystem::path diagnosticPath;
double nextDiagnostic{};
cvar_t* enabled{};

bool EnsureGL()
{
    const auto current=wglGetCurrentContext();if(!current)return false;
    if(graphicsContext && graphicsContext!=current)ClearScene();
    graphicsContext=current;
    if(!glewReady){if(!wglGetCurrentContext())return false;glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK || !GLEW_VERSION_3_3)return false;glewReady=true;}
    if(!renderer)
    {
        auto module=api->GetPluginModuleHandleByBaseFileName("Renderer");
        if(!module)module=api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");
        auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress((HMODULE)module,"CreateInterface")):nullptr;
        if(factory)
        {
            renderer=static_cast<IMetaRenderer*>(factory(METARENDERER_INTERFACE_VERSION,nullptr));
            postRenderer=static_cast<IMetaRendererPostScene*>(factory(METARENDERER_POST_SCENE_INTERFACE_VERSION,nullptr));
            if(postRenderer && !postRenderer->RegisterPostSceneCallbacks(&worldCallbacks))postRenderer=nullptr;
        }
    }
    return true;
}
GLuint Compile(GLenum type,const char* text)
{
    const auto shader=glCreateShader(type);glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);GLint ok{};glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096]{};glGetShaderInfoLog(shader,sizeof(log),nullptr,log);glDeleteShader(shader);throw std::runtime_error(log);}return shader;
}
void Program()
{
    if(program)return;
    const char* vs=R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in ivec2 bone;
uniform vec4 bones[384];uniform mat4 world,projection;uniform vec4 uvTransform;
out vec3 worldPosition,worldNormal;out vec2 uv;
void main(){int p=bone.x*3,n=bone.y*3;vec4 v=vec4(position,1);
worldPosition=vec3(dot(v,bones[p]),dot(v,bones[p+1]),dot(v,bones[p+2]));
worldNormal=normalize(vec3(dot(normal,bones[n].xyz),dot(normal,bones[n+1].xyz),dot(normal,bones[n+2].xyz)));
uv=texcoord*uvTransform.xy+uvTransform.zw;gl_Position=projection*world*vec4(worldPosition,1);}
)";
    const char* fs=R"(#version 330 core
in vec3 worldPosition,worldNormal;in vec2 uv;out vec4 outputColor;
uniform sampler2D diffuse;uniform samplerCube environmentMap;uniform vec4 color;
uniform vec3 eye,light,lightColor;uniform float ambient,shade,environmentAdd;
uniform vec4 gammaSettings; // display gamma, texture gamma, light gamma, gamma blending
uniform vec2 lightingSettings; // brightness, lambert
uniform bool fullbright,masked,environment,flatshade;
void main(){vec4 tex=texture(diffuse,uv);if(masked && tex.a<0.5)discard;
vec3 rgb=tex.rgb;if(environment){vec3 r=reflect(worldPosition-eye,normalize(worldNormal));r.z=-r.z;rgb=(rgb+environmentAdd)*texture(environmentMap,r).rgb;}
float exponent=gammaSettings.w>0.5?1.0/gammaSettings.x:1.0;
rgb=pow(max(rgb,vec3(0)),vec3(gammaSettings.y*exponent));
if(!fullbright){float cosine=dot(normalize(worldNormal),light);float illumination=ambient+shade;
if(flatshade)illumination=ambient+shade*0.8;
else {cosine=(cosine+lightingSettings.y-1.0)/lightingSettings.y;if(cosine>0.0)illumination-=shade*cosine;}
float brightness=lightingSettings.x;float threshold=brightness<=0.0?0.125:brightness>1.0?0.05:0.125-brightness*brightness*0.075;
float lv=pow(clamp(illumination/255.0,0.0,1.0),gammaSettings.z);if(brightness>1.0)lv*=brightness;
lv=lv>threshold?0.125+(lv-threshold)/(1.0-threshold)*0.875:lv/threshold*0.125;
rgb*=pow(max(lightColor*lv,vec3(0)),vec3(exponent));}
vec3 modulation=gammaSettings.w>0.5?color.rgb:pow(max(color.rgb,vec3(0)),vec3(gammaSettings.x));
outputColor=vec4(rgb*modulation,tex.a*color.a);}
)";
    const auto vertex=Compile(GL_VERTEX_SHADER,vs);GLuint fragment{};
    try {fragment=Compile(GL_FRAGMENT_SHADER,fs);}catch(...){glDeleteShader(vertex);throw;}
    program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    GLint ok{};glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);glDeleteProgram(program);program=0;throw std::runtime_error(log);}
    auto location=[](const char* name){return glGetUniformLocation(program,name);};
    uniform={location("world"),location("projection"),location("bones"),location("uvTransform"),location("color"),location("eye"),location("light"),location("lightColor"),location("ambient"),location("shade"),location("fullbright"),location("masked"),location("environment"),location("environmentAdd"),location("gammaSettings"),location("lightingSettings"),location("flatshade")};
    glUseProgram(program);glUniform1i(location("diffuse"),0);glUniform1i(location("environmentMap"),1);
    glGenVertexArrays(1,&quadVAO);glGenBuffers(1,&quadVBO);glBindVertexArray(quadVAO);glBindBuffer(GL_ARRAY_BUFFER,quadVBO);glBufferData(GL_ARRAY_BUFFER,6*sizeof(Vertex),nullptr,GL_STREAM_DRAW);
    for(int i=0;i<3;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==2?2:3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(size_t(i)*12));}
    glEnableVertexAttribArray(3);glVertexAttribIPointer(3,2,GL_INT,sizeof(Vertex),(void*)offsetof(Vertex,bone));
}
void Camera()
{
    glUseProgram(program);
    if(!colorSettings[0]){const char* names[]{"gamma","texgamma","lightgamma","r_gamma_blend","brightness","lambert"};for(int i=0;i<6;++i)colorSettings[i]=engine.pfnGetCvarPointer(names[i]);}
    auto setting=[](int index,float fallback){const auto* v=colorSettings[index];return v && std::isfinite(v->value)?v->value:fallback;};
    glUniform4f(uniform.gamma,std::max(.1f,setting(0,2.2f)),std::max(.1f,setting(1,2.f)),std::max(.1f,setting(2,2.5f)),!renderer || setting(3,1)>0?1.f:0.f);
    glUniform2f(uniform.lighting,std::max(0.f,setting(4,0)),std::max(1.f,setting(5,1.5f)));
    if(postFrame)
    {
        glUniformMatrix4fv(uniform.world,1,GL_FALSE,postFrame->world_matrix);glUniformMatrix4fv(uniform.projection,1,GL_FALSE,postFrame->projection_matrix);
        glUniform4f(uniform.gamma,std::max(.1f,setting(0,2.2f)),std::max(.1f,setting(1,2.f)),std::max(.1f,setting(2,2.5f)),postFrame->flags&META_POST_SCENE_GAMMA?1.f:0.f);
    }
    else if(renderer){glUniformMatrix4fv(uniform.world,1,GL_FALSE,renderer->GetWorldMatrix());glUniformMatrix4fv(uniform.projection,1,GL_FALSE,renderer->GetProjectionMatrix());}
    else {float w[16],p[16];glGetFloatv(GL_MODELVIEW_MATRIX,w);glGetFloatv(GL_PROJECTION_MATRIX,p);glUniformMatrix4fv(uniform.world,1,GL_FALSE,w);glUniformMatrix4fv(uniform.projection,1,GL_FALSE,p);}
    glUniform3fv(uniform.eye,1,postFrame?postFrame->inverse_world_matrix+12:view.vieworg);
}
float Frame(const cl_entity_t* ent,const mstudioseqdesc_t& seq,double time)
{
    double frame=seq.numframes>1?ent->curstate.frame*(seq.numframes-1)/256.0:0;
    if(time>=ent->curstate.animtime)frame+=(time-ent->curstate.animtime)*ent->curstate.framerate*seq.fps;
    if(seq.flags&STUDIO_LOOPING){if(seq.numframes>1)frame-=std::floor(frame/(seq.numframes-1))*(seq.numframes-1);}
    else frame=std::clamp(frame,0.0,std::max(0.0,seq.numframes-1.001));return float(frame);
}
State& Capture(cl_entity_t* ent,Asset& asset,State* supplied=nullptr)
{
    int frame{};double time{},old{};studio.GetTimes(&frame,&time,&old);
    auto& state=supplied?*supplied:states[ent];
    if(state.asset!=&asset || time<state.time || state.frame+2<frame)
    {
        state={};state.asset=&asset;state.model=ent->model;state.body.Reset(asset.profile);
        state.sprites.resize(asset.sprites.size());state.fades.resize(asset.config.Fades.size());state.results.resize(state.fades.size());state.attachments.resize(asset.Header().numattachments);
    }
    memcpy(state.bones.data(),studio.StudioGetBoneTransform(),asset.Header().numbones*sizeof(state.bones[0]));
    if(BeamEffectIdentity(ent) && ent->curstate.scale>0 && ent->curstate.scale!=1)
        for(int bone=0;bone<asset.Header().numbones;++bone)for(int axis=0;axis<3;++axis)
        {
            for(int n=0;n<3;++n)state.bones[bone][axis][n]*=ent->curstate.scale;
            state.bones[bone][axis][3]=ent->origin[axis]+(state.bones[bone][axis][3]-ent->origin[axis])*ent->curstate.scale;
        }
    const auto& h=asset.Header();auto attachments=asset.Data<mstudioattachment_t>(h.attachmentindex,h.numattachments);
    for(size_t i=0;i<attachments.size();++i)
    {
        const auto& a=attachments[i];if(a.bone<0 || a.bone>=h.numbones)throw std::runtime_error("invalid attachment bone");
        for(int axis=0;axis<3;++axis){const auto& m=state.bones[a.bone][axis];state.attachments[i][axis]=a.org[0]*m[0]+a.org[1]*m[1]+a.org[2]*m[2]+m[3];}
        if(i<4)memcpy(ent->attachment[i],state.attachments[i].data(),12);
    }
    if(state.frame==frame)return state;
    const int sequence=ent->curstate.sequence;const double delta=state.time<0?0:std::max(0.,time-state.time);
    const double action=std::max(0.,time-ent->curstate.animtime);
    if(state.sequence!=sequence || state.start!=ent->curstate.animtime)
    {
        if(state.sequence<0 || sequence==state.sequence || sequence/6!=state.sequence/6 || sequence%6==2 || state.sequence%6==1 || state.sequence>=12)state.reviveTime=action;
        else if(state.sequence%6==2)state.reviveTime=std::max(0.,state.action-1.)+action;
        else state.reviveTime+=delta;
    }
    else state.reviveTime+=delta;
    state.time=time;state.frame=frame;state.sequence=sequence;state.start=ent->curstate.animtime;state.action=action;
    state.body.Time=time;state.body.Update(sequence,float(time));
    if(sequence<0 || sequence>=h.numseq)return state;
    const auto& seq=asset.Data<mstudioseqdesc_t>(h.seqindex,h.numseq)[sequence];const float modelFrame=Frame(ent,seq,time);
    for(size_t i=0;i<asset.config.Sprites.size();++i)
    {
        const auto& binding=asset.config.Sprites[i];if(!asset.sprites[i] || sequence>=binding.Rules.size())continue;
        const auto& rule=binding.Rules[sequence];if(rule.Sequence<0)continue;
        state.sprites[i]=AdvanceSpriteFrame(rule,binding.FPS,asset.sprites[i]->numframes,state.sprites[i],delta,modelFrame);
    }
    for(size_t i=0;i<state.fades.size();++i)
    {
        state.fades[i].BeginFrame();state.results[i]={};
        if(const auto* rule=asset.config.Fades[i].Rule(sequence))state.results[i]=EvaluateFade(*rule,modelFrame,seq.fps,seq.numframes,ent->curstate.framerate,state.fades[i].Completed);
    }
    return state;
}
void LoadParts(cl_entity_t* entity,Asset& primary)
{
    if(parts.primary==entity->model)return;
    parts={};parts.primary=entity->model;
    const auto& h=primary.Header();const auto mainBones=primary.Data<mstudiobone_t>(h.boneindex,h.numbones);
    auto check=[](const Asset& asset,const std::vector<std::string>& actions)
    {
        const auto& header=asset.Header();if(header.numseq!=actions.size())return false;
        const auto sequences=asset.Data<mstudioseqdesc_t>(header.seqindex,header.numseq);
        for(size_t i=0;i<actions.size();++i)if(Name(sequences[i].label)!=actions[i])return false;return true;
    };
    for(int slot=0;slot<2;++slot)
    {
        const auto definition=PartsProfile(primary.name,slot);if(definition.Actions.empty())continue;
        if(!check(primary,definition.PrimaryActions))throw std::runtime_error("primary actions do not match companion profile");
        std::string name=primary.name.substr(0,primary.name.size()-4)+"_parts";
        if(definition.Behavior!=PartsBehavior::Standard)name+=slot==0?"01":"02";name+=".mdl";
        auto* model=LoadEngineModel(name.c_str());if(!model)throw std::runtime_error("missing companion: "+name);
        auto* asset=GetAsset(model);if(!asset || !check(*asset,definition.Actions))throw std::runtime_error("companion actions do not match profile: "+name);
        const auto& partHeader=asset->Header();const auto bones=asset->Data<mstudiobone_t>(partHeader.boneindex,partHeader.numbones);
        auto& part=parts.slots[slot];part.boneMap.resize(bones.size(),-1);
        for(size_t i=0;i<bones.size();++i)
        {
            if(bones[i].parent>=int(i) || bones[i].parent<-1)throw std::runtime_error("invalid companion bone hierarchy");
            for(size_t j=0;j<mainBones.size();++j)if(_stricmp(Name(bones[i].name).c_str(),Name(mainBones[j].name).c_str())==0){part.boneMap[i]=int(j);break;}
            if(bones[i].parent<0 && (part.boneMap[i]<0 || mainBones[part.boneMap[i]].parent>=0))throw std::runtime_error("unmatched companion root bone");
        }
        EnsureGeometry(*asset);part.model=model;part.asset=asset;part.definition=definition;part.start=engine.GetClientTime();
    }
}
void DrawAsset(cl_entity_t* ent,Asset& asset,State& state,int onlyPass=-1);
void DrawParts(cl_entity_t* entity,Asset& primary,const State& parent,int (*original)(int))
{
    LoadParts(entity,primary);
    // Reception uses GetClientTime. StudioGetTimes can lag that clock during
    // drawing, which would falsely classify a new message as a time rollback.
    const double messageTime=engine.GetClientTime();
    for(size_t slot=0;slot<parts.slots.size();++slot)
    {
        auto& pending=pendingParts[slot];auto& part=parts.slots[slot];
        if(!pending.Value)continue;
        const auto index=pending.Value->Model;
        if(studio.GetModelByIndex(int(index))!=entity->model)
        {pending.Take(0,messageTime);continue;}
        if(auto message=pending.Take(index,messageTime))
        {
            if(!part.asset || message->Sequence>=part.asset->Header().numseq || message->Skin>=part.asset->Header().numskinfamilies){++rejectedMessages;continue;}
            part.sequence=message->Sequence;part.skin=message->Skin;part.body=message->Body;
            part.ammo=message->Ammo;part.maximum=message->Maximum;part.expanded=(message->Flags&1)!=0;part.start=parent.time;
        }
    }
    const cl_entity_t saved=*entity;Bones savedLights{};memcpy(savedLights.data(),studio.StudioGetLightTransform(),sizeof(Bones));
    struct Restore
    {
        cl_entity_t* entity;const cl_entity_t& saved;const Bones& bones;const Bones& lights;
        ~Restore(){*entity=saved;studio.StudioSetHeader(studio.Mod_Extradata(saved.model));studio.SetRenderModel(saved.model);memcpy(studio.StudioGetBoneTransform(),bones.data(),sizeof(Bones));memcpy(studio.StudioGetLightTransform(),lights.data(),sizeof(Bones));}
    } restore{entity,saved,parent.bones,savedLights};
    for(auto& part:parts.slots)
    {
        if(!part.model)continue;int sequence=part.sequence;double phase=std::max(0.,parent.time-part.start);
        if(part.definition.Behavior==PartsBehavior::AmmoGauge && sequence==1 && phase>1.){sequence=0;phase-=1.;}
        if(part.definition.Behavior==PartsBehavior::AmmoState && sequence==4 && phase>1.){sequence=part.expanded?2:3;phase-=1.;}
        const double transition=sequence==1?part.definition.OpenSeconds:sequence==3?part.definition.CloseSeconds:0;
        if(transition>0 && (part.definition.InclusiveTimer?phase>=transition:phase>transition)){sequence=sequence==1?2:0;phase-=transition;}
        *entity=saved;entity->model=part.model;entity->curstate.sequence=sequence;entity->curstate.body=part.body;
        entity->curstate.skin=part.definition.FollowSkin?saved.curstate.skin:part.definition.ActiveSkin?(sequence==2?1:0):part.skin;
        entity->curstate.frame=0;entity->curstate.framerate=1;entity->curstate.animtime=float(parent.time-phase);
        entity->latched.prevsequence=sequence;entity->latched.prevframe=0;entity->latched.sequencetime=0;
        const auto& h=part.asset->Header();
        if(part.definition.Behavior==PartsBehavior::AmmoGauge && sequence==0)
        {
            const int frame=int(66.f/std::max(1,part.maximum)*std::clamp(part.ammo,0,std::max(1,part.maximum)));
            const auto& seq=part.asset->Data<mstudioseqdesc_t>(h.seqindex,h.numseq)[sequence];
            entity->curstate.frame=seq.numframes>1?frame*256.f/(seq.numframes-1):0;entity->curstate.framerate=0;
        }
        if(!original(0))continue;
        Bones local{};memcpy(local.data(),studio.StudioGetBoneTransform(),sizeof(Bones));Bones merged=local;
        const auto bones=part.asset->Data<mstudiobone_t>(h.boneindex,h.numbones);
        for(int i=0;i<h.numbones;++i)
        {
            if(part.boneMap[i]>=0){merged[i]=parent.bones[part.boneMap[i]];continue;}
            const int p=bones[i].parent;if(p<0)continue;
            // Recover child-local transform from the original orthonormal parent,
            // then concatenate with the parent already merged by bone name.
            float relative[3][4]{};
            for(int a=0;a<3;++a)for(int b=0;b<4;++b)for(int k=0;k<3;++k)
                relative[a][b]+=local[p][k][a]*(local[i][k][b]-(b==3?local[p][k][3]:0));
            for(int a=0;a<3;++a)for(int b=0;b<4;++b)
            {merged[i][a][b]=b==3?merged[p][a][3]:0;for(int k=0;k<3;++k)merged[i][a][b]+=merged[p][a][k]*relative[k][b];}
        }
        memcpy(studio.StudioGetBoneTransform(),merged.data(),sizeof(Bones));
        auto& state=Capture(entity,*part.asset,&part.state);DrawAsset(entity,*part.asset,state);
    }
}
int PartMessage(const char* name,int size,void* data)
{
    const auto message=data && size>0?ParsePartPresentation({(const uint8_t*)data,size_t(size)}):std::nullopt;
    if(!message){++rejectedMessages;return 0;}
    pendingParts[message->Slot].Set(*message,engine.GetClientTime());++partMessages;return 1;
}
int ViewMessage(const char*,int size,void* data)
{
    const auto message=data && size>0?ParseViewPresentation({(const uint8_t*)data,size_t(size)}):std::nullopt;
    if(!message){++rejectedMessages;return 0;}
    pendingView.Set(*message,engine.GetClientTime());++viewMessages;return 1;
}
void Blending(Blend blend)
{
    GLState::Enable(GL_BLEND,blend!=Blend::Opaque && blend!=Blend::Masked);glBlendEquation(GL_FUNC_ADD);
    if(blend==Blend::Add)glBlendFunc(GL_ONE,GL_ONE);
    else if(blend==Blend::AddAlpha)glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
void DrawAsset(cl_entity_t* ent,Asset& asset,State& state,int onlyPass)
{
    const auto& h=asset.Header();Camera();glUniform4fv(uniform.bones,h.numbones*3,(float*)state.bones.data());
    if(!postFrame)
    {
        alight_t lighting{};lighting.plightvec=state.lightDirection.data();studio.StudioDynamicLight(ent,&lighting);studio.StudioEntityLight(&lighting);
        std::copy_n(lighting.color,3,state.lightColor.begin());state.ambient=lighting.ambientlight;state.shade=lighting.shadelight;
    }
    glUniform3fv(uniform.light,1,state.lightDirection.data());glUniform3fv(uniform.lightColor,1,state.lightColor.data());glUniform1f(uniform.ambient,float(state.ambient));glUniform1f(uniform.shade,float(state.shade));
    glBindVertexArray(asset.vao);glEnable(GL_DEPTH_TEST);
    const auto groups=asset.Data<mstudiobodyparts_t>(h.bodypartindex,h.numbodyparts);
    const auto skins=asset.Data<int16_t>(h.skinindex,h.numskinref*h.numskinfamilies);
    const int skin=std::clamp(int(ent->curstate.skin),0,h.numskinfamilies-1);
    // Alpha is shared across meshes in file order, with body overrides scoped
    // to one group, matching the verified engine material-loop trace.
    float materialAlpha=1,savedAlpha=1;int lastGroup=-1;bool bodyAlpha=false;
    for(int pass=0;pass<2;++pass)
    {
    if(onlyPass>=0 && onlyPass!=pass)continue;
    lastGroup=-1;
    for(const auto& batch:asset.batches)
    {
        if(batch.group!=lastGroup)
        {
            if(bodyAlpha)materialAlpha=savedAlpha;
            savedAlpha=materialAlpha;lastGroup=batch.group;
            bodyAlpha=batch.group<state.body.Groups.size() && state.body.Groups[batch.group].Alpha>=0;
            if(bodyAlpha)materialAlpha=state.body.Groups[batch.group].Alpha/255.f;
        }
        const auto& group=groups[batch.group];int body=ent->curstate.body;
        if(batch.group<state.body.Groups.size() && state.body.Groups[batch.group].Body>=0)body=state.body.Groups[batch.group].Body;
        int selected=(std::max(0,body)/group.base)%group.nummodels;
        if(ent==engine.GetViewModel())selected=CharacterHandSubmodel(asset,batch.group,selected,ent->curstate.body);
        if(batch.submodel!=selected)continue;
        const int index=skins[skin*h.numskinref+batch.skinref];const auto& m=asset.materials[index];
        auto scroll=state.body.Scroll(batch.group);
        const bool transparent=(m.flags&0x20) || (m.special && m.blend!=Blend::Opaque) || m.fade>=0 || m.commonAlpha || m.commonAdd;
        if(transparent!=(pass==1) || (bodyAlpha && materialAlpha==0))continue;
        const auto draw=EvaluateMaterial(m,m.flags,m.fade>=0?state.results[m.fade]:FadeResult{},materialAlpha);
        if(m.fade>=0 && state.results[m.fade].Active)state.fades[m.fade].Observe(state.results[m.fade].MarkCompleted);
        if(materialAlpha<=0)continue;
        GLuint texture=m.texture;float width=float(m.width),height=float(m.height);
        if(m.sprite>=0)
        {
            const auto& binding=asset.config.Sprites[m.sprite];const int sequence=ent->curstate.sequence;
            if(sequence>=0 && sequence<binding.Rules.size() && binding.Rules[sequence].Sequence>=0 && asset.sprites[m.sprite])
            {if(auto* frame=SpriteFrameAt(asset.sprites[m.sprite],SpriteFrameIndex(state.sprites[m.sprite],asset.sprites[m.sprite]->numframes),state.time))texture=frame->texture;}
        }
        if(ent==engine.GetViewModel())ApplyHand(m,texture,width,height,scroll.data());
        Blending(draw.blend);glDepthMask(postFrame ? (onlyPass==0 && draw.depth) : draw.depth);GLState::Enable(GL_CULL_FACE,draw.cull);glCullFace(GL_FRONT);
        const auto* right=engine.pfnGetCvarPointer("cl_righthand");if(ent==engine.GetViewModel() && right && right->value>0)glDisable(GL_CULL_FACE);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_CUBE_MAP,m.environment);glActiveTexture(GL_TEXTURE0);
        glUniform4f(uniform.uv,1/width,1/height,scroll[0],scroll[1]);glUniform4fv(uniform.color,1,draw.color.data());
        glUniform1i(uniform.fullbright,draw.fullbright);glUniform1i(uniform.masked,draw.masked);
        glUniform1i(uniform.flatshade,(m.flags&STUDIO_NF_FLATSHADE)!=0);
        glUniform1i(uniform.environment,m.environment!=0);glUniform1f(uniform.environmentAdd,m.environmentAdd);
        glDrawElements(GL_TRIANGLES,batch.count,GL_UNSIGNED_INT,(void*)(size_t(batch.offset)*4));
    }
    if(bodyAlpha)materialAlpha=savedAlpha;bodyAlpha=false;
    }
    ++draws;
}
void Quad(model_t* model,int index,const float* origin,float scale,float alpha,float angle,bool alphaBlend,bool overlay,bool flip)
{
    const auto* frame=SpriteFrameAt(model,index);if(!frame)return;
    float eye[3],up[3],right[3],normal[3];
    if(postFrame)for(int i=0;i<3;++i)
    {right[i]=postFrame->inverse_world_matrix[i];up[i]=postFrame->inverse_world_matrix[4+i];normal[i]=-postFrame->inverse_world_matrix[8+i];}
    else studio.GetViewInfo(eye,up,right,normal);
    const float sine=std::sin(angle),cosine=std::cos(angle);float u[3],r[3];
    for(int i=0;i<3;++i){u[i]=up[i]*cosine+right[i]*sine;r[i]=right[i]*cosine-up[i]*sine;}
    constexpr int corners[]{0,1,2,0,2,3};const float x[]{frame->left,frame->right,frame->right,frame->left};const float y[]{frame->up,frame->up,frame->down,frame->down};
    Vertex vertices[6]{};for(int i=0;i<6;++i){const int c=corners[i];for(int a=0;a<3;++a){vertices[i].position[a]=origin[a]+scale*(x[c]*r[a]+y[c]*u[a]);vertices[i].normal[a]=normal[a];}vertices[i].uv[0]=float((c==1 || c==2)!=flip);vertices[i].uv[1]=float(c>=2);}
    const float identity[]{1,0,0,0,0,1,0,0,0,0,1,0};glUniform4fv(uniform.bones,3,identity);
    glBindVertexArray(quadVAO);glBindBuffer(GL_ARRAY_BUFFER,quadVBO);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(vertices),vertices);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,frame->texture);glUniform4f(uniform.uv,1,1,0,0);glUniform4f(uniform.color,1,1,1,alpha);
    glUniform1i(uniform.fullbright,1);glUniform1i(uniform.masked,0);glUniform1i(uniform.environment,0);Blending(alphaBlend?Blend::Alpha:Blend::AddAlpha);
    glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);GLState::Enable(GL_DEPTH_TEST,!overlay);glDrawArrays(GL_TRIANGLES,0,6);
}
void DrawEffects(const cl_entity_t* owner,double now)
{
    std::erase_if(effects,[&](const Effect& e){return now<e.birth || now-e.birth>=e.rule.Duration(e.sprite->numframes) || e.owner->model!=e.ownerModel || (e.rule.SequenceOnly && e.owner->curstate.sequence!=e.sequence) || (e.rule.Animate && !e.rule.Loop && e.first+(now-e.birth)*e.rule.FPS>=e.sprite->numframes);});
    for(auto& e:effects)
    {
        if(e.owner!=owner)continue;
        if(e.rule.Follow>=0){const auto it=states.find(e.owner);if(it==states.end() || e.rule.Follow>=it->second.attachments.size())continue;e.position=it->second.attachments[e.rule.Follow];}
        int frame=e.first;if(e.rule.Animate)frame+=int((now-e.birth)*e.rule.FPS);if(e.rule.Loop)frame%=e.sprite->numframes;
        const auto* hand=engine.pfnGetCvarPointer("cl_righthand");const bool viewmodel=owner==engine.GetViewModel();
        Quad(e.sprite,frame,e.position.data(),e.rule.Scale,1,e.angle,e.rule.Alpha,viewmodel && e.rule.Overlay,viewmodel && e.rule.HandFlip && hand && hand->value==0);
    }
}
void Revive(cl_entity_t* ent,State& state)
{
    if(!state.asset->revive || state.sequence<0 || state.sequence>=12 || state.sequence%6==1)return;
    const bool variant=state.sequence>=6,draw=state.sequence%6==2 && state.action<1.;const auto* hand=engine.pfnGetCvarPointer("cl_righthand");
    std::string name="sprites/ef_revivegun_";name+=draw?"draw":"idle";name+=variant?"2":"1";if(hand && hand->value==0)name+="_left";name+=".spr";
    auto* sprite=LoadEngineModel(name.c_str());if(!sprite || sprite->type!=mod_sprite || sprite->numframes<1)return;
    const auto phase=state.sequence%6==2?(draw?state.action:state.action-1):state.reviveTime;
    Quad(sprite,int(phase*(draw||variant?30:20))%sprite->numframes,state.attachments[5].data(),variant?.04f:.028f,250/255.f,0,false,true,false);
}
void DrawOwnedWorld(const MetaRendererPostSceneFrame& frame) noexcept
{
    if(!enabled || !enabled->value || !program)return;
    try
    {
        GLState save;postFrame=&frame;
        glDisable(GL_STENCIL_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
        // Host transparency/OIT has resolved and world depth is still intact.
        // No Renderer changes or viewmodel depth/projection are involved.
        auto distance=[](const WorldDraw& d){float value=0;for(int n=0;n<3;++n){const float x=d.entity->origin[n]-view.vieworg[n];value+=x*x;}return value;};
        std::array<WorldDraw,256> visible{};size_t count=0;
        for(auto& draw:worldDraws)
        {
            if(draw.entity && draw.frame==int(frame.frame_number) && WorldIdentity(draw.entity)==draw.identity &&
                draw.target==renderer->GetCurrentSceneFBO() &&
                std::equal(draw.world.begin(),draw.world.end(),frame.world_matrix) &&
                std::equal(draw.projection.begin(),draw.projection.end(),frame.projection_matrix))
                visible[count++]=draw;
            draw={};
        }
        std::sort(visible.begin(),visible.begin()+count,[&](const auto& a,const auto& b){return distance(a)>distance(b);});
        // World weapon bodies participate in occlusion before any transparent
        // weapon/effect layer. TEMPENTITY effects never write scene depth.
        for(size_t i=0;i<count;++i)if(WorldWeaponFor(visible[i].entity))
        {
            const auto found=states.find(visible[i].entity);
            if(found!=states.end())DrawAsset(visible[i].entity,*found->second.asset,found->second,0);
        }
        for(size_t i=0;i<count;++i)
        {
            auto* entity=visible[i].entity;const auto found=states.find(entity);
            if(found==states.end())continue;
            auto& state=found->second;
            if(visible[i].events && state.eventFrame!=state.frame)
            {
                // StudioClientEvents reads the engine's current entity/header.
                // Dispatch only for this eligible main view, once per frame.
                auto* previous=studio.GetCurrentEntity();
                auto* previousHeader=currentStudioHeader;
                auto* previousModel=currentStudioModel;
                Bones previousBones{},previousLights{};
                memcpy(previousBones.data(),studio.StudioGetBoneTransform(),sizeof(Bones));
                memcpy(previousLights.data(),studio.StudioGetLightTransform(),sizeof(Bones));
                memcpy(studio.StudioGetBoneTransform(),state.bones.data(),sizeof(Bones));
                memcpy(studio.StudioGetLightTransform(),state.bones.data(),sizeof(Bones));
                renderer->SetCurrentEntity(entity);studio.StudioSetHeader(studio.Mod_Extradata(entity->model));studio.SetRenderModel(entity->model);
                studio.StudioClientEvents();state.eventFrame=state.frame;
                renderer->SetCurrentEntity(previous);
                studio.StudioSetHeader(previousHeader);studio.SetRenderModel(previousModel);
                memcpy(studio.StudioGetBoneTransform(),previousBones.data(),sizeof(Bones));
                memcpy(studio.StudioGetLightTransform(),previousLights.data(),sizeof(Bones));
            }
            DrawAsset(entity,*state.asset,state,WorldWeaponFor(entity)?1:-1);DrawEffects(entity,state.time);
        }
    }
    catch(const std::exception& e){engine.Con_Printf("CSOLSupport: world effects: %s\n",e.what());}
    postFrame=nullptr;
}
}

bool DrawWorldWeaponPoints()
{
    auto* source=studio.GetCurrentEntity();auto* model=currentStudioModel;
    if(!source || source==engine.GetViewModel() || !model || model->type!=mod_studio || !enabled || !enabled->value)return false;
    auto name=std::string_view(model->name,strnlen(model->name,sizeof(model->name)));
    // CS changes the studio header for a merged held weapon without updating
    // SetRenderModel (which still identifies the player). Match the active
    // header to that player's precached weapon before taking over its meshes.
    if(!name.starts_with("models/p_") && source->curstate.weaponmodel>0)
    {
        auto* held=studio.GetModelByIndex(source->curstate.weaponmodel);
        if(held && held->type==mod_studio && std::string_view(held->name,strnlen(held->name,sizeof(held->name))).starts_with("models/p_") &&
            studio.Mod_Extradata(held)==currentStudioHeader)
        {model=held;name=std::string_view(model->name,strnlen(model->name,sizeof(model->name)));}
    }
    const bool attached=name.starts_with("models/p_");
    const bool character=source->player && name.starts_with("models/player/csol");
    if(!attached && !character && !name.starts_with("models/w_"))return false;
    if((attached || character) && HostDrawsLocalHeldWeapon(source))return false;
    if(!EnsureGL() || !renderer || !postRenderer || failures.contains(model))return false;
    try
    {
        auto [cached,asset]=CachedAsset(model);
        if(!cached){GLState save;asset=GetAsset(model);}
        if(!asset || !asset->special)return false;
        if(!asset->vao || !program){GLState save;EnsureGeometry(*asset);Program();}
        int frame;double now,old;studio.GetTimes(&frame,&now,&old);
        auto slot=std::find_if(worldWeapons.begin(),worldWeapons.end(),[&](const WorldWeapon& w){return w.source==source && w.weaponModel==model;});
        if(slot==worldWeapons.end())slot=std::find_if(worldWeapons.begin(),worldWeapons.end(),[&](const WorldWeapon& w){return !w.source || w.frame+2<frame;});
        if(slot==worldWeapons.end())return false;
        if(slot->source!=source || slot->weaponModel!=model || slot->sourceModel!=source->model || slot->frame+2<frame)
        {
            ForgetSceneEntity(&slot->entity);*slot={};
            if(++worldWeaponGeneration==0)++worldWeaponGeneration;
            slot->generation=worldWeaponGeneration;
        }
        slot->source=source;slot->sourceModel=source->model;slot->weaponModel=model;slot->attached=attached;slot->frame=frame;
        slot->entity=*source;slot->entity.model=model;
        // The host has already merged the held model's bones into the player.
        // Its sequence number is not the player's skull8 sequence.
        if(attached){slot->entity.curstate.sequence=0;slot->entity.curstate.frame=0;}
        auto& state=Capture(&slot->entity,*asset);
        alight_t lighting{};lighting.plightvec=state.lightDirection.data();studio.StudioDynamicLight(source,&lighting);studio.StudioEntityLight(&lighting);
        std::copy_n(lighting.color,3,state.lightColor.begin());state.ambient=lighting.ambientlight;state.shade=lighting.shadelight;
        auto draw=std::find_if(worldDraws.begin(),worldDraws.end(),[&](const WorldDraw& d){return d.entity==&slot->entity || !d.entity;});
        if(draw==worldDraws.end())return false;
        *draw={&slot->entity,slot->generation,frame,false,renderer->GetCurrentSceneFBO()};
        std::copy_n(renderer->GetWorldMatrix(),16,draw->world.begin());std::copy_n(renderer->GetProjectionMatrix(),16,draw->projection.begin());
        return true;
    }
    catch(const std::exception& error){failures.insert(model);engine.Con_Printf("CSOLSupport: world weapon %s: %s\n",model->name,error.what());return false;}
}

bool DrawModel(cl_entity_t* ent,int flags,int (*original)(int))
{
    if(!ent->model || !enabled || !enabled->value)return false;
    const bool isView=ent==engine.GetViewModel();
    const unsigned worldIdentity=isView?0:BeamEffectIdentity(ent);
    if(!isView && !worldIdentity)return false;
    if(!EnsureGL())return false;
    StockHandScope stockHands(isView?ent:nullptr);
    if(!isView)
    {
        if(!worldIdentity || !postRenderer)return false;
        try
        {
            auto [cached,asset]=CachedAsset(ent->model);
            if(!cached || !asset || !asset->vao || !program)
            {GLState save;if(!cached)asset=GetAsset(ent->model);if(!asset)return false;EnsureGeometry(*asset);Program();}
            // Ask the host for unscaled bones, then apply scale exactly once in
            // Capture. Client implementations differ in whether they scale here.
            const float scale=ent->curstate.scale;ent->curstate.scale=1;
            const int result=original(flags&~(STUDIO_RENDER|STUDIO_EVENTS));ent->curstate.scale=scale;
            if(!result)return true;
            auto& state=Capture(ent,*asset);
            if(flags&STUDIO_EVENTS)state.eventRequestedFrame=state.frame;
            alight_t lighting{};lighting.plightvec=state.lightDirection.data();studio.StudioDynamicLight(ent,&lighting);studio.StudioEntityLight(&lighting);
            std::copy_n(lighting.color,3,state.lightColor.begin());state.ambient=lighting.ambientlight;state.shade=lighting.shadelight;
            if(flags&STUDIO_RENDER)
            {
                auto slot=std::find_if(worldDraws.begin(),worldDraws.end(),[&](const WorldDraw& d){return d.entity==ent || !d.entity;});
                if(slot!=worldDraws.end())
                {
                    *slot={ent,worldIdentity,state.frame,state.eventRequestedFrame==state.frame,renderer->GetCurrentSceneFBO()};
                    std::copy_n(renderer->GetWorldMatrix(),16,slot->world.begin());
                    std::copy_n(renderer->GetProjectionMatrix(),16,slot->projection.begin());
                }
            }
        }
        catch(const std::exception& error){engine.Con_Printf("CSOLSupport: world model: %s\n",error.what());}
        return true;
    }
    if(presentationModel!=ent->model){presentationModel=ent->model;viewState.reset();parts={};}
    const char* name=ent->model->name;if(failures.contains(ent->model))return false;
    auto [cached,asset]=CachedAsset(ent->model);
    try {
        if(!cached || !program || (asset && !asset->handAdaptationChecked && ViewCharacter())) {
            GLState save;if(!cached)asset=GetAsset(ent->model);asset=ViewHandAsset(asset);if(asset && asset->special)Program();
        } else if(asset && ViewCharacter() && asset->adaptedHands)asset=asset->adaptedHands.get();
    }
    catch(const std::exception& error){failures.insert(ent->model);engine.Con_Printf("CSOLSupport: %s: %s\n",name,error.what());return false;}
    if(!asset || !asset->special)return false;
    const double now=engine.GetClientTime();
    if(pendingView.Value)
    {
        const auto index=pendingView.Value->Model;
        if(studio.GetModelByIndex(int(index))==ent->model)
        {
            if(auto message=pendingView.Take(index,now))
            {if(message->Skin<asset->Header().numskinfamilies)viewState=*message;else ++rejectedMessages;}
        }
        else pendingView.Take(0,now);
    }
    // Temporary draw overrides do not rewrite the native client's prediction.
    const auto skin=ent->curstate.skin;const auto body=ent->curstate.body;
    struct RestorePresentation{cl_entity_t* ent;short skin;int body;~RestorePresentation(){ent->curstate.skin=skin;ent->curstate.body=body;}} restore{ent,skin,body};
    if(viewState){ent->curstate.skin=viewState->Skin;ent->curstate.body=viewState->Body;}
    const int beamSkin=BeamgunViewSkin(ent);
    if(beamSkin>=0 && beamSkin<asset->Header().numskinfamilies)ent->curstate.skin=short(beamSkin);
    stockHands.PrepareCustomRender(flags);
    if(!original(flags & ~(STUDIO_RENDER|STUDIO_EVENTS)))return true;
    try
    {
        auto& state=Capture(ent,*asset);
        if(flags&STUDIO_EVENTS)stockHands.ClientEvents();
        if(flags&STUDIO_RENDER){GLState save;UpdateHands();DrawAsset(ent,*asset,state);DrawParts(ent,*asset,state,original);Camera();DrawEffects(ent,state.time);Revive(ent,state);}
    }
    catch(const std::exception& error){failures.insert(ent->model);engine.Con_Printf("CSOLSupport: %s: %s\n",name,error.what());}
    return true;
}
void StudioEvent(const mstudioevent_t* event,const cl_entity_t* entity)
{
    if(CannonStudioEvent(event,entity))return;
    if(!event || event->options[0]!='#'){if(downstream.HUD_StudioEvent)downstream.HUD_StudioEvent(event,entity);return;}
    try
    {
        if(event->event<5000 || event->event>5639)
        {if(downstream.HUD_StudioEvent)downstream.HUD_StudioEvent(event,entity);return;}
        const auto parsed=ParseSpriteEvent(event->event,{event->options,strnlen(event->options,64)});
        const auto it=states.find(entity);
        if(!parsed || it==states.end())
        {if(downstream.HUD_StudioEvent)downstream.HUD_StudioEvent(event,entity);return;}
        if(effects.size()>=256)return;
        const auto& rule=*parsed;const auto& state=it->second;if(rule.Attachment>=state.attachments.size() || rule.Follow>=int(state.attachments.size()))return;
        auto* sprite=LoadEngineModel(rule.ResourceName().c_str());if(!sprite || sprite->type!=mod_sprite || sprite->numframes<1)return;
        Effect e;e.owner=entity;e.ownerModel=entity->model;e.sprite=sprite;e.rule=rule;e.sequence=entity->curstate.sequence;e.birth=state.time;e.position=state.attachments[rule.Attachment];
        if(rule.RandomFrame)e.first=engine.pfnRandomLong(0,sprite->numframes-1);
        if(rule.Rotation==1 || rule.Rotation==2)e.angle=engine.pfnRandomLong(0,rule.Rotation==1?20:359)*3.14159265f/180;
        effects.push_back(std::move(e));++eventCount;
    }
    catch(const std::exception& error){engine.Con_Printf("CSOLSupport: event: %s\n",error.what());}
}
void DrawWorldEffects(){if(enabled && enabled->value && EnsureGL()){GLState save;DrawBeamgunTracers();DrawCannonSmoke();DrawScenarioSmoke();}}
bool EnsureGraphics(){return EnsureGL();}
void InitializeScene()
{
    enabled=engine.pfnRegisterVariable("csol_enable","1",FCVAR_ARCHIVE);InitializeHands();
    // HookUserMsg only replaces an existing message; CsolPart is a new message.
    engine.pfnHookUserMsg("CsolPart",PartMessage);
    engine.pfnHookUserMsg("CsolView",ViewMessage);
    wchar_t path[32768]{};const auto size=GetEnvironmentVariableW(L"CSOL_DIAGNOSTIC_PATH",path,DWORD(std::size(path)));
    if(size>0 && size<std::size(path))diagnosticPath=path;
}
void ClearScene()
{
    if(postRenderer)postRenderer->UnregisterPostSceneCallbacks(&worldCallbacks);
    postRenderer=nullptr;postFrame=nullptr;worldDraws={};
    states.clear();effects.clear();failures.clear();parts={};
    ResetStockHands();
    for(auto& weapon:worldWeapons)weapon={};
    pendingParts={};pendingView={};viewState.reset();presentationModel=nullptr;nextDiagnostic=0;
    const bool deleteGraphics=glewReady && graphicsContext && graphicsContext==wglGetCurrentContext();
    if(deleteGraphics)ResetBeamScope();
    ClearAssets(deleteGraphics);
    if(deleteGraphics){if(program)glDeleteProgram(program);if(quadVAO)glDeleteVertexArrays(1,&quadVAO);if(quadVBO)glDeleteBuffers(1,&quadVBO);}
    program=quadVAO=quadVBO=0;glewReady=false;graphicsContext=nullptr;renderer=nullptr;draws=eventCount=0;partMessages=viewMessages=rejectedMessages=0;
    ResetBeamScope();
}
void ForgetSceneEntity(const cl_entity_t* entity)
{
    states.erase(entity);
    std::erase_if(effects,[&](const Effect& e){return e.owner==entity;});
    for(auto& draw:worldDraws)if(draw.entity==entity)draw={};
}
void SceneFrame()
{
    PruneBeamEffects();
    int frame;double time,old;studio.GetTimes(&frame,&time,&old);
    for(auto& weapon:worldWeapons)if(weapon.source && (weapon.frame+2<frame || frame<weapon.frame))
    {ForgetSceneEntity(&weapon.entity);weapon={};}
    // Explicitly enabled local test diagnostics; no per-frame file I/O in normal play.
    if(diagnosticPath.empty())return;
    const double now=engine.GetClientTime();if(now<nextDiagnostic)return;nextDiagnostic=now+.1;
    const auto* ent=engine.GetViewModel();const auto found=states.find(ent);const State* state=found==states.end()?nullptr:&found->second;
    std::ofstream file(diagnosticPath,std::ios::trunc);if(!file)return;
    file<<"{\"time\":"<<now<<",\"draws\":"<<draws<<",\"events\":"<<eventCount<<",\"effects\":"<<effects.size()
        <<",\"failures\":"<<failures.size()<<",\"partMessages\":"<<partMessages<<",\"viewMessages\":"<<viewMessages<<",\"rejectedMessages\":"<<rejectedMessages
        <<",\"skin\":"<<(viewState?int(viewState->Skin):ent?int(ent->curstate.skin):-1)<<",\"sequence\":"<<(state?state->sequence:-1)<<",\"parts\":[";
    for(size_t i=0;i<parts.slots.size();++i){if(i)file<<',';file<<(parts.slots[i].model?parts.slots[i].sequence:-1);}
    file<<"],\"bodyGroups\":[";
    if(state)for(size_t i=0;i<state->body.Groups.size();++i){if(i)file<<',';const auto& group=state->body.Groups[i];file<<'['<<group.Body<<','<<group.Alpha<<']';}
    file<<']';WeaponDiagnostics(file);CameraDiagnostics(file);ProjectileDiagnostics(file);CharacterDiagnostics(file);PlayerAnimationDiagnostics(file);StockHandDiagnostics(file);BuyMenuDiagnostics(file);
    const auto* hands=ViewCharacter() && state && state->asset?state->asset:nullptr;
    const int handBody=ent?ent->curstate.body:0;
    file<<",\"handAdapter\":{\"meshes\":"<<(hands?hands->adaptedMeshes:0)<<",\"bones\":"<<(hands?hands->adaptedBones:0)
        <<",\"body\":"<<handBody<<",\"conditionalGroups\":"<<(hands?hands->handBodygroups.size():0)
        <<",\"submodel\":"<<(hands && !hands->handBodygroups.empty()?CharacterHandSubmodel(*hands,hands->Header().numbodyparts-1,0,handBody):-1)<<'}';file<<'}';
}
void Report(){engine.Con_Printf("CSOLSupport independent DLL: assets=%u draws=%d events=%d active=%u rendererAPI=%d partMessages=%d viewMessages=%d rejected=%d\n",unsigned(AssetCount()),draws,eventCount,unsigned(effects.size()),renderer!=nullptr,partMessages,viewMessages,rejectedMessages);}
}
