#pragma once
#include "wire.hpp"
#include <algorithm>
#include <string>

namespace goldcraft {
// GoldSrc ShowMenu: LE16 key mask, signed byte lifetime, continuation byte,
// NUL-terminated text. AMXX 1.9 util.cpp may split the text across messages.
class FormMenu {
    std::string pending_, text_;
    unsigned keys_=0;
    double expires_=0;
    bool continuing_=false;
public:
    void clear(){pending_.clear();text_.clear();keys_=0;expires_=0;continuing_=false;}
    bool active(double now)const{return keys_&&!text_.empty()&&(!expires_||now<expires_);}
    unsigned keys()const{return keys_;}
    const std::string& text()const{return text_;}
    void message(std::span<const std::uint8_t> bytes,double now){
        if(bytes.size()<5||bytes.size()>512||bytes.back()!=0||bytes[3]>1){clear();throw ProtocolError("Invalid ShowMenu payload");}
        const unsigned keys=bytes[0]|(static_cast<unsigned>(bytes[1])<<8);
        if(keys&~1023u||std::find(bytes.begin()+4,bytes.end()-1,0)!=bytes.end()-1){clear();throw ProtocolError("Invalid ShowMenu keys/text");}
        if(!keys){clear();return;}
        if(!continuing_)pending_.clear();
        if(pending_.size()+bytes.size()-5>4096){clear();throw ProtocolError("Oversized ShowMenu text");}
        pending_.append(reinterpret_cast<const char*>(bytes.data()+4),bytes.size()-5);
        continuing_=bytes[3]!=0;
        if(continuing_)return;
        if(pending_.find("GoldCraft forms")==std::string::npos){clear();return;}
        text_=std::move(pending_);pending_.clear();keys_=keys;
        expires_=bytes[2]==255?0:now+bytes[2];
    }
    // Return the server's 1..10 menuselect index, never an MC hotbar action.
    int selection(int key,double now)const{
        const int index=key=='0'?9:key>='1'&&key<='9'?key-'1':-1;
        return active(now)&&index>=0&&(keys_&(1u<<index))?index+1:-1;
    }
};
}
