#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace goldcraft::text_input {
// Decode a complete SDL commit before delivering any characters. Invalid UTF-8
// must not leave a partially inserted command or accept overlong control bytes.
template<class Emit> bool decode(std::string_view text,Emit emit){
    std::array<std::uint32_t,1024> points{};std::size_t count=0;
    for(std::size_t i=0;i<text.size();){
        const auto first=static_cast<unsigned char>(text[i++]);
        std::uint32_t cp=first;unsigned extra=0;std::uint32_t minimum=0;
        if(first<0x80){}
        else if(first>=0xc2&&first<=0xdf){cp=first&31;extra=1;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){cp=first&15;extra=2;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){cp=first&7;extra=3;minimum=0x10000;}
        else return false;
        if(i+extra>text.size())return false;
        for(unsigned j=0;j<extra;++j){const auto c=static_cast<unsigned char>(text[i++]);if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||count==points.size())return false;
        if(cp>=32&&cp!=127)points[count++]=cp;
    }
    for(std::size_t i=0;i<count;++i)emit(points[i]);
    return true;
}
inline std::wstring wide(std::string_view text){
    std::wstring result;
    if(!decode(text,[&](std::uint32_t cp){
        if(cp<=0xffff)result+=static_cast<wchar_t>(cp);
        else {cp-=0x10000;result+=static_cast<wchar_t>(0xd800+(cp>>10));result+=static_cast<wchar_t>(0xdc00+(cp&1023));}
    }))result.clear();
    return result;
}
struct Target {
    std::uint64_t world=0,menu=0;std::uint32_t window=0;
    bool operator==(const Target&)const=default;
    explicit operator bool()const{return world&&menu&&window;}
};
struct Session {
    Target target{};std::uint64_t since=0;
    bool composing=false,composition_batch=false;
    bool set(Target value,std::uint64_t now){
        if(value==target)return false;
        target=value;since=now;composing=composition_batch=false;return true;
    }
    bool accepts(std::uint32_t window,std::uint64_t timestamp)const{
        return bool(target)&&target.window==window&&static_cast<std::int64_t>(timestamp-since)>=0;
    }
    void editing(bool nonempty){composing=nonempty;composition_batch=true;}
    void committed(){if(composing)composition_batch=true;composing=false;}
    void end_batch(){composition_batch=composing;}
    bool block_key(int glfw)const{
        // Preserve modifier edges; a candidate-selection key must never submit
        // chat, close the screen or edit the already committed text.
        return (composing||composition_batch)&&(glfw<340||glfw>348);
    }
};
}
