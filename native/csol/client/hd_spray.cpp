// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "hd_spray.hpp"
#include "hd_spray_http.hpp"
#include <net_api.h>
#include <fstream>
#include <cmath>
#include <deque>
#include <set>
#include <sstream>

namespace csol::client {
namespace {
SpraySettings settings;
unsigned epoch{},uploadId{};size_t sent{},acked{};bool dirty{},uploading{};double nextSend{};
spray::Bytes uploadBytes;
spray::Transfer receiving;
spray_http::Worker httpWorker;std::shared_ptr<spray_http::Job> httpUpload,httpDownload;
spray::http::Endpoint httpEndpoint;bool httpAccepted{};unsigned protocol=spray::Version,httpRequest{};
double httpDeadline{};
auto decodedMemory=std::make_shared<std::atomic<size_t>>(0);
std::unordered_map<unsigned,std::shared_ptr<const spray::Animation>> images;
std::deque<SprayPlacement> placements;
std::wstring status=L"应用后上传至当前服务器";
SpraySettings NormalizeSettings(SpraySettings value){
    if(value.bytes.empty()){
        if(value.enabled)throw std::runtime_error("请先选择图片");
        value.original={};return value;
    }
    const auto image=spray::DecodeAnimation(value.bytes,false);
    // Legacy saves only retain the encoded dimensions. Do not invent the
    // discarded source size; reimporting the original records it exactly.
    if(!value.original.width&&!value.original.height)value.original={image.First().Width,image.First().Height};
    spray::CheckDimensions(value.original,image);return value;
}
spray::Bytes EncodeSettings(const SpraySettings& value){
    spray::Bytes bytes{3,static_cast<std::uint8_t>(value.enabled)};
    spray::Put32(bytes,value.original.width);spray::Put32(bytes,value.original.height);
    bytes.insert(bytes.end(),value.bytes.begin(),value.bytes.end());return bytes;
}
SpraySettings DecodeSettings(std::span<const std::uint8_t> bytes){
    spray::Reader r{bytes};SpraySettings value;const auto version=r.U8();
    if(version<=1)value.enabled=version;
    else {spray::Check(version==2||version==3);const auto enabled=r.U8();spray::Check(enabled<=1);value.enabled=enabled;value.original={r.U32(),r.U32()};}
    value.bytes.assign(bytes.begin()+r.at,bytes.end());return NormalizeSettings(std::move(value));
}
std::filesystem::path SettingsPath(){
    wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))throw std::runtime_error("Cannot locate game directory");
    const auto mod=std::filesystem::path(engine.pfnGetGameDirectory()).filename();
    return std::filesystem::path(executable).parent_path()/mod/L"csol"/L"hd-spray.dat";
}
void Command(const std::string& text){engine.pfnServerCmd(const_cast<char*>((text+"\n").c_str()));}
std::string Prefix(const char* verb){return std::string("csol_hd ")+verb+" "+std::to_string(epoch);}
void Collect(unsigned retain=0,size_t required=0){
    std::set<unsigned> live;for(const auto& d:placements)live.insert(d.asset);live.insert(uploadId);live.insert(retain);
    for(auto i=images.begin();i!=images.end();)if(!live.contains(i->first)&&(images.size()>spray::MaxDecals||decodedMemory->load()+required>spray::CacheBytes)){
        Command(Prefix("forget")+" "+std::to_string(i->first));i=images.erase(i);
    }else ++i;
}
void StoreImage(unsigned id,std::span<const std::uint8_t> bytes){
    if(images.contains(id))return;
    const auto info=spray::InspectAnimation(bytes);Collect(id,info.memory);
    images[id]=spray_http::Decode(bytes,spray_http::Reserve(decodedMemory,info.memory));
}
void StoreDecoded(unsigned id,std::shared_ptr<const spray::Animation> image){
    if(images.contains(id))return;spray::Check(image!=nullptr);images[id]=std::move(image);Collect(id);
}
std::shared_ptr<spray_http::Job> HttpJob(unsigned id,unsigned size,unsigned hash,bool upload,const std::string& token){
    spray::Check(httpEndpoint.port&&size>=22&&size<=spray::MaxBytes);Collect(id,size_t(spray::MaxPixels)*4);
    const size_t used=decodedMemory->load();
    spray::Check(used<spray::CacheBytes);
    auto job=std::make_shared<spray_http::Job>();job->epoch=epoch;job->id=id;job->size=size;job->checksum=hash;job->upload=upload;
    job->host=httpEndpoint.host;job->port=httpEndpoint.port;job->secure=httpEndpoint.secure;job->path=std::string(upload?"/spray/upload/":"/spray/image/")+token;
    job->decodedLimit=std::min(size_t(spray::MaxPixels)*4,size_t(spray::CacheBytes)-used);
    job->reservation=spray_http::Reserve(decodedMemory,job->decodedLimit);return job;
}
std::string ReadToken(spray::Reader& r){spray::Check(r.bytes.size()-r.at>=16);const auto token=spray::Hex(r.bytes.subspan(r.at,16));r.at+=16;return token;}
void HttpFrame(){
    if(uploading&&httpEndpoint.port&&GetTickCount64()*.001>=httpDeadline){
        if(httpUpload)httpUpload->cancelled=true;httpUpload.reset();uploading=false;uploadId=0;uploadBytes.clear();
        Command(Prefix("off"));status=L"HTTP 上传或确认超时，点击应用重试";
    }
    if(httpDownload&&httpDownload->done.load(std::memory_order_acquire)){
        auto job=std::move(httpDownload);if(job->epoch==epoch&&!job->cancelled){
            try{spray::Check(job->success&&job->animation);StoreDecoded(job->id,job->animation);Command(Prefix("ready")+" "+std::to_string(job->id));}
            catch(...){Command(Prefix("skip")+" "+std::to_string(job->id));status=L"喷漆下载失败，请检查服务器 HTTP 连接";}
        }
    }
    if(httpUpload){sent=httpUpload->transferred.load();
        if(!httpUpload->done.load(std::memory_order_acquire)){status=L"HTTP 上传 "+std::to_wstring(sent*100/httpUpload->size)+L"%";return;}
        if(!httpUpload->success||httpUpload->cancelled){Command(Prefix("off"));uploading=false;uploadId=0;httpUpload.reset();status=L"HTTP 上传失败，点击应用重试";return;}
        if(!httpAccepted){status=L"上传完成，等待服务器确认";return;}
        try{StoreDecoded(uploadId,httpUpload->animation);status=L"已上传，按原喷漆键喷到墙面";}
        catch(...){Command(Prefix("off"));uploadId=0;status=L"动画缓存已满，图片已保存，请稍后重新应用";}
        uploading=false;httpUpload.reset();
    }
}
int Message(const char*,int count,void* data){
    if(count<5||count>255||!data)return 1;
    SynchronizeSession();
    try {
        spray::Reader r{std::span(static_cast<const std::uint8_t*>(data),size_t(count))};
        auto type=r.U8();const unsigned session=r.U32();
        if(type==spray::Hello){
            const unsigned version=r.U8();spray::Check(session&&(version==3||version==spray::Version));
            spray::http::Endpoint endpoint;
            if(version==spray::Version){const auto port=r.U32();const auto url=spray::http::ReadString(r);spray::Check(port<=65535);
                if(port){if(!url.empty())endpoint=spray::http::ParseEndpoint(url);
                    else {net_status_t net{};spray::Check(engine.pNetAPI&&engine.pNetAPI->Status);engine.pNetAPI->Status(&net);
                        spray::Check(net.connected&&(net.remote_address.type==NA_IP||net.remote_address.type==NA_LOOPBACK));
                        endpoint.port=static_cast<unsigned short>(port);
                        if(net.remote_address.type==NA_LOOPBACK)endpoint.host="127.0.0.1";
                        else {const auto* ip=net.remote_address.ip;endpoint.host=std::to_string(ip[0])+"."+std::to_string(ip[1])+"."+std::to_string(ip[2])+"."+std::to_string(ip[3]);}
                    }
                }
            }r.End();
            if(epoch!=session){ResetSpray();epoch=session;dirty=true;status=L"服务器支持高清喷漆";}
            if(version>=protocol||!httpEndpoint.port){
                if(httpEndpoint.host!=endpoint.host||httpEndpoint.port!=endpoint.port||httpEndpoint.secure!=endpoint.secure){
                    httpWorker.Cancel();httpUpload.reset();httpDownload.reset();httpAccepted=false;uploading=false;uploadId=0;uploadBytes.clear();receiving={};dirty=true;
                }
                protocol=version;httpEndpoint=endpoint;
            }
            status=httpEndpoint.port?L"HTTP 快速喷漆已就绪":L"服务器使用较慢的游戏通道传输";
            Command(Prefix("hello")+" "+std::to_string(version));return 1;
        }
        spray::Check(epoch&&session==epoch);
        if(type==spray::HttpUpload){const auto id=r.U32(),request=r.U32();const auto token=ReadToken(r);r.End();if(!uploading||request!=httpRequest)return 1;spray::Check(!httpUpload&&!uploadBytes.empty());
            try{uploadId=id;httpAccepted=false;auto job=HttpJob(id,unsigned(uploadBytes.size()),spray::Checksum(uploadBytes),true,token);job->bytes=std::move(uploadBytes);
                spray::Check(httpWorker.Submit(job));httpUpload=std::move(job);
            }catch(...){uploading=false;uploadId=0;uploadBytes.clear();Command(Prefix("off"));status=L"HTTP 上传无法开始，请稍后重新应用";}
        }
        else if(type==spray::HttpResult){const auto id=r.U32(),request=r.U32(),result=r.U8();r.End();spray::Check(result<=1);
            if(!uploading||request!=httpRequest||(id&&id!=uploadId))return 1;httpAccepted=result==1;
            if(!result){if(httpUpload)httpUpload->cancelled=true;httpUpload.reset();uploadBytes.clear();uploading=false;uploadId=0;status=L"服务器拒绝了图片，请重新应用";}
        }
        else if(type==spray::HttpDownload){const auto id=r.U32(),size=r.U32(),hash=r.U32();const auto token=ReadToken(r);r.End();
            if(images.contains(id)){Command(Prefix("ready")+" "+std::to_string(id));return 1;}
            try{spray::Check(!httpDownload);auto job=HttpJob(id,size,hash,false,token);spray::Check(httpWorker.Submit(job));httpDownload=std::move(job);}
            catch(...){Command(Prefix("skip")+" "+std::to_string(id));status=L"喷漆下载无法开始，请检查缓存或 HTTP 连接";}
        }
        else if(type==spray::Begin){const auto id=r.U32(),size=r.U32(),hash=r.U32();r.End();receiving.Begin(id,size,hash);}
        else if(type==spray::Data){const auto id=r.U32(),offset=r.U32();auto bytes=r.bytes.subspan(r.at);spray::Check(bytes.size()<=spray::Chunk);
            if(receiving.Append(id,offset,bytes)){
                try{StoreImage(id,receiving.bytes);}catch(...){Command(Prefix("skip")+" "+std::to_string(id));throw;}
                Command(Prefix("ack")+" "+std::to_string(id)+" "+std::to_string(receiving.bytes.size()));receiving={};Collect(id);
            }else if(receiving.bytes.size()%(spray::Chunk*4)==0)Command(Prefix("ack")+" "+std::to_string(id)+" "+std::to_string(receiving.bytes.size()));}
        else if(type==spray::Placement){
            SprayPlacement p;p.id=r.U32();p.asset=r.U32();p.entity=r.U32();p.model=r.U32();
            for(auto& f:p.position)f=r.Float();for(auto& f:p.normal)f=r.Float();p.original={r.U32(),r.U32()};p.started=r.Float();r.End();
            spray::Check(p.id&&p.entity<2048&&p.model<4096&&images.contains(p.asset));
            spray::CheckDimensions(p.original,*images.at(p.asset));spray::Check(std::isfinite(p.started)&&p.started>=0);
            float n=0;for(auto f:p.normal){spray::Check(std::isfinite(f));n+=f*f;}spray::Check(n>.99f&&n<1.01f);
            for(auto f:p.position)spray::Check(std::isfinite(f)&&std::abs(f)<65536);
            if(std::any_of(placements.begin(),placements.end(),[&](auto& d){return d.id==p.id;}))return 1;
            if(placements.size()==spray::MaxDecals){RemoveSprayGeometry(placements.front().id);placements.pop_front();}
            placements.push_back(p);AddSprayGeometry(p,images.at(p.asset));Collect();
            engine.Con_DPrintf("HD spray: received placement %u asset %u (%u frames)\n",p.id,p.asset,unsigned(images.at(p.asset)->frames.size()));
        }else if(type==spray::Remove){auto id=r.U32();r.End();RemoveSprayGeometry(id);std::erase_if(placements,[&](auto& p){return p.id==id;});Collect();}
        else if(type==spray::UploadResult){const auto result=r.U8();
            if(httpEndpoint.port)return 1;
            if(result==2){uploadId=r.U32();spray::Check(uploadId&&uploading&&!uploadBytes.empty());sent=acked=0;status=L"正在上传高清喷漆…";}
            else if(result==3){const auto id=r.U32(),offset=r.U32();spray::Check(id==uploadId&&uploading&&offset>=acked&&offset<=sent);acked=offset;}
            else {spray::Check(result<2);if(!uploading){r.End();return 1;}
                if(httpUpload){httpAccepted=result==1;if(!result){httpUpload->cancelled=true;uploading=false;uploadId=0;httpUpload.reset();status=L"服务器拒绝了图片，请重新应用";}r.End();return 1;}
                uploading=false;
                if(result==1&&uploadId){
                    try{StoreImage(uploadId,uploadBytes);Collect(uploadId);status=L"已上传，按原喷漆键喷到墙面";}
                    catch(const std::exception&){Command(Prefix("off"));uploadId=0;status=L"动画缓存已满，图片已保存，请稍后重新应用";}
                }
                else status=L"服务器拒绝了图片，请重新应用";
                uploadBytes.clear();
            }r.End();
        }else spray::Check(false);
    }catch(const std::exception& e){receiving={};engine.Con_DPrintf("HD spray rejected: %s\n",e.what());}
    return 1;
}
void Info(){engine.Con_Printf("HD spray: session=%u enabled=%d uploaded=%u sending=%u/%u images=%u placements=%u original=%ux%u\n",epoch,int(settings.enabled),uploadId,unsigned(sent),unsigned(uploadBytes.size()),unsigned(images.size()),unsigned(placements.size()),settings.original.width,settings.original.height);}
}
const SpraySettings& CurrentSpray(){return settings;}
const wchar_t* SprayStatus(){return status.c_str();}
bool IsSprayUploading(){return uploading;}
void CancelSprayUpload(){
    if(httpUpload)httpUpload->cancelled=true;httpUpload.reset();httpAccepted=false;
    uploading=dirty=false;uploadId=0;uploadBytes.clear();sent=acked=0;
    if(epoch){Command(Prefix("off"));Collect();}
    status=L"已取消上传，点击应用可重试";
}
void ApplySpray(const SpraySettings& value){
    const auto normalized=NormalizeSettings(value);const auto bytes=EncodeSettings(normalized);
    auto path=SettingsPath();std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=L".tmp";
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.flush();if(!f)throw std::runtime_error("无法保存喷漆设置");}
    // Atomic replacement also detaches any existing linked destination.
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("无法保存喷漆设置");
    if(httpUpload)httpUpload->cancelled=true;httpUpload.reset();httpAccepted=false;
    settings=normalized;dirty=true;uploading=false;uploadId=0;uploadBytes.clear();
    status=epoch?L"等待上传…":L"已保存，连接支持高清喷漆的服务器后上传";
}
void InitializeSpray(){
    engine.pfnHookUserMsg(spray::Message,Message);engine.pfnAddCommand("csol_hd_info",Info);
    try{auto path=SettingsPath();if(std::filesystem::exists(path)){
        auto size=std::filesystem::file_size(path);spray::Check(size>=1&&size<=spray::MaxBytes+10);
        std::ifstream f(path,std::ios::binary);spray::Bytes bytes(size);spray::Check(bool(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())));
        settings=DecodeSettings(bytes);
    }}catch(const std::exception&){settings={};status=L"已忽略损坏的喷漆设置，请重新选择图片";}
}
void ResetSpray(){httpWorker.Cancel();httpUpload.reset();httpDownload.reset();httpEndpoint={};httpAccepted=false;protocol=3;SprayVideoReset();epoch=uploadId=0;uploading=false;dirty=true;uploadBytes.clear();receiving={};placements.clear();images.clear();status=L"连接支持高清喷漆的服务器后上传";}
void SprayFrame(){
    SprayOptionsFrame();PrepareSprayGeometry();if(!epoch)return;HttpFrame();
    const double now=GetTickCount64()*.001;if(now<nextSend)return;nextSend=now+.025;
    if(dirty){dirty=false;
        if(!settings.enabled){Command(Prefix("off"));status=L"使用普通喷漆";return;}
        uploadBytes=settings.bytes;uploading=true;uploadId=0;sent=0;httpDeadline=now+150;
        if(++httpRequest==0)++httpRequest;
        Command(Prefix("begin")+" "+std::to_string(uploadBytes.size())+" "+std::to_string(spray::Checksum(uploadBytes))+" "+std::to_string(settings.original.width)+" "+std::to_string(settings.original.height)+(httpEndpoint.port?" "+std::to_string(httpRequest):""));return;
    }
    for(int batch=0;batch<3&&!httpEndpoint.port&&uploading&&uploadId&&sent<uploadBytes.size()&&sent-acked<spray::UploadChunk*12;++batch){
        const auto chunk=std::span(uploadBytes).subspan(sent,std::min(size_t(spray::UploadChunk),uploadBytes.size()-sent));
        const auto command=Prefix("data")+" "+std::to_string(uploadId)+" "+std::to_string(sent)+" "+spray::Base64(chunk);
        spray::Check(command.size()<127);Command(command);sent+=chunk.size();
        status=L"正在上传 "+std::to_wstring(sent*100/uploadBytes.size())+L"%";
    }
}
void ShutdownSpray(){ReleaseSprayOptions();ResetSpray();httpWorker.Shutdown();ShutdownSprayGraphics();}
}
