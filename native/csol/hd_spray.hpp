// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_codec.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cmath>

namespace csol::spray {
inline constexpr unsigned Version=3, MaxSide=512, MaxBytes=10*1024*1024;
inline constexpr unsigned MaxFrameBytes=MaxSide*MaxSide*5+22, MaxFrames=256;
inline constexpr unsigned MaxPixels=8*1024*1024, CacheBytes=128*1024*1024;
inline constexpr unsigned Chunk=176, UploadChunk=48, MaxDecals=32;
static_assert(13+Chunk<=192); // GoldSrc MAX_USER_MSG_DATA, including our header.
inline constexpr char Message[]="CSHDSpray";
enum MessageType : unsigned char { Hello, Begin, Data, Placement, Remove, UploadResult };
using Bytes=std::vector<std::uint8_t>;
inline void Check(bool value) { if(!value) throw std::runtime_error("Invalid HD spray"); }
struct Dimensions { unsigned width{},height{}; };
inline Dimensions TextureDimensions(Dimensions source,unsigned frames=1){
    Check(source.width&&source.height&&source.width<=32768&&source.height<=32768&&std::uint64_t(source.width)*source.height<=128*1024*1024);
    Check(frames&&frames<=MaxFrames);
    unsigned side=MaxSide;
    while(std::uint64_t(side)*side*frames>MaxPixels)--side;
    const unsigned longest=std::max(source.width,source.height);
    if(longest<=side)return source;
    return {std::max(1u,source.width*side/longest),std::max(1u,source.height*side/longest)};
}
inline void CheckDimensions(Dimensions source,const Image& image){
    const auto expected=TextureDimensions(source);
    Check(expected.width==image.Width&&expected.height==image.Height);
}
inline void Put32(Bytes& b,std::uint32_t v) { for(int i=0;i<4;++i)b.push_back(std::uint8_t(v>>(i*8))); }
struct Reader {
    std::span<const std::uint8_t> bytes; size_t at{};
    unsigned U8(){Check(at<bytes.size());return bytes[at++];}
    std::uint32_t U32(){std::uint32_t v=0;for(int i=0;i<4;++i)v|=U8()<<(i*8);return v;}
    float Float(){auto v=U32();float f;std::memcpy(&f,&v,4);return f;}
    void End(){Check(at==bytes.size());}
};
inline void PutFloat(Bytes& b,float f){std::uint32_t v;std::memcpy(&v,&f,4);Put32(b,v);}
inline std::uint32_t Checksum(std::span<const std::uint8_t> b) {
    std::uint32_t c=~0u;for(auto v:b){c^=v;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;
}
inline std::string Hex(std::span<const std::uint8_t> b) {
    constexpr char digits[]="0123456789abcdef";std::string s;s.reserve(b.size()*2);
    for(auto v:b){s+=digits[v>>4];s+=digits[v&15];}return s;
}
inline Bytes Unhex(std::string_view s) {
    Check(!s.empty()&&s.size()<=UploadChunk*2&&s.size()%2==0);Bytes b;b.reserve(s.size()/2);
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;Check(false);return 0;};
    for(size_t i=0;i<s.size();i+=2)b.push_back(digit(s[i])*16+digit(s[i+1]));return b;
}
inline std::string Base64(std::span<const std::uint8_t> b){
    constexpr char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";std::string out;
    unsigned bits=0,n=0;for(auto c:b){bits=(bits<<8)|c;n+=8;while(n>=6){n-=6;out+=digits[(bits>>n)&63];}}
    if(n)out+=digits[(bits<<(6-n))&63];return out;
}
inline Bytes Unbase64(std::string_view text){
    Check(!text.empty()&&text.size()<=UploadChunk*4/3&&text.size()%4!=1);Bytes out;unsigned bits=0,n=0;
    for(char c:text){unsigned v;if(c>='A'&&c<='Z')v=c-'A';else if(c>='a'&&c<='z')v=c-'a'+26;else if(c>='0'&&c<='9')v=c-'0'+52;else if(c=='-')v=62;else if(c=='_')v=63;else{Check(false);v=0;}
        bits=(bits<<6)|v;n+=6;if(n>=8){n-=8;out.push_back(std::uint8_t(bits>>n));}}
    Check((bits&((1u<<n)-1))==0);return out;
}
// QOI's bounded lossless RGBA representation. Imported file parsers are never
// exposed to peers: the wire accepts only this representation, <=512 on either axis.
inline Dimensions ImageDimensions(std::span<const std::uint8_t> b){
    Check(b.size()>=22&&b.size()<=MaxFrameBytes&&std::memcmp(b.data(),"qoif",4)==0);
    auto be=[&](int p){return (unsigned(b[p])<<24)|(unsigned(b[p+1])<<16)|(unsigned(b[p+2])<<8)|b[p+3];};
    unsigned w=be(4),h=be(8);Check(w&&h&&w<=MaxSide&&h<=MaxSide&&b[12]==4&&b[13]==0);
    return {w,h};
}
inline Image Decode(std::span<const std::uint8_t> b) {
    const auto [w,h]=ImageDimensions(b);
    Image image{w,h,Bytes(size_t(w)*h*4)};
    std::array<std::array<std::uint8_t,4>,64> index{};std::array<std::uint8_t,4> p{0,0,0,255};
    size_t at=14;unsigned run=0;
    auto take=[&](){Check(at<b.size()-8);return b[at++];};
    for(unsigned i=0;i<w*h;++i){
        if(run)--run;
        else {auto t=take();
            if(t==254){p[0]=take();p[1]=take();p[2]=take();}
            else if(t==255){for(auto& c:p)c=take();}
            else if((t&192)==0)p=index[t];
            else if((t&192)==64){p[0]+=((t>>4)&3)-2;p[1]+=((t>>2)&3)-2;p[2]+=(t&3)-2;}
            else if((t&192)==128){auto t2=take();int g=(t&63)-32;p[0]+=g+(t2>>4)-8;p[1]+=g;p[2]+=g+(t2&15)-8;}
            else {run=t&63;Check(run<w*h-i);}
            index[(p[0]*3+p[1]*5+p[2]*7+p[3]*11)%64]=p;
        }
        std::memcpy(image.RGBA.data()+size_t(i)*4,p.data(),4);
    }
    constexpr std::uint8_t end[]{0,0,0,0,0,0,0,1};
    Check(!run&&at==b.size()-8&&std::memcmp(b.data()+at,end,8)==0);return image;
}
inline Bytes Encode(const Image& image) {
    auto w=image.Width,h=image.Height;Check(w&&h&&w<=MaxSide&&h<=MaxSide&&image.RGBA.size()==size_t(w)*h*4);
    Bytes b{'q','o','i','f'};auto be=[&](unsigned v){for(int i=3;i>=0;--i)b.push_back(v>>(i*8));};be(w);be(h);b.push_back(4);b.push_back(0);
    std::array<std::array<std::uint8_t,4>,64> index{};std::array<std::uint8_t,4> prev{0,0,0,255};unsigned run=0;
    for(size_t i=0;i<size_t(w)*h;++i){
        std::array<std::uint8_t,4> p;std::memcpy(p.data(),image.RGBA.data()+i*4,4);
        if(p==prev){if(++run==62||i+1==size_t(w)*h){b.push_back(192+run-1);run=0;}continue;}
        if(run){b.push_back(192+run-1);run=0;}
        unsigned slot=(p[0]*3+p[1]*5+p[2]*7+p[3]*11)%64;
        if(index[slot]==p)b.push_back(slot);
        else {index[slot]=p;
            int dr=int(p[0])-prev[0],dg=int(p[1])-prev[1],db=int(p[2])-prev[2];
            if(p[3]!=prev[3]){b.push_back(255);b.insert(b.end(),p.begin(),p.end());}
            else if(dr>=-2&&dr<=1&&dg>=-2&&dg<=1&&db>=-2&&db<=1)b.push_back(64|((dr+2)<<4)|((dg+2)<<2)|(db+2));
            else if(dg>=-32&&dg<=31&&dr-dg>=-8&&dr-dg<=7&&db-dg>=-8&&db-dg<=7){b.push_back(128|(dg+32));b.push_back(((dr-dg+8)<<4)|(db-dg+8));}
            else {b.push_back(254);b.insert(b.end(),p.begin(),p.begin()+3);}
        }prev=p;
    }
    b.insert(b.end(),{0,0,0,0,0,0,0,1});Check(b.size()<=MaxFrameBytes);return b;
}
struct Frame { Image image;unsigned duration{100}; };
struct Animation {
    std::vector<Frame> frames;unsigned plays{1}; // 0 loops forever; otherwise total plays.
    const Image& First()const{return frames.front().image;}
    size_t Memory()const{return size_t(First().Width)*First().Height*4*frames.size();}
};
struct EncodedFrame { std::span<const std::uint8_t> bytes;unsigned duration; };
struct AnimationInfo { std::vector<EncodedFrame> frames;unsigned plays{1};Dimensions size;size_t memory{}; };
inline AnimationInfo InspectAnimation(std::span<const std::uint8_t> b){
    Check(b.size()>=22&&b.size()<=MaxBytes);AnimationInfo info;
    if(std::memcmp(b.data(),"qoif",4)==0)info.frames.push_back({b,100});
    else {
        Check(std::memcmp(b.data(),"qanm",4)==0);Reader r{b,4};Check(r.U32()==1);
        const auto count=r.U32();info.plays=r.U32();Check(count&&count<=MaxFrames&&info.plays<=65536);
        for(unsigned i=0;i<count;++i){const auto delay=r.U32(),length=r.U32();
            Check(delay>=20&&delay<=655350&&length>=22&&length<=b.size()-r.at);
            info.frames.push_back({b.subspan(r.at,length),delay});r.at+=length;
        }r.End();
    }
    for(const auto& frame:info.frames){const auto size=ImageDimensions(frame.bytes);
        if(!info.size.width)info.size=size;
        Check(size.width==info.size.width&&size.height==info.size.height);
        info.memory+=size_t(size.width)*size.height*4;Check(info.memory<=size_t(MaxPixels)*4);
    }return info;
}
inline Animation DecodeAnimation(std::span<const std::uint8_t> bytes,bool keepPixels=true){
    const auto info=InspectAnimation(bytes);Animation animation;animation.plays=info.plays;
    for(const auto& frame:info.frames){auto image=Decode(frame.bytes);
        if(!keepPixels)Bytes{}.swap(image.RGBA);
        animation.frames.push_back({std::move(image),frame.duration});
    }return animation;
}
inline void CheckDimensions(Dimensions source,const Animation& animation){
    const auto expected=TextureDimensions(source,unsigned(animation.frames.size()));
    Check(expected.width==animation.First().Width&&expected.height==animation.First().Height);
}
inline Bytes EncodeAnimation(const Animation& animation){
    Check(!animation.frames.empty()&&animation.frames.size()<=MaxFrames&&animation.plays<=65536);
    if(animation.Memory()>size_t(MaxPixels)*4)throw std::runtime_error("GIF 解码后画面过大");
    if(animation.frames.size()==1)return Encode(animation.First());
    Bytes bytes{'q','a','n','m'};Put32(bytes,1);Put32(bytes,unsigned(animation.frames.size()));Put32(bytes,animation.plays);
    for(const auto& frame:animation.frames){
        Check(frame.image.Width==animation.First().Width&&frame.image.Height==animation.First().Height&&frame.duration>=20&&frame.duration<=655350);
        auto encoded=Encode(frame.image);
        if(encoded.size()+8>MaxBytes-bytes.size())throw std::runtime_error("转换后的动画超过 10 MB 上传上限，请减少帧数或图片尺寸");
        Put32(bytes,frame.duration);Put32(bytes,unsigned(encoded.size()));bytes.insert(bytes.end(),encoded.begin(),encoded.end());
    }return bytes;
}
inline size_t FrameAt(const Animation& animation,double seconds){
    if(animation.frames.size()<2||!std::isfinite(seconds)||seconds<=0)return 0;
    std::uint64_t duration=0;for(const auto& frame:animation.frames)duration+=frame.duration;
    const double ms=seconds*1000.0;
    if(animation.plays&&ms>=double(duration)*animation.plays)return animation.frames.size()-1;
    const double phase=std::fmod(ms,double(duration));std::uint64_t end=0;
    for(size_t i=0;i<animation.frames.size();++i){end+=animation.frames[i].duration;if(phase<double(end))return i;}
    return animation.frames.size()-1;
}
// Keep the existing 3 commands /25ms throttle. A 10MiB payload takes at least
// ~30 minutes; allow low-FPS (10Hz) uploads, but expire stalled senders quickly.
inline double UploadSeconds(unsigned bytes){return 120.0+double(bytes)/(UploadChunk*3*10);}
struct Transfer {
    std::uint32_t id{},size{},checksum{};Bytes bytes;
    void Begin(std::uint32_t identity,std::uint32_t length,std::uint32_t hash){
        Check(identity&&length>=22&&length<=MaxBytes);id=identity;size=length;checksum=hash;bytes.clear();bytes.reserve(length);
    }
    bool Append(std::uint32_t identity,std::uint32_t offset,std::span<const std::uint8_t> chunk){
        Check(id&&identity==id&&offset==bytes.size()&&!chunk.empty()&&chunk.size()<=Chunk&&chunk.size()<=size-bytes.size());
        bytes.insert(bytes.end(),chunk.begin(),chunk.end());if(bytes.size()!=size)return false;
        Check(Checksum(bytes)==checksum);return true;
    }
};
}
