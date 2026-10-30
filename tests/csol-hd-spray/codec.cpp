// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/hd_spray.hpp"
#include <iostream>
#include <random>
#include <functional>
using namespace csol;
unsigned checks{};
void require(bool b){++checks;if(!b)throw std::runtime_error("test failure");}
void rejects(const std::function<void()>& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught);}
void animationChecks(){
    spray::Animation animation{{{Image{1,1,{255,0,0,255}},100},{Image{1,1,{0,255,0,0}},200}},0};
    const auto bytes=spray::EncodeAnimation(animation);const auto decoded=spray::DecodeAnimation(bytes);
    require(decoded.plays==0&&decoded.frames.size()==2&&decoded.frames[1].image.RGBA==animation.frames[1].image.RGBA);
    for(auto [time,frame]:{std::pair{0.0,0u},{.099,0u},{.1,1u},{.299,1u},{.301,0u},{.4,1u},{3000.1,1u}})require(spray::FrameAt(decoded,time)==frame);
    animation.plays=2;require(spray::FrameAt(animation,.6)==1&&spray::FrameAt(animation,600000)==1);
    require(spray::FrameAt(animation,-1)==0&&spray::FrameAt(animation,std::numeric_limits<double>::infinity())==0);
    for(size_t length=0;length<bytes.size();++length)rejects([&]{spray::DecodeAnimation(std::span(bytes).first(length));});
    for(auto offset:{4u,8u,12u,16u,20u}){auto corrupt=bytes;for(unsigned n=0;n<4;++n)corrupt[offset+n]=255;rejects([&]{spray::DecodeAnimation(corrupt);});}
    auto trailing=bytes;trailing.push_back(0);rejects([&]{spray::DecodeAnimation(trailing);});
    // Exactly 10MiB of valid animation, with QOI opcodes independent of Encode.
    // Eight 512x512 RGBA streams would be 256 bytes too long; use RGB for
    // 256 pixels in the final stream. All alpha values remain255.
    spray::Bytes boundary{'q','a','n','m'};spray::Put32(boundary,1);spray::Put32(boundary,8);spray::Put32(boundary,0);
    for(unsigned f=0;f<8;++f){spray::Bytes frame{'q','o','i','f',0,0,2,0,0,0,2,0,4,0};
        for(unsigned i=0;i<512*512;++i){const bool rgb=f==7&&i<256;frame.push_back(rgb?254:255);frame.insert(frame.end(),{1,2,3});if(!rgb)frame.push_back(255);}
        frame.insert(frame.end(),{0,0,0,0,0,0,0,1});spray::Put32(boundary,100);spray::Put32(boundary,unsigned(frame.size()));boundary.insert(boundary.end(),frame.begin(),frame.end());
    }
    require(boundary.size()==spray::MaxBytes);require(spray::DecodeAnimation(boundary,false).frames.size()==8);
    spray::Transfer transfer;transfer.Begin(1,spray::MaxBytes,0);require(transfer.size==spray::MaxBytes);
    boundary.push_back(0);rejects([&]{spray::InspectAnimation(boundary);});rejects([&]{transfer.Begin(1,spray::MaxBytes+1,0);});
    // Header metadata alone cannot reserve over32MiB of decoded pixels.
    auto huge=spray::Bytes{'q','a','n','m'};spray::Put32(huge,1);spray::Put32(huge,33);spray::Put32(huge,0);
    const auto solid=spray::Encode(Image{512,512,spray::Bytes(512*512*4,255)});
    for(unsigned n=0;n<33;++n){spray::Put32(huge,100);spray::Put32(huge,unsigned(solid.size()));huge.insert(huge.end(),solid.begin(),solid.end());}rejects([&]{spray::InspectAnimation(huge);});
    std::mt19937 random(7);Image noise{512,512,spray::Bytes(512*512*4)};for(auto& c:noise.RGBA)c=std::uint8_t(random());
    spray::Animation over;for(int n=0;n<9;++n)over.frames.push_back({noise,100});rejects([&]{spray::EncodeAnimation(over);});
    const auto resized=spray::TextureDimensions({2048,1024},256);require(resized.width==181&&resized.height==90);
    require(spray::UploadSeconds(spray::MaxBytes)>spray::MaxBytes/(48*3*10.0));
}
int main(){
    std::mt19937 random(0x534f4c);
    for(auto [w,h]:{std::pair{1u,1u},{63u,17u},{256u,512u},{512u,512u}}){
        for(unsigned pattern=0;pattern<4;++pattern){
            Image image{w,h,std::vector<std::uint8_t>(size_t(w)*h*4)};
            for(size_t i=0;i<image.RGBA.size();++i)image.RGBA[i]=pattern==0?255:pattern==1?0:pattern==2?std::uint8_t(i/4):std::uint8_t(random());
            auto b=spray::Encode(image);auto decoded=spray::Decode(b);
            require(decoded.Width==w&&decoded.Height==h&&decoded.RGBA==image.RGBA);
            require(b.size()<=spray::MaxBytes);
            spray::Transfer transfer;transfer.Begin(19,unsigned(b.size()),spray::Checksum(b));
            for(size_t at=0;at<b.size();){auto block=std::span(b).subspan(at,std::min(size_t(spray::UploadChunk),b.size()-at));
                require(spray::Unhex(spray::Hex(block))==spray::Bytes(block.begin(),block.end()));
                require(spray::Unbase64(spray::Base64(block))==spray::Bytes(block.begin(),block.end()));
                bool done=transfer.Append(19,unsigned(at),block);at+=block.size();require(done==(at==b.size()));
            }
            require(transfer.bytes==b);
            for(size_t n:{size_t(0),size_t(13),b.size()-9,b.size()-1})rejects([&]{spray::Decode(std::span(b).first(n));});
            auto corrupt=b;corrupt.back()=0;rejects([&]{spray::Decode(corrupt);});
            corrupt=b;corrupt[6]=3;corrupt[7]=0;rejects([&]{spray::Decode(corrupt);});
            corrupt=b;corrupt.push_back(0);rejects([&]{spray::Decode(corrupt);});
        }
    }
    auto single=spray::Encode(Image{1,1,{10,20,30,40}});
    spray::Transfer t;
    rejects([&]{t.Begin(0,22,0);});rejects([&]{t.Begin(1,spray::MaxBytes+1,0);});
    t.Begin(1,unsigned(single.size()),spray::Checksum(single));
    rejects([&]{t.Append(2,0,single);});rejects([&]{t.Append(1,1,single);});rejects([&]{t.Append(1,0,{});});
    auto bad=single;bad[14]^=1;rejects([&]{t.Append(1,0,bad);});
    rejects([&]{spray::Unhex("z0");});rejects([&]{spray::Unhex("abc");});rejects([&]{spray::Unhex(std::string(722,'0'));});
    rejects([&]{spray::Unbase64("A");});rejects([&]{spray::Unbase64("AB");});rejects([&]{spray::Unbase64("AA/");});rejects([&]{spray::Unbase64(std::string(65,'A'));});
    // Independent minimal QOI fixture: one RGBA pixel and exact end marker.
    const spray::Bytes fixture{'q','o','i','f',0,0,0,1,0,0,0,1,4,0,255,1,2,3,4,0,0,0,0,0,0,0,1};
    require(spray::Decode(fixture).RGBA==spray::Bytes({1,2,3,4}));
    auto overflow=fixture;overflow[14]=193;rejects([&]{spray::Decode(overflow);});
    animationChecks();std::cout<<checks<<" HD spray animation/codec/transfer checks passed\n";
}
