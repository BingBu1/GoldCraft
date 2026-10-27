// SPDX-License-Identifier: GPL-3.0-or-later
// Engine services for testing the actual BeamEffect receiver and draw callback.
std::vector<std::array<float,6>> beamVertices;
std::vector<int> beamFrames;
std::array<float,3> beamStyle{};
model_t beamSprite{};
bool beamMissing{},beamTextureMissing{};
int beamMatrixCalls{},beamRenderMode{},beamCull{};
triangleapi_t beamTriangles{};
void ClearBeamCapture(){beamVertices.clear();beamFrames.clear();beamMatrixCalls=0;}
void SetupBeamCapture()
{
    ResetBeamgun();ClearBeamCapture();view.frametime=0;testThirdPerson=0;
    beamMissing=beamTextureMissing=false;beamSprite={};beamSprite.type=mod_sprite;beamSprite.numframes=20;
    players={};weaponView={};players[1].index=1;
    beamTriangles={};engine.pTriAPI=&beamTriangles;engine.pEfxAPI=nullptr;
    engine.GetLocalPlayer=[]()->cl_entity_t*{return &players[1];};
    engine.GetViewModel=[]()->cl_entity_t*{return &weaponView;};
    engine.GetEntityByIndex=[](int index)->cl_entity_t*{return index>0&&index<33?&players[index]:nullptr;};
    engine.CL_LoadModel=[](const char* path,int* index)->model_t*{effectPaths.emplace_back(path);*index=1;return beamMissing?nullptr:&beamSprite;};
    beamTriangles.GetMatrix=[](const int name,float* output){
        ++beamMatrixCalls;std::fill_n(output,16,0.f);
        // Camera along +X, right +Y, up +Z. The resulting clip w is X.
        if(name==GL_MODELVIEW_MATRIX){output[4]=output[9]=1;output[2]=-1;output[15]=1;}
        else{output[0]=output[5]=output[10]=1;output[11]=-1;}
    };
    beamTriangles.RenderMode=[](int mode){beamRenderMode=mode;};
    beamTriangles.CullFace=[](TRICULLSTYLE cull){beamCull=cull;};
    beamTriangles.Begin=[](int primitive){Check(primitive==TRI_QUADS);};
    beamTriangles.End=[](){};
    beamTriangles.Color4f=[](float r,float g,float b,float a){Check(r==1&&g==1&&b==1);beamStyle[2]=a;};
    beamTriangles.TexCoord2f=[](float u,float v){beamStyle[0]=u;beamStyle[1]=v;};
    beamTriangles.Vertex3f=[](float x,float y,float z){beamVertices.push_back({x,y,z,beamStyle[0],beamStyle[1],beamStyle[2]});};
    beamTriangles.SpriteTexture=[](model_t* model,int frame)->int{Check(model==&beamSprite);beamFrames.push_back(frame);return !beamTextureMissing;};
}
BeamEffect RenderEffect(int weapon=726,int mode=3,int phase=0)
{
    return {weapon,1,mode,phase,0,{}, {100,0,10},{400,20,10}};
}
void SendRenderEffect(const BeamEffect& effect)
{
    const auto bytes=EncodeBeamEffect(effect);
    Check(messages[BeamEffectMessage](BeamEffectMessage,int(bytes.size()),const_cast<std::byte*>(bytes.data()))==1);
}
void EndBeamCapture()
{
    ResetBeamgun();engine.pTriAPI=nullptr;engine.GetViewModel=nullptr;engine.GetLocalPlayer=nullptr;
    testThirdPerson=0;
}
void BeamRenderChecks()
{
    for(int weapon:{725,726})for(int mode:{0,3})
    {
        SetupBeamCapture();now=17.137f;
        auto effect=RenderEffect(weapon,mode,mode==0?4:0);SendRenderEffect(effect);
        Check(effectPaths[effectPaths.size()-2]==(weapon==726?"sprites/ef_beamgunle_charge_shootA.spr":"sprites/ef_beamgun_charge_shootA.spr"));
        DrawBeamgunTracers();Check(!beamVertices.empty());
        Check(beamFrames==std::vector<int>{0}&&beamMatrixCalls==2);
        Check(beamVertices.size()==96&&beamVertices[0][5]==0&&beamVertices.back()[5]==1);
        Check(std::abs(beamVertices.back()[4]-.96f)<1e-5f);
        Check(beamRenderMode==kRenderNormal&&beamCull==TRI_FRONT);
        const auto first=beamVertices;ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices==first);
        now+=.1f;view.frametime=.1f;ClearBeamCapture();DrawBeamgunTracers();
        Check(beamFrames==std::vector<int>{4});Check(beamVertices.back()[5]==1);
        const auto second=beamVertices;ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices==second);
        now+=.39f;view.frametime=.39f;ClearBeamCapture();DrawBeamgunTracers();Check(std::abs(beamVertices.back()[5]-.4f)<.001f);
        now+=.02f;ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices.empty());
        now=80;SendRenderEffect(effect);ClearBeamCapture();DrawBeamgunTracers();Check(!beamVertices.empty());
        now=79;ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices.empty());
        for(int action=0;action<2;++action){now=100;SendRenderEffect(effect);if(action)BeamgunVideoInit();else ResetBeamgun();ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices.empty());}
        // No large-beam path on an ordinary or small-explosion A event.
        effect.mode=0;for(int phase:{0,1}){effect.phase=phase;SendRenderEffect(effect);ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices.empty());}
    }
    SetupBeamCapture();now=100;auto effect=RenderEffect();
    for(int invalid=0;invalid<3;++invalid){beamMissing=invalid==0;beamSprite.type=invalid==1?mod_studio:mod_sprite;beamSprite.numframes=invalid==2?0:20;
        SendRenderEffect(effect);ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices.empty());}
    beamMissing=false;beamSprite.type=mod_sprite;beamSprite.numframes=20;
    SendRenderEffect(effect);beamTextureMissing=true;DrawBeamgunTracers();Check(beamVertices.empty());
    beamTextureMissing=false;DrawBeamgunTracers();Check(!beamVertices.empty());
    // Resolve attachment0 from the render entity selected for this observer.
    for(int context=0;context<3;++context){ResetBeamgun();ClearBeamCapture();players[1].model=players[2].model=weaponView.model=&effectModel;
        players[1].attachment[0][0]=120;players[2].attachment[0][0]=140;weaponView.attachment[0][0]=160;
        effect.owner=context==2?2:1;testThirdPerson=context==1;SendRenderEffect(effect);DrawBeamgunTracers();
        Check(!beamVertices.empty());Check(beamVertices[0][0]==(context==0?160:context==1?120:140));}
    SetupBeamCapture();now=100;effect=RenderEffect();
    for(int i=0;i<70;++i)SendRenderEffect(effect);
    DrawBeamgunTracers();Check(beamFrames.size()==64&&beamVertices.size()==64*96&&beamMatrixCalls==2);
    now=101;ClearBeamCapture();DrawBeamgunTracers();Check(beamVertices.empty());
    SendRenderEffect(effect);ClearBeamCapture();DrawBeamgunTracers();Check(beamFrames.size()==1);
    EndBeamCapture();
}
void BeamRenderOracle(char** args)
{
    SetupBeamCapture();now=float(std::atof(args[2]));
    auto effect=RenderEffect(std::atoi(args[7]));
    for(int n=0;n<3;++n)effect.end[n]=effect.origin[n]+float(std::atof(args[4+n]));
    SendRenderEffect(effect);
    const float birth=now;now+=float(std::atof(args[3]));view.frametime=float(std::atof(args[8]));
    DrawBeamgunTracers();
    std::cout<<std::setprecision(9)<<"{\"birth\":"<<birth<<",\"now\":"<<now<<",\"frames\":[";
    for(std::size_t i=0;i<beamFrames.size();++i){if(i)std::cout<<',';std::cout<<beamFrames[i];}
    std::cout<<"],\"vertices\":[";
    for(std::size_t i=0;i<beamVertices.size();++i){if(i)std::cout<<',';std::cout<<'[';for(int n=0;n<6;++n){if(n)std::cout<<',';std::cout<<beamVertices[i][n];}std::cout<<']';}
    std::cout<<"]}\n";EndBeamCapture();
}
