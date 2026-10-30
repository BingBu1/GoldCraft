// SPDX-License-Identifier: GPL-3.0-or-later
// Actual client receiver/uploader; the engine command and renderer boundaries
// are captured. No fake process is presented as multiplayer gameplay evidence.
#include "../../native/csol/client/hd_spray.cpp"
#include <iostream>
namespace csol::client {
cl_enginefunc_t engine{};
unsigned drawn{},removed{};spray::Dimensions drawnSize;
void SynchronizeSession(){}
void SprayVideoReset(){drawn=removed=0;}
void SprayOptionsFrame(){}
void PrepareSprayGeometry(){}
void ReleaseSprayOptions(){}
void ShutdownSprayGraphics(){}
void AddSprayGeometry(const SprayPlacement& p,std::shared_ptr<const spray::Animation>){++drawn;drawnSize=p.original;}
void RemoveSprayGeometry(unsigned){++removed;}
}
using namespace csol;
using namespace csol::client;
unsigned checks{};std::vector<std::string> commands;
void require(bool b){++checks;if(!b)throw std::runtime_error("HD spray client check "+std::to_string(checks));}
int SendCommand(const char* s){require(std::strlen(s)<=127);commands.emplace_back(s);return 1;}
void Log(const char*,...){}
spray::Bytes Packet(spray::MessageType type,unsigned session=100){spray::Bytes b{type};spray::Put32(b,session);return b;}
void Receive(spray::Bytes b){require(Message("CSHDSpray",int(b.size()),b.data())==1);}
void Asset(unsigned id,const spray::Bytes& encoded){
    auto begin=Packet(spray::Begin);spray::Put32(begin,id);spray::Put32(begin,unsigned(encoded.size()));spray::Put32(begin,spray::Checksum(encoded));Receive(begin);
    for(size_t at=0;at<encoded.size();at+=spray::Chunk){auto b=Packet(spray::Data);spray::Put32(b,id);spray::Put32(b,unsigned(at));b.insert(b.end(),encoded.begin()+at,encoded.begin()+std::min(encoded.size(),at+spray::Chunk));Receive(b);}
}
void Place(unsigned asset,unsigned id=1,spray::Dimensions original={1024,1024}){auto b=Packet(spray::Placement);for(auto v:{id,asset,0u,1u})spray::Put32(b,v);for(float f:{10.f,20.f,30.f,1.f,0.f,0.f})spray::PutFloat(b,f);spray::Put32(b,original.width);spray::Put32(b,original.height);spray::PutFloat(b,12.5f);Receive(b);}
int main(){try{
    engine.pfnServerCmd=SendCommand;engine.Con_DPrintf=Log;
    auto hello=Packet(spray::Hello);hello.push_back(spray::Version);Receive(hello);require(epoch==100&&commands.size()==1);
    const auto encoded=spray::Encode(Image{512,512,spray::Bytes(512*512*4,255)});
    auto begin=Packet(spray::Begin);spray::Put32(begin,7);spray::Put32(begin,unsigned(encoded.size()));spray::Put32(begin,spray::Checksum(encoded));Receive(begin);
    for(size_t at=0;at<encoded.size();at+=spray::Chunk){auto data=Packet(spray::Data);spray::Put32(data,7);spray::Put32(data,unsigned(at));data.insert(data.end(),encoded.begin()+at,encoded.begin()+std::min(encoded.size(),at+spray::Chunk));Receive(data);}
    require(images.contains(7));Place(7);require(drawn==1);Place(7);require(drawn==1);Place(99,2);require(drawn==1);
    require(drawnSize.width==1024&&drawnSize.height==1024);
    Place(7,2,{0,512});Place(7,2,{32769,32769});Place(7,2,{1024,512});require(drawn==1);
    auto stale=Packet(spray::Remove,99);spray::Put32(stale,1);Receive(stale);require(removed==0);
    auto remove=Packet(spray::Remove);spray::Put32(remove,1);Receive(remove);require(removed==1&&placements.empty());
    Receive(begin);auto bad=Packet(spray::Data);spray::Put32(bad,7);spray::Put32(bad,1);bad.push_back(0);Receive(bad);require(!receiving.id);
    auto giant=Packet(spray::Begin);spray::Put32(giant,9);spray::Put32(giant,spray::MaxBytes+1);spray::Put32(giant,0);Receive(giant);require(!receiving.id);
    // Upload must pause after 12 commands until a real server acknowledgement.
    settings={true,encoded,{2048,2048}};dirty=true;nextSend=0;SprayFrame();require(uploading&&uploadId==0);
    require(commands.back().find(" 2048 2048\n")!=std::string::npos);
    auto assigned=Packet(spray::UploadResult);assigned.push_back(2);spray::Put32(assigned,8);Receive(assigned);
    commands.clear();for(int i=0;i<8;++i){nextSend=0;SprayFrame();}require(sent==spray::UploadChunk*12&&commands.size()==12);
    auto ack=Packet(spray::UploadResult);ack.push_back(3);spray::Put32(ack,8);spray::Put32(ack,unsigned(sent));Receive(ack);nextSend=0;SprayFrame();require(sent==spray::UploadChunk*15);
    require(IsSprayUploading());CancelSprayUpload();require(!IsSprayUploading()&&!dirty&&uploadBytes.empty()&&settings.bytes==encoded&&settings.enabled);
    require(commands.back().find(" off 100")!=std::string::npos);const auto cancelled=std::wstring(SprayStatus());
    auto late=Packet(spray::UploadResult);late.push_back(1);Receive(late);require(std::wstring(SprayStatus())==cancelled);
    ResetSpray();require(!epoch&&images.empty()&&placements.empty()&&!uploading&&settings.enabled);
    // Persistence is tested through the production encoder/decoder, without
    // touching any running client's settings. The transport texture is512px.
    auto saved=EncodeSettings(NormalizeSettings(settings));auto loaded=DecodeSettings(saved);
    require(loaded.enabled&&loaded.original.width==2048&&loaded.original.height==2048&&loaded.bytes==encoded);
    spray::Bytes legacy{1};legacy.insert(legacy.end(),encoded.begin(),encoded.end());loaded=DecodeSettings(legacy);
    require(loaded.original.width==512&&loaded.original.height==512&&loaded.bytes==encoded);
    const auto disabled=DecodeSettings(spray::Bytes{0});require(!disabled.enabled&&disabled.bytes.empty());
    for(auto broken:{spray::Bytes{2},spray::Bytes{2,3,0,0,0,0,0,0,0,0}}){bool rejected=false;try{DecodeSettings(broken);}catch(const std::exception&){rejected=true;}require(rejected);}
    auto mismatched=settings;mismatched.original={2048,1024};bool rejected=false;try{NormalizeSettings(mismatched);}catch(const std::exception&){rejected=true;}require(rejected);
    saved[0]=2;loaded=DecodeSettings(saved);require(loaded.original.width==2048&&loaded.bytes==encoded);
    spray::Animation animation{{{Image{512,512,spray::Bytes(512*512*4,255)},100},{Image{512,512,spray::Bytes(512*512*4,0)},250}},3};
    const auto animated=spray::EncodeAnimation(animation);settings={true,animated,{2048,2048}};
    saved=EncodeSettings(NormalizeSettings(settings));require(saved[0]==3);loaded=DecodeSettings(saved);auto restored=spray::DecodeAnimation(loaded.bytes);
    require(loaded.original.width==2048&&restored.plays==3&&restored.frames.size()==2&&restored.frames[1].duration==250);
    Receive(hello);Asset(10,animated);Place(10,1,{2048,2048});require(drawn==1&&placements.back().started==12.5f&&images.at(10)->frames.size()==2);
    require(spray::FrameAt(*images.at(10),12.7-placements.back().started)==1);
    ResetSpray();Receive(hello);
    spray::Animation large;for(int i=0;i<32;++i)large.frames.push_back({animation.First(),100});const auto largeBytes=spray::EncodeAnimation(large);
    for(unsigned i=1;i<=4;++i){Asset(i,largeBytes);Place(i,i);require(images.size()==i);}
    Asset(5,largeBytes);require(!images.contains(5)&&!receiving.id&&commands.back().find(" skip 100 5")!=std::string::npos);
    ResetSpray();require(images.empty()&&placements.empty());
    std::cout<<checks<<" actual-client HD spray protocol checks passed; live UI/network pending\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
