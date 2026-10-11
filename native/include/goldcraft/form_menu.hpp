#pragma once
#include "wire.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace goldcraft {
// GoldSrc ShowMenu: LE16 key mask, signed byte lifetime, continuation byte,
// NUL-terminated text. AMXX 1.9 util.cpp may split the text across messages.
class FormMenu {
public:
    struct Run { std::string text; char color='w'; bool right=false; };
    using Line=std::vector<Run>;
private:
    std::string pending_, text_;
    unsigned keys_=0;
    unsigned pending_keys_=0;
    int pending_lifetime_=0;
    double expires_=0,fragment_deadline_=0;
    bool continuing_=false;
    std::vector<Line> lines_;
    std::uint64_t revision_=0;
    void layout(){
        lines_.clear();lines_.emplace_back();char color='w';bool right=false;
        std::string run;
        auto flush=[&]{if(!run.empty()){lines_.back().push_back({std::move(run),color,right});run.clear();}};
        for(std::size_t i=0;i<text_.size();++i){
            char c=text_[i];
            if(c=='\n'){flush();lines_.emplace_back();right=false;}
            else if(c=='\r')continue;
            else if(c=='\\'&&i+1<text_.size()&&std::string_view("wrydgR").find(text_[i+1])!=std::string_view::npos){
                flush();c=text_[++i];if(c=='R')right=true;else color=c;
            }else run+=c;
        }
        flush();
    }
public:
    void clear(){pending_.clear();text_.clear();lines_.clear();keys_=pending_keys_=0;expires_=fragment_deadline_=0;continuing_=false;++revision_;}
    bool active(double now)const{return keys_&&!text_.empty()&&(!expires_||now<expires_);}
    unsigned keys()const{return keys_;}
    const std::string& text()const{return text_;}
    const std::vector<Line>& lines()const{return lines_;}
    std::uint64_t revision()const{return revision_;}
    bool forms()const{return text_.find("GoldCraft forms")!=std::string::npos;}
    void message(std::span<const std::uint8_t> bytes,double now){
        if(bytes.size()<5||bytes.size()>512||bytes.back()!=0||bytes[3]>1){clear();throw ProtocolError("Invalid ShowMenu payload");}
        const unsigned keys=bytes[0]|(static_cast<unsigned>(bytes[1])<<8);
        if(keys&~1023u||std::find(bytes.begin()+4,bytes.end()-1,0)!=bytes.end()-1){clear();throw ProtocolError("Invalid ShowMenu keys/text");}
        if(!keys){clear();return;}
        const int lifetime=static_cast<std::int8_t>(bytes[2]);
        if(continuing_&&(now>=fragment_deadline_||keys!=pending_keys_||lifetime!=pending_lifetime_)){
            clear();throw ProtocolError("Interrupted ShowMenu fragments");
        }
        if(!continuing_){
            clear();pending_keys_=keys;pending_lifetime_=lifetime;
        }
        if(pending_.size()+bytes.size()-5>4096){clear();throw ProtocolError("Oversized ShowMenu text");}
        pending_.append(reinterpret_cast<const char*>(bytes.data()+4),bytes.size()-5);
        continuing_=bytes[3]!=0;
        if(continuing_){fragment_deadline_=now+2;return;}
        text_=std::move(pending_);pending_.clear();keys_=keys;
        expires_=lifetime>0?now+lifetime:0;layout();++revision_;
    }
    // Return the server's 1..10 menuselect index, never an MC hotbar action.
    int selection(int key,double now)const{
        const int index=key=='0'?9:key>='1'&&key<='9'?key-'1':-1;
        return active(now)&&index>=0&&(keys_&(1u<<index))?index+1:-1;
    }
    static int slot_key(int key,std::string_view binding){
        if(binding.starts_with("slot")){
            const auto number=binding.substr(4);
            if(number=="10")return '0';
            if(number.size()==1&&number[0]>='1'&&number[0]<='9')return number[0];
        }
        return key>='0'&&key<='9'?key:0;
    }
    int selection(int key,std::string_view binding,double now)const{
        return selection(slot_key(key,binding),now);
    }
};
}
