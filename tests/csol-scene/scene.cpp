// SPDX-License-Identifier: GPL-3.0-or-later
// Production scene.cpp and shaders execute on a real hidden GL context.
// Engine/asset fixtures supply poses and a small two-skin material mesh.
#include "../../native/csol/client/scene.cpp"
#include "../../native/csol/client/beamgun_scope.cpp"
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <source_location>
namespace fixture {
using namespace csol::client;
int targetTag,frame=1,checks,poses,lightingCalls,eventCalls,geometryCalls;
double sceneTime=1;
FBO_Container_t* target=reinterpret_cast<FBO_Container_t*>(&targetTag);
std::array<float,16> world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},projection=world;
cl_entity_t* current{};
cl_entity_t* local{};
bool thirdPerson{},cannonEventHandled{};
int cannonEventCalls{},nativeEventCalls{};
cl_entity_t entity{},viewmodel{},ordinary{};
model_t model{},ordinaryModel{},sprite{};
Asset asset;
Bones bones{};
unsigned identity=1;
EngineSpriteFrame spriteFrames[2]{};
void check(bool value,const std::source_location where=std::source_location::current())
{++checks;if(!value){std::fprintf(stderr,"FAIL scene line %u GL %x\n",where.line(),glGetError());std::exit(1);}}
int pose(int flags){check(flags==0);check(entity.curstate.scale==1);++poses;return 1;}
void console(const char* fmt,...){std::fprintf(stderr,"unexpected console: %s\n",fmt);std::abort();}
}
#include "renderer_fixture.hpp"
namespace csol::client {
metahook_api_t* api{};mh_interface_t* interfaces{};
cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};engine_studio_api_t studio{};
r_studio_interface_t* studioInterface{};ref_params_t view{};bool glewReady{};int* hostTextureCache{};
void* currentStudioHeader{};model_t* currentStudioModel{};
Asset::~Asset(){}
Asset* GetAsset(model_t* model){return model==&fixture::model?&fixture::asset:nullptr;}
Asset* ViewHandAsset(Asset* asset){return asset;}
std::pair<bool,Asset*> CachedAsset(model_t* model){return {true,GetAsset(model)};}
EngineSpriteFrame* SpriteFrameAt(model_t* model,int index,double){return model==&fixture::sprite?&fixture::spriteFrames[index%2]:nullptr;}
void ClearAssets(bool){} size_t AssetCount(){return 1;}
std::vector<std::byte> ReadResource(const char*){throw std::runtime_error("unexpected fixture resource read");}
GLuint UploadImage(const Image&){std::abort();}
void InitializeHands(){}void UpdateHands(){}
void ApplyHand(const Material&,GLuint&,float&,float&,float*){std::abort();}
model_t* LoadEngineModel(const char*){return nullptr;}
int BeamgunViewSkin(const cl_entity_t*){return -1;}
unsigned BeamEffectIdentity(const cl_entity_t* entity){return entity==&fixture::entity?fixture::identity:0;}
void PruneBeamEffects(){}void DrawBeamgunTracers(){}void DrawCannonSmoke(){}
bool CannonStudioEvent(const mstudioevent_t*,const cl_entity_t*){++fixture::cannonEventCalls;return fixture::cannonEventHandled;}
void WeaponDiagnostics(std::ostream&){}void CameraDiagnostics(std::ostream&){}void ProjectileDiagnostics(std::ostream&){}
void CharacterDiagnostics(std::ostream&){}void PlayerAnimationDiagnostics(std::ostream&){}
const Character* ViewCharacter(){return nullptr;}
int CharacterHandSubmodel(const Asset&,int,int original,int){return original;}
int CameraIsThirdPerson(){return fixture::thirdPerson;}
void EnsureGeometry(Asset& a)
{
    if(a.vao)return;++fixture::geometryCalls;
    Vertex vertices[4]{};
    const float xy[4][2]={{-.8f,-.8f},{.8f,-.8f},{.8f,.8f},{-.8f,.8f}};
    for(int i=0;i<4;++i){vertices[i].position[0]=xy[i][0];vertices[i].position[1]=xy[i][1];vertices[i].normal[2]=1;}
    const unsigned indices[]={0,1,2,0,2,3};
    glGenVertexArrays(1,&a.vao);glBindVertexArray(a.vao);glGenBuffers(1,&a.vbo);glBindBuffer(GL_ARRAY_BUFFER,a.vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glGenBuffers(1,&a.ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,a.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    for(int i=0;i<3;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==2?2:3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(size_t(i)*12));}
    glEnableVertexAttribArray(3);glVertexAttribIPointer(3,2,GL_INT,sizeof(Vertex),(void*)offsetof(Vertex,bone));
    a.batches={{0,6,0,0,0}};
}
}
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;using namespace fixture;
    check(glfwInit()!=0);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);auto* window=glfwCreateWindow(32,32,"CSOL scene regression",nullptr,nullptr);check(window!=nullptr);
    glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;check(glewInit()==GLEW_OK);while(glGetError()){}
    glewReady=true;graphicsContext=wglGetCurrentContext();RendererFixture host;renderer=&host;
    struct Post final:IMetaRendererPostScene {bool RegisterPostSceneCallbacks(IMetaRendererPostSceneCallbacks*)noexcept override{return true;}bool UnregisterPostSceneCallbacks(IMetaRendererPostSceneCallbacks*)noexcept override{return true;}} post;
    postRenderer=&post;cvar_t enable{};enable.value=1;enabled=&enable;
    engine.GetViewModel=[](){return &viewmodel;};engine.GetClientTime=[]()->float{return float(sceneTime);};
    engine.GetLocalPlayer=[](){return local;};
    downstream.HUD_StudioEvent=[](const mstudioevent_t*,const cl_entity_t*){++nativeEventCalls;};
    mstudioevent_t numeric{};numeric.event=5001;std::strcpy(numeric.options,"103112");
    cannonEventHandled=true;StudioEvent(&numeric,&viewmodel);check(cannonEventCalls==1&&nativeEventCalls==0);
    cannonEventHandled=false;StudioEvent(&numeric,&viewmodel);check(cannonEventCalls==2&&nativeEventCalls==1);
    numeric.event=5004;std::strcpy(numeric.options,"weapons/cannon_draw.wav");StudioEvent(&numeric,&viewmodel);check(cannonEventCalls==3&&nativeEventCalls==2);
    StudioEvent(nullptr,&viewmodel);check(cannonEventCalls==4&&nativeEventCalls==3);
    engine.pfnGetCvarPointer=[](const char*)->cvar_t*{return nullptr;};
    engine.Con_Printf=console;
    studio.GetTimes=[](int* f,double* t,double* old){*f=frame;*t=sceneTime;*old=sceneTime-.01;};
    studio.StudioGetBoneTransform=[]()->float****{return reinterpret_cast<float****>(bones.data());};
    studio.StudioGetLightTransform=studio.StudioGetBoneTransform;
    studio.StudioDynamicLight=[](cl_entity_t* e,alight_t* l){check(current==e);++lightingCalls;l->ambientlight=255;l->color[0]=l->color[1]=l->color[2]=1;};
    studio.StudioEntityLight=[](alight_t*){};studio.GetCurrentEntity=[](){return current;};
    studio.Mod_Extradata=[](model_t* m)->void*{check(m==&model);return asset.bytes.data();};
    studio.StudioSetHeader=[](void* h){currentStudioHeader=h;};studio.SetRenderModel=[](model_t* m){currentStudioModel=m;};
    studio.GetModelByIndex=[](int)->model_t*{return &model;};
    studio.StudioClientEvents=[](){check(current==&entity);++eventCalls;};
    bones[0][0][0]=bones[0][1][1]=bones[0][2][2]=1;
    asset.bytes.resize(sizeof(studiohdr_t)+sizeof(mstudiobodyparts_t)+sizeof(mstudioseqdesc_t)+4);
    auto* h=reinterpret_cast<studiohdr_t*>(asset.bytes.data());h->numbones=1;h->numbodyparts=1;h->numskinref=1;h->numskinfamilies=2;h->numseq=1;
    h->bodypartindex=sizeof(*h);h->seqindex=h->bodypartindex+sizeof(mstudiobodyparts_t);h->skinindex=h->seqindex+sizeof(mstudioseqdesc_t);
    auto* group=reinterpret_cast<mstudiobodyparts_t*>(asset.bytes.data()+h->bodypartindex);group->base=group->nummodels=1;
    auto* seq=reinterpret_cast<mstudioseqdesc_t*>(asset.bytes.data()+h->seqindex);seq->numframes=2;seq->fps=30;
    reinterpret_cast<short*>(asset.bytes.data()+h->skinindex)[1]=1;
    entity.model=&model;ordinary.model=&ordinaryModel;entity.curstate.scale=.5f;entity.curstate.framerate=1;current=&entity;
    auto texture=[](std::array<unsigned char,4> color){GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,color.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);return t;};
    Material material;material.width=material.height=1;material.special=true;material.cull=false;material.blend=Blend::Alpha;material.texture=texture({255,0,0,128});
    asset.materials.push_back(material);material.texture=texture({0,255,0,128});asset.materials.push_back(material);
    GLuint fbo,color,depth;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    color=texture({0,0,0,0});glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glGenTextures(1,&depth);glBindTexture(GL_TEXTURE_2D,depth);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH24_STENCIL8,32,32,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,32,32);
    MetaRendererPostSceneFrame descriptor{};descriptor.struct_size=sizeof(descriptor);descriptor.flags=META_POST_SCENE_GAMMA;descriptor.frame_number=frame;
    std::copy(world.begin(),world.end(),descriptor.world_matrix);std::copy(projection.begin(),projection.end(),descriptor.projection_matrix);
    auto pixel=[](){std::array<unsigned char,4> p{};glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;};
    auto clear=[&](double z=1){glDepthMask(1);glStencilMask(255);glClearColor(0,0,1,1);glClearDepth(z);glClearStencil(73);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);};
    auto queue=[&](){current=&entity;check(DrawModel(&entity,STUDIO_RENDER|STUDIO_EVENTS,pose));};
    check(!DrawModel(&ordinary,STUDIO_RENDER,pose));check(poses==0);
    clear();queue();check(geometryCalls==1&&entity.curstate.scale==.5f);check(std::abs(states[&entity].bones[0][0][0]-.5f)<1e-6f);
    const int lightBefore=lightingCalls;current=&ordinary;ordinaryModel.type=mod_brush;
    currentStudioHeader=nullptr;currentStudioModel=&ordinaryModel;
    glEnable(GL_STENCIL_TEST);glStencilFunc(GL_NEVER,0,255);glDepthFunc(GL_GREATER);glDepthMask(1);
    DrawOwnedWorld(descriptor);auto p=pixel();check(p[0]>=127&&p[0]<=129&&p[2]>=126&&p[2]<=128);check(lightingCalls==lightBefore&&eventCalls==1&&current==&ordinary);
    check(currentStudioHeader==nullptr && currentStudioModel==&ordinaryModel);
    GLint function;GLboolean mask;glGetIntegerv(GL_DEPTH_FUNC,&function);glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);check(function==GL_GREATER&&mask&&glIsEnabled(GL_STENCIL_TEST));
    float z;unsigned char stencil;glReadPixels(16,16,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&z);glReadPixels(16,16,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);check(z==1&&stencil==73);
    glDisable(GL_STENCIL_TEST);clear(.25);queue();DrawOwnedWorld(descriptor);check(pixel()[0]==0&&pixel()[2]==255&&eventCalls==1);
    // A queued reflection/other view is never emitted in the main post scene.
    clear();world[12]=10;queue();world[12]=0;DrawOwnedWorld(descriptor);check(pixel()[0]==0);
    clear();queue();++identity;DrawOwnedWorld(descriptor);check(pixel()[0]==0);ForgetSceneEntity(&entity);check(!states.contains(&entity));
    // Reused entity and changed skin: fresh pose/event state, green alpha material.
    ++frame;sceneTime+=.1;descriptor.frame_number=frame;entity.curstate.skin=1;queue();clear();DrawOwnedWorld(descriptor);p=pixel();check(p[0]==0&&p[1]>=127&&p[2]>=126&&eventCalls==2);
    check(geometryCalls==1);check(glGetError()==GL_NO_ERROR);
    // Charge bindSpr follows the selected skin and advances the real shader's
    // sampled texture; all-zero alpha stays invisible instead of a black card.
    asset.config.Sprites.push_back({1,"fixture.spr",30,{{0,true,false,0,0,0,1}}});
    sprite.numframes=3;asset.sprites.push_back(&sprite);asset.materials[1].sprite=0;
    spriteFrames[0].texture=texture({0,0,0,0});spriteFrames[1].texture=texture({255,0,0,255});
    ForgetSceneEntity(&entity);++frame;sceneTime+=.1;descriptor.frame_number=frame;clear();queue();DrawOwnedWorld(descriptor);check(pixel()[0]==0&&pixel()[2]==255);
    ++frame;sceneTime+=1.1/30;descriptor.frame_number=frame;clear();queue();DrawOwnedWorld(descriptor);check(pixel()[0]==255&&pixel()[2]==0);
    check(eventCalls==4);check(glGetError()==GL_NO_ERROR);
    // The ordinary brush context above must never be passed to Mod_Extradata.
    // A dropped special weapon uses its real host pose; ordinary models still
    // fall through. Reuse/model changes cancel a queued world-weapon draw.
    ForgetSceneEntity(&entity);identity=0;model.type=mod_studio;std::strcpy(model.name,"models/w_fixture.mdl");asset.special=true;
    entity.curstate.skin=0;entity.curstate.scale=1;current=&entity;currentStudioModel=&model;
    ++frame;sceneTime+=.1;descriptor.frame_number=frame;clear();
    check(DrawWorldWeaponPoints());DrawOwnedWorld(descriptor);check(pixel()[0]>=127);
    ++frame;sceneTime+=.1;descriptor.frame_number=frame;clear();current=&entity;
    check(DrawWorldWeaponPoints());entity.model=&ordinaryModel;DrawOwnedWorld(descriptor);check(pixel()[0]==0&&pixel()[2]==255);entity.model=&model;
    // Opaque world geometry must write depth, while transparent layers do not.
    asset.materials[0].blend=Blend::Opaque;
    ++frame;sceneTime+=.1;descriptor.frame_number=frame;clear();current=&entity;
    check(DrawWorldWeaponPoints());DrawOwnedWorld(descriptor);
    float depthValue;glReadPixels(16,16,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depthValue);check(depthValue<.51f);
    // CS sets only the header for a held model; SetRenderModel stays on the
    // player. The player's own header must never be mistaken for that weapon.
    std::strcpy(model.name,"models/p_fixture.mdl");ordinaryModel.type=mod_studio;
    entity.model=&ordinaryModel;entity.curstate.weaponmodel=1;currentStudioModel=&ordinaryModel;
    currentStudioHeader=nullptr;check(!DrawWorldWeaponPoints());
    currentStudioHeader=asset.bytes.data();++frame;sceneTime+=.1;descriptor.frame_number=frame;clear();
    check(DrawWorldWeaponPoints());DrawOwnedWorld(descriptor);check(pixel()[0]>=127);
    // Local first-person lower-body draws must fall through to the host's
    // clipping. Remote/third-person held weapons retain custom materials.
    local=&entity;clear();check(!DrawWorldWeaponPoints());DrawOwnedWorld(descriptor);
    check(pixel()[0]==0&&pixel()[2]==255);
    thirdPerson=true;clear();check(DrawWorldWeaponPoints());DrawOwnedWorld(descriptor);check(pixel()[0]>=127);
    clear();check(DrawWorldWeaponPoints());thirdPerson=false;DrawOwnedWorld(descriptor);
    check(pixel()[0]==0&&pixel()[2]==255); // queued before a same-frame view change
    local=&ordinary;clear();check(DrawWorldWeaponPoints());DrawOwnedWorld(descriptor);check(pixel()[0]>=127);
    local=nullptr;
    entity.curstate.weaponmodel=0;entity.model=&model;currentStudioModel=&model;
    // The viewmodel without a hand alias still reaches the real scoped event
    // route. Keep its native identity and header on repeated event draws.
    engine.GetViewModel=[](){return &entity;};current=&entity;
    h->length=int(asset.bytes.size());currentStudioHeader=h;
    const auto nativeModel=entity.model;const auto nativeIndex=entity.curstate.modelindex;
    const int eventsBefore=eventCalls;
    check(DrawModel(&entity,STUDIO_EVENTS,pose));
    check(DrawModel(&entity,STUDIO_EVENTS,pose));
    check(eventCalls==eventsBefore+2 && entity.model==nativeModel && entity.curstate.modelindex==nativeIndex && currentStudioHeader==h);
    engine.GetViewModel=[](){return &viewmodel;};
    // Real HUD shader draws once per frame; no VGUI retained polygon batch.
    attempted[1]=true;textures[1][0]=texture({255,0,0,128});
    clear();glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glEnable(GL_STENCIL_TEST);
    GLint programBefore;glGetIntegerv(GL_CURRENT_PROGRAM,&programBefore);
    DrawBeamScope(BeamgunLEId,32,32,0);p=pixel();check(p[0]>=127&&p[0]<=129&&p[2]>=126&&p[2]<=128);
    GLint programAfter;glGetIntegerv(GL_CURRENT_PROGRAM,&programAfter);glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
    check(programBefore==programAfter&&mask&&glIsEnabled(GL_STENCIL_TEST)&&glIsEnabled(GL_DEPTH_TEST));
    clear();DrawBeamScope(BeamgunLEId,32,32,0);check(pixel()==p);check(glGetError()==GL_NO_ERROR);
    ResetBeamScope();
    // Real 66x160 NPOT uploads with asymmetric transparent margins expose
    // padded-VGUI UVs that a solid 1x1 texture cannot detect.
    glDisable(GL_STENCIL_TEST);
    for(int resource=0;resource<(argc>1?2:1);++resource)for(int variant=0;variant<2;++variant)
    {
        std::array<std::array<unsigned char,4>,66*160> background{},fill{};
        for(int y=0;y<160;++y)for(int x=0;x<66;++x)
        {
            if(x>=9&&x<=57&&y>=6&&y<=154&&(x<13||x>53||y<10||y>150))
                background[y*66+x]={230,170,40,255};
            if(x>=14&&x<=52&&y>=11&&y<=149)
                fill[y*66+x]={static_cast<unsigned char>(variant?30:180),
                    static_cast<unsigned char>(40+y),static_cast<unsigned char>(60+x*2),255};
        }
        if(resource)
        {
            auto read=[&](const char* suffix,auto& pixels){
                const auto path=std::filesystem::path(argv[1])/(std::string(variant?"beamgunle_gauge":"beamgun_gauge")+suffix+".tga");
                std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file));
                std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));file.seekg(0);
                file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());check(bool(file));
                const auto image=DecodeTGA(bytes);check(image.Width==66&&image.Height==160);
                std::memcpy(pixels.data(),image.RGBA.data(),image.RGBA.size());
            };
            read("_bg",background);read("",fill);
        }
        attempted[variant]=true;
        auto upload=[&](const auto& data){auto id=texture({0,0,0,0});glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,66,160,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);return id;};
        glDeleteTextures(2,textures[variant].data()+3);
        textures[variant][3]=upload(background);textures[variant][4]=upload(fill);
        auto blend=[](auto& result,const auto& data,double u,double v){
            const double px=u*66-.5,py=v*160-.5;
            const int ix=int(std::floor(px)),iy=int(std::floor(py));const double fx=px-ix,fy=py-iy;
            std::array<double,4> sample{};
            for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx)
            {
                const auto& p=data[std::clamp(iy+yy,0,159)*66+std::clamp(ix+xx,0,65)];
                const double weight=(xx?fx:1-fx)*(yy?fy:1-fy);
                for(int c=0;c<4;++c)sample[c]+=weight*p[c];
            }
            for(int c=0;c<3;++c)result[c]=std::round(sample[c]*sample[3]/255+result[c]*(1-sample[3]/255));
        };
        for(const auto size:std::array<std::array<int,2>,3>{{{640,480},{1280,720},{1920,1080}}})
        {
            const int width=size[0],height=size[1];
            glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glBindTexture(GL_TEXTURE_2D,depth);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH24_STENCIL8,width,height,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,nullptr);
            glViewport(0,0,width,height);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
            const float scale=height>768?height/768.f:1.f;
            const int left=int(width*.5f-66*scale),right=width/2,top=int((height-160*scale)*.5f),bottom=int((height+160*scale)*.5f);
            std::vector<std::array<unsigned char,4>> pixels(size_t(width)*height);
            for(const float input:{0.f,.25f,.625f,1.f,2.f,-.5f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
            {
                const double ratio=std::isfinite(input)?std::clamp(double(input),0.,1.):0.;
                clear();DrawBeamScope(variant?BeamgunLEId:BeamgunId,width,height,input);
                glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                const int fillTop=int((height-160*scale)*.5f+(1-ratio)*160*scale);
                for(int y=top-2;y<bottom+2;++y)for(int x=left-2;x<right+2;++x)
                {
                    std::array<double,3> expected{0,0,255};
                    if(ratio>0&&x>=left&&x<right&&y>=top&&y<bottom)
                    {
                        const double u=(x+.5-left)/(right-left),v=(y+.5-top)/(bottom-top);
                        blend(expected,background,u,v);
                        if(y>=fillTop)
                        {
                            blend(expected,fill,u,(1-ratio)+(y+.5-fillTop)*ratio/(bottom-fillTop));
                        }
                    }
                    const auto actual=pixels[size_t(height-1-y)*width+x];
                    const bool matches=std::abs(actual[0]-expected[0])<=2&&std::abs(actual[1]-expected[1])<=2&&std::abs(actual[2]-expected[2])<=2;
                    if(!matches){std::fprintf(stderr,"scope resource=%d variant=%d size=%dx%d ratio=%g pixel=%d,%d actual=%d,%d,%d expected=%.0f,%.0f,%.0f\n",resource,variant,width,height,ratio,x,y,actual[0],actual[1],actual[2],expected[0],expected[1],expected[2]);}
                    check(matches);
                }
                check(glGetError()==GL_NO_ERROR);
            }
        }
    }
    ResetBeamScope();
    std::printf("PASS production scene GL %d checks; GPU=%s\n",checks,glGetString(GL_RENDERER));
    ClearScene();glfwDestroyWindow(window);glfwTerminate();return 0;
}
