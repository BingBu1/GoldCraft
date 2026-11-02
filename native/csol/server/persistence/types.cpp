// SPDX-License-Identifier: GPL-3.0-or-later
#include "types.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>

namespace csol::persistence {
bool EventId::Valid()const{return std::any_of(bytes.begin(),bytes.end(),[](auto b){return b!=0;});}
std::string EventId::Text()const{
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(36);
    for(std::size_t i=0;i<bytes.size();++i){if(i==4||i==6||i==8||i==10)result+='-';result+=hex[bytes[i]>>4];result+=hex[bytes[i]&15];}
    return result;
}
std::optional<EventId> EventId::Parse(std::string_view text){
    if(text.size()!=36)return{};EventId result;std::size_t at=0;
    const auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(std::size_t i=0;i<16;++i){if(i==4||i==6||i==8||i==10){if(text[at++]!='-')return{};}
        const int a=digit(text[at++]),b=digit(text[at++]);if(a<0||b<0)return{};result.bytes[i]=std::uint8_t(a*16+b);}
    return result.Valid()?std::optional(result):std::nullopt;
}
std::optional<EventId> EventId::Generate(){
    EventId result;if(BCryptGenRandom(nullptr,result.bytes.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return{};
    result.bytes[6]=std::uint8_t((result.bytes[6]&15)|64);result.bytes[8]=std::uint8_t((result.bytes[8]&63)|128);return result;
}
std::size_t EventHash::operator()(const EventId& id)const noexcept{
    std::size_t value=2166136261u;for(auto b:id.bytes)value=(value^b)*16777619u;return value;
}
bool ValidSteamAccount(std::uint64_t value){
    return (value>>56)==1&&((value>>52)&15)==1&&((value>>32)&0xfffff)==1&&std::uint32_t(value)!=0;
}
bool ValidAsset(std::string_view value){
    return !value.empty()&&value.size()<=48&&std::all_of(value.begin(),value.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});
}
bool Identities::Approve(unsigned slot,int userid,std::uint64_t connection,std::uint64_t account,bool lan,bool bot,bool proxy){
    if(slot<1||slot>32||userid<=0||!connection||!ValidSteamAccount(account)||lan||bot||proxy)return false;
    players_[slot]={account,connection,0,userid,std::uint8_t(slot)};return true;
}
bool Identities::Life(unsigned slot,int userid,std::uint64_t connection,std::uint64_t life){
    if(slot<1||slot>32||!life)return false;auto& p=players_[slot];
    if(!p.account||p.userid!=userid||p.connection!=connection)return false;p.life=life;return true;
}
void Identities::Disconnect(unsigned slot,int userid,std::uint64_t connection){
    if(slot>0&&slot<=32&&players_[slot].userid==userid&&players_[slot].connection==connection)players_[slot]={};
}
std::optional<Owner> Identities::Get(unsigned slot)const{
    if(slot<1||slot>32||!players_[slot].account)return{};return players_[slot];
}
bool Identities::Current(const Owner& owner)const{
    return owner.slot>0&&owner.slot<=32&&owner.account&&owner.connection&&owner.life&&players_[owner.slot]==owner;
}
bool Request::Valid()const{
    if(!ledger.Valid()||!event.Valid()||!ValidSteamAccount(account))return false;
    if(operation==Operation::Snapshot)return amount==0&&!reference.Valid()&&(asset.empty()||ValidAsset(asset));
    if(amount<=0||amount>MaximumBalance)return false;
    if(operation==Operation::CreditCoins)return asset.empty()&&!reference.Valid();
    if(operation==Operation::CreditItem||operation==Operation::DebitItem)return ValidAsset(asset)&&!reference.Valid();
    return operation==Operation::RefundDebit&&ValidAsset(asset)&&reference.Valid()&&reference!=event;
}
bool Request::SameMutation(const Request& other)const{
    return ledger==other.ledger&&event==other.event&&reference==other.reference&&account==other.account&&
        operation==other.operation&&asset==other.asset&&amount==other.amount;
}
ClaimResult EffectClaims::Claim(const Notice& notice,const Identities& identities){
    if(notice.kind!=NoticeKind::Finished||notice.recovered||
        notice.request.operation!=Operation::DebitItem||notice.reply.outcome!=Outcome::Applied)return ClaimResult::Invalid;
    auto index=EventHash{}(notice.request.event)%Capacity;
    for(std::size_t n=0;n<Capacity;++n,index=(index+1)%Capacity){
        if(claims_[index]==notice.request.event)return ClaimResult::Duplicate;
        if(!claims_[index].Valid()){
            if(!notice.effectEligible)return ClaimResult::Invalid;
            if(!identities.Current(notice.request.owner))return ClaimResult::Stale;
            claims_[index]=notice.request.event;return ClaimResult::Granted;
        }
    }return ClaimResult::Capacity;
}
const char* Message(Outcome value){
    switch(value){
    case Outcome::Applied:return "存档已确认";
    case Outcome::Insufficient:return "道具数量不足";
    case Outcome::Conflict:return "结算编号冲突，未再次扣除或发放";
    case Outcome::Invalid:return "结算请求无效";
    case Outcome::Unavailable:return "存档服务暂不可用；已保存的奖励会继续结算，道具尚未生效";
    case Outcome::StorageFailure:return "无法保存奖励，请稍后重试拾取";
    case Outcome::WrongLedger:return "存档库配置不匹配，已暂停结算";
    case Outcome::Cancelled:return "当前角色状态已变化，道具未生效";
    }return "存档状态未知";
}
}
