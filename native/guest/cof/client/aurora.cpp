// SPDX-License-Identifier: GPL-3.0-or-later
#include "aurora.hpp"
#include <metahook.h>
#include <com_model.h>
#include <triangleapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#pragma clang fp contract(off)
namespace guest::cof::client
{
namespace
{
constexpr unsigned Index(AuroraField f) noexcept { return static_cast<unsigned>(f); }
constexpr std::array<std::string_view,24> Keys{{"lifetime","gravity","sprayrate","sprayforce",
    "spraypitch","sprayyaw","startsize","endsize","sizedelta","startframe","framerate",
    "startalpha","endalpha","startred","endred","startgreen","endgreen","startblue","endblue",
    "startangle","angledelta","drag","windstrength","windyaw"}};
struct Tokens
{
    std::string_view input;
    bool invalid{};
    std::string_view Next() noexcept
    {
        for(;;)
        {
            while(!input.empty() && static_cast<unsigned char>(input.front())<=32) input.remove_prefix(1);
            if(!input.starts_with("//"))break;
            const auto end=input.find_first_of("\r\n");
            if(end==input.npos){input={};return {};}
            input.remove_prefix(end);
        }
        if(input.empty())return {};
        if(input.front()=='{' || input.front()=='}')
        {const auto token=input.substr(0,1);input.remove_prefix(1);return token;}
        if(input.front()!='"'){invalid=true;input={};return {};}
        input.remove_prefix(1);
        const auto end=input.find('"');
        if(end==input.npos){invalid=true;input={};return {};}
        const auto token=input.substr(0,end);input.remove_prefix(end+1);return token;
    }
};
bool Number(std::string_view text,float& out) noexcept
{
    if(text.empty() || text.size()>63)return false;
    std::array<char,64> copy{};std::memcpy(copy.data(),text.data(),text.size());
    char* end{};const double value=std::strtod(copy.data(),&end);
    if(end!=copy.data()+text.size() || !std::isfinite(value))return false;
    out=static_cast<float>(value);return std::isfinite(out);
}
bool Range(std::string_view text,AuroraRange& range) noexcept
{
    const auto split=text.find("..");
    if(split==text.npos)
    {if(!Number(text,range.left))return false;range.right=range.left;}
    else if(!Number(text.substr(0,split),range.left) || !Number(text.substr(split+2),range.right))return false;
    range.defined=true;return true;
}
template<std::size_t N> bool Copy(std::array<char,N>& out,std::string_view text) noexcept
{
    if(text.empty() || text.size()>=N || text.find(char(0))!=text.npos)return false;
    std::memcpy(out.data(),text.data(),text.size());out[text.size()]=0;return true;
}
float Delta(float end,float start,float inverse) noexcept
{return static_cast<float>((static_cast<double>(end)-static_cast<double>(start))*static_cast<double>(inverse));}
}
AuroraResult Aurora::Prepare(std::string_view script) noexcept
{
    if(head_>=0)return AuroraResult::InvalidInput;
    if(!engine_ || !engine_->pfnSPR_Load || !engine_->GetSpritePointer || !engine_->pfnRandomFloat ||
       !engine_->GetClientTime || !engine_->GetViewModel || !engine_->GetEntityByIndex ||
       !engine_->GetLocalPlayer || !engine_->pTriAPI)return AuroraResult::MissingBackend;
    if(script.empty() || script.size()>16384 || script.find(char(0))!=script.npos)return AuroraResult::ParseFailed;
    std::array<AuroraType,2> types{};
    for(auto& t:types)
    {
        for(auto f:{AuroraField::Alpha,AuroraField::EndAlpha,AuroraField::Red,AuroraField::EndRed,
                    AuroraField::Green,AuroraField::EndGreen,AuroraField::Blue,AuroraField::EndBlue})
            t.fields[Index(f)]={1,1,true};
        t.fields[Index(AuroraField::Angle)]={45,45,true};
    }
    Tokens tokens{script};unsigned count=0;bool particles=false,limited=false,main=false;
    while(true)
    {
        const auto key=tokens.Next();if(key.empty())break;
        if(key=="{")
        {
            if(count==2)return AuroraResult::Unsupported;
            auto& type=types[count++];bool name=false;
            for(;;)
            {
                const auto field=tokens.Next();if(field=="}")break;
                if(field.empty() || field=="{")return AuroraResult::ParseFailed;
                const auto value=tokens.Next();if(value.empty() || value=="}" || value=="{")return AuroraResult::ParseFailed;
                if(field=="name"){if(!Copy(type.name,value))return AuroraResult::ParseFailed;name=true;}
                else if(field=="spraytype"){if(!Copy(type.sprayName,value))return AuroraResult::ParseFailed;type.spray=true;}
                else if(field=="sprite"){if(value!="sprites/fog2.spr")return AuroraResult::Unsupported;type.sprite=true;}
                else if(field=="rendermode"){if(value!="additive")return AuroraResult::Unsupported;}
                else if(field=="bounce"){AuroraRange r;if(!Range(value,r))return AuroraResult::ParseFailed;
                    if(r.left!=0 || r.right!=0)return AuroraResult::Unsupported;}
                else
                {
                    const auto found=std::find(Keys.begin(),Keys.end(),field);
                    if(found==Keys.end())return AuroraResult::UnknownField;
                    if(field=="sizedelta" || field=="windstrength" || field=="windyaw")return AuroraResult::Unsupported;
                    if(!Range(value,type.fields[static_cast<std::size_t>(found-Keys.begin())]))return AuroraResult::ParseFailed;
                }
            }
            if(!name)return AuroraResult::ParseFailed;
        }
        else
        {
            const auto value=tokens.Next();
            if(key=="particles"){if(value!="20")return AuroraResult::Unsupported;particles=true;}
            else if(key=="limited"){if(value!="1")return AuroraResult::Unsupported;limited=true;}
            else if(key=="maintype"){if(value!="sparker")return AuroraResult::Unsupported;main=true;}
            else return AuroraResult::UnknownField;
        }
    }
    if(tokens.invalid || count!=2 || !particles || !limited || !main)return AuroraResult::ParseFailed;
    if(std::string_view(types[0].name.data())!="sparker" || std::string_view(types[1].name.data())!="sparks" ||
       !types[0].spray || std::string_view(types[0].sprayName.data())!="sparks" || types[1].spray ||
       types[0].sprite || !types[1].sprite)return AuroraResult::Unsupported;
    for(const auto& type:types)
    {
        const auto& life=type.fields[Index(AuroraField::Life)];
        if(life.left<=0 || life.right<=0)return AuroraResult::Unsupported;
        for(auto field:{AuroraField::Angle,AuroraField::Pitch,AuroraField::Yaw,AuroraField::WindYaw})
        {const auto& r=type.fields[Index(field)];if(r.left<=-360 || r.right<=-360 || r.left>=361 || r.right>=361)return AuroraResult::Unsupported;}
    }
    const auto& rate=types[0].fields[Index(AuroraField::Rate)];
    if(rate.left<=0 || rate.right<=0)return AuroraResult::Unsupported;
    const int handle=engine_->pfnSPR_Load("sprites/fog2.spr");
    auto* sprite=handle?engine_->GetSpritePointer(handle):nullptr;
    if(!sprite || sprite->numframes<=0)return AuroraResult::ResourceFailed;
    types_=types;sprite_=sprite;PrepareAuroraDegreeTable(table_);prepared_=true;
    return AuroraResult::Applied;
}
AuroraResult Aurora::Add(int owner,std::string_view script,int local,std::string_view model,
                         int attachment,float particleLimit) noexcept
{
    if(!prepared_)return AuroraResult::NotPrepared;
    if(local!=1 || attachment!=0 || script!="aurora/gunsmoke.aur")return AuroraResult::Unsupported;
    if(owner<0 || !std::isfinite(particleLimit) || model.empty() || model.size()>=128 ||
       model.find(char(0))!=model.npos)return AuroraResult::InvalidInput;
    unsigned limited=0;
    for(int i=head_;i>=0;i=systems_[i].next)
    {const auto& s=systems_[i];if(s.main>=0){const auto& p=s.particles[s.main];if(p.life>=0 && p.life>p.age)++limited;}}
    auto found=std::find_if(systems_.begin(),systems_.end(),[](const auto& s){return !s.used;});
    if(found==systems_.end())return AuroraResult::CapacityExceeded;
    *found={};found->used=true;found->owner=owner;found->attachment=attachment;
    Copy(found->model,model);found->state=static_cast<float>(limited)>particleLimit?2:0;
    found->next=head_;head_=static_cast<int>(found-systems_.begin());
    for(int i=0;i<20;++i)found->particles[i].next=i==19?-1:i+1;
    return AuroraResult::Applied;
}
float Aurora::Random(const AuroraType& type,AuroraField field) noexcept
{const auto& r=type.fields[Index(field)];return engine_->pfnRandomFloat(r.left,r.right);}
int Aurora::Create(AuroraSystem& s,int typeIndex) noexcept
{
    if(s.free<0)return -1;
    const int index=s.free;auto& p=s.particles[index];s.free=p.next;p.next=s.head;s.head=index;
    p.type=typeIndex;p.age=0;const auto& t=types_[typeIndex];p.life=Random(t,AuroraField::Life);
    const float inverse=1.0F/p.life;
    p.velocity={};p.acceleration={0,0,Random(t,AuroraField::Gravity)};p.owner=0;
    p.nextSpray=t.spray?static_cast<float>(1.0/static_cast<double>(Random(t,AuroraField::Rate))):0.0F;
    p.render.size=Random(t,AuroraField::Size);
    p.sizeDelta=t.fields[Index(AuroraField::EndSize)].defined?
        Delta(Random(t,AuroraField::EndSize),p.render.size,inverse):Random(t,AuroraField::SizeDelta);
    p.render.frame=Random(t,AuroraField::Frame);p.frameDelta=Random(t,AuroraField::FrameRate);
    p.render.color[3]=Random(t,AuroraField::Alpha);
    p.alphaDelta=Delta(Random(t,AuroraField::EndAlpha),p.render.color[3],inverse);
    for(unsigned i=0;i<3;++i)
    {
        const auto field=static_cast<AuroraField>(Index(AuroraField::Red)+2*i);
        p.render.color[i]=Random(t,field);
        p.colorDelta[i]=Delta(Random(t,static_cast<AuroraField>(Index(field)+1)),p.render.color[i],inverse);
    }
    p.render.angle=Random(t,AuroraField::Angle);p.angleDelta=Random(t,AuroraField::AngleDelta);
    p.drag=Random(t,AuroraField::Drag);
    const float strength=Random(t,AuroraField::WindStrength);
    const int yaw=static_cast<int>(Random(t,AuroraField::WindYaw));
    p.wind={table_.values[yaw<0?yaw+360:yaw]*strength,
            table_.values[yaw<-90?yaw+450:yaw+90]*strength,0};
    return index;
}
void Aurora::Drain(AuroraSystem& s) noexcept
{
    if(s.state==1)return;
    s.state=1;s.deadline=static_cast<float>(static_cast<double>(engine_->GetClientTime())+3.0);
    s.fadeStart=engine_->GetClientTime();s.fadeDuration=1;
}
bool Aurora::Update(AuroraSystem& s,AuroraParticle& p,float dt) noexcept
{
    if(dt==0.0F)return true;
    if(s.state==1 && engine_->GetClientTime()>s.deadline)return false;
    p.age+=dt;
    if(p.owner)
    {
        if(p.life>=0 && p.age>p.life)Drain(s);
        if(s.state==1 && engine_->GetClientTime()>s.deadline)return false;
        const auto* view=engine_->GetViewModel();if(!view)return false;
        for(unsigned i=0;i<3;++i)
        {p.velocity[i]=(view->attachment[s.attachment][i]-p.render.position[i])/dt;
         p.render.position[i]=view->attachment[s.attachment][i];}
    }
    else
    {
        if(p.life>=0 && p.age>p.life)return false;
        if(p.drag!=0)
        {
            const float factor=-p.drag*dt;
            for(unsigned i=0;i<3;++i)p.velocity[i]+=(p.velocity[i]-p.wind[i])*factor;
        }
        for(unsigned i=0;i<3;++i)p.velocity[i]+=p.acceleration[i]*dt;
        for(unsigned i=0;i<3;++i)p.render.position[i]+=p.velocity[i]*dt;
    }
    if(p.nextSpray!=0 && p.age>p.nextSpray)
    {
        const auto& type=types_[p.type];p.nextSpray=p.age+1.0F/Random(type,AuroraField::Rate);
        const int child=type.spray?Create(s,1):-1;
        if(child>=0)
        {
            auto& c=s.particles[child];c.render.position=p.render.position;
            const float force=Random(type,AuroraField::Force);c.velocity=p.velocity;
            if(force!=0)
            {
                const int pitch=static_cast<int>(Random(type,AuroraField::Pitch));
                const int yaw=static_cast<int>(Random(type,AuroraField::Yaw));
                const float sinPitch=table_.values[pitch<0?pitch+360:pitch];
                const float cosPitch=table_.values[pitch<-90?pitch+450:pitch+90];
                const float sinYaw=table_.values[yaw<0?yaw+360:yaw];
                const float cosYaw=table_.values[yaw<-90?yaw+450:yaw+90];
                const float horizontal=static_cast<float>(static_cast<double>(sinPitch)*force);
                c.velocity[0]=static_cast<float>(static_cast<double>(sinYaw)*horizontal+c.velocity[0]);
                c.velocity[1]=static_cast<float>(static_cast<double>(cosYaw)*horizontal+c.velocity[1]);
                c.velocity[2]=static_cast<float>(static_cast<double>(c.velocity[2])-static_cast<double>(cosPitch)*force);
            }
        }
    }
    p.render.size+=p.sizeDelta*dt;p.render.color[3]+=p.alphaDelta*dt;
    for(unsigned i=0;i<3;++i)p.render.color[i]+=p.colorDelta[i]*dt;
    p.render.frame+=p.frameDelta*dt;p.render.angle+=p.angleDelta*dt;
    while(p.render.angle<0)p.render.angle+=360;
    while(p.render.angle>360)p.render.angle-=360;
    return true;
}
bool Aurora::DeleteOwner(int owner) noexcept
{
    int previous=-1;
    for(int i=head_;i>=0;i=systems_[i].next)
    {auto& s=systems_[i];if(s.owner==owner){if(previous<0)head_=s.next;else systems_[previous].next=s.next;
        s.used=false;return true;}previous=i;}
    return false;
}
void Aurora::KillLocal(bool immediate) noexcept
{
    for(int i=head_;i>=0;i=systems_[i].next)
    {auto& s=systems_[i];if(s.main<0)continue;if(immediate)s.state=2;else if(s.state!=1)s.kill=true;}
}
void Aurora::Clear() noexcept
{for(auto& s:systems_)s.used=false;head_=-1;}
void Aurora::VideoInit() noexcept
{Clear();prepared_=false;sprite_=nullptr;}
AuroraResult Aurora::Frame(float elapsed,int mode,const std::array<float,3>& right,
                            const std::array<float,3>& up) noexcept
{
    if(!prepared_)return AuroraResult::NotPrepared;
    if(!std::isfinite(elapsed))return AuroraResult::InvalidInput;
    if(!engine_->pTriAPI || !engine_->pTriAPI->RenderMode)return AuroraResult::MissingBackend;
    struct RestoreMode { triangleapi_s* api; ~RestoreMode(){FinishAuroraFrame(api);} };
    const RestoreMode restore{engine_->pTriAPI};
    const auto* local=engine_->GetLocalPlayer();if(!local)return AuroraResult::MissingBackend;
    int previous=-1;bool renderFailed=false;
    for(int i=head_;i>=0;)
    {
        auto& s=systems_[i];const int next=s.next;
        const auto* owner=engine_->GetEntityByIndex(s.owner);
        if(!owner)return AuroraResult::InvalidInput;
        if(owner->curstate.skin==666)return AuroraResult::Unsupported;
        if(mode!=0 || owner->curstate.body==70 || owner->curstate.body==184 || owner->curstate.body==137)
        {previous=i;i=next;continue;}
        if(s.state==2)
        {if(previous<0)head_=next;else systems_[previous].next=next;s.used=false;i=next;continue;}
        const auto* view=engine_->GetViewModel();
        if(view)
        {
            if(s.main<0){s.main=Create(s,0);if(s.main>=0)s.particles[s.main].owner=s.owner;}
            else
            {
                view=engine_->GetViewModel();
                if(view && view->model && !std::memchr(view->model->name,0,sizeof(view->model->name)))
                    return AuroraResult::InvalidInput;
                if(!view || s.kill || !view->model || std::strcmp(view->model->name,s.model.data())!=0)Drain(s);
            }
            int prevParticle=-1;
            for(int particle=s.head;particle>=0;)
            {
                auto& p=s.particles[particle];const bool alive=Update(s,p,elapsed);const int after=p.next;
                if(alive){prevParticle=particle;particle=after;}
                else
                {if(prevParticle<0)s.head=after;else s.particles[prevParticle].next=after;
                 p.next=s.free;s.free=particle;particle=after;}
            }
        }
        for(int particle=s.head;particle>=0;particle=s.particles[particle].next)
        {
            auto& p=s.particles[particle];if(!types_[p.type].sprite)continue;
            const AuroraRenderFade fade{s.state,engine_->GetClientTime(),s.fadeStart,s.fadeDuration};
            const auto draw=DrawAuroraParticle(p.render,table_,{true,0,0},fade,right,up,sprite_,engine_->pTriAPI);
            if(draw!=AuroraDrawResult::Drawn && draw!=AuroraDrawResult::SizeZero &&
               draw!=AuroraDrawResult::TextureUnavailable)renderFailed=true;
        }
        previous=i;i=next;
    }
    return renderFailed?AuroraResult::RenderFailed:AuroraResult::Applied;
}
}
