// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/hd_spray.hpp"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <iostream>
using namespace csol;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void require(bool v){++checks;if(!v)throw std::runtime_error("image import test failed "+std::to_string(checks));}
void hr(HRESULT v){require(SUCCEEDED(v));}
void writeImage(const std::filesystem::path& path,GUID format,unsigned w,unsigned h){
    ComPtr<IWICImagingFactory> factory;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;hr(factory->CreateStream(&stream));hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;hr(factory->CreateEncoder(format,nullptr,&encoder));hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;hr(encoder->CreateNewFrame(&frame,nullptr));hr(frame->Initialize(nullptr));hr(frame->SetSize(w,h));
    GUID pixels=GUID_WICPixelFormat24bppBGR;hr(frame->SetPixelFormat(&pixels));require(pixels==GUID_WICPixelFormat24bppBGR);
    std::vector<std::uint8_t> bytes(size_t(w)*h*3);for(size_t i=0;i<bytes.size();i+=3){bytes[i]=20;bytes[i+1]=70;bytes[i+2]=210;}
    hr(frame->WritePixels(h,w*3,UINT(bytes.size()),bytes.data()));hr(frame->Commit());hr(encoder->Commit());
}
void rejects(const std::filesystem::path& p){bool rejected=false;try{client::ImportSpray(p);}catch(const std::exception&){rejected=true;}require(rejected);}
struct GifPart {unsigned x,y,w,h,disposal,delay;std::string pixels;bool transparent{true},local{},interlace{};};
spray::Bytes gifBytes(const std::vector<GifPart>& parts,int repeat=2,unsigned w=4,unsigned h=3,unsigned bg=0){
    spray::Bytes b{'G','I','F','8','9','a'};auto u16=[&](unsigned n){b.push_back(n);b.push_back(n>>8);};
    u16(w);u16(h);b.insert(b.end(),{0xf1,std::uint8_t(bg),0,0,0,0,255,0,0,0,255,0,0,0,255});
    if(repeat>=0){b.insert(b.end(),{0x21,0xff,11});const char* name="NETSCAPE2.0";b.insert(b.end(),name,name+11);b.insert(b.end(),{3,1});u16(repeat);b.push_back(0);}
    for(const auto& p:parts){b.insert(b.end(),{0x21,0xf9,4,std::uint8_t(p.disposal*4+int(p.transparent))});u16(p.delay);b.insert(b.end(),{0,0,0x2c});
        u16(p.x);u16(p.y);u16(p.w);u16(p.h);b.push_back((p.local?0x81:0)|(p.interlace?0x40:0));
        if(p.local)b.insert(b.end(),{0,0,0,0,255,0,255,0,0,0,0,255});
        // Independent legal LZW stream: CLEAR before each literal prevents
        // dictionary growth. This exercises WIC, not a second copy of our code.
        spray::Bytes data;unsigned bits=0,n=0;auto code=[&](unsigned c){bits|=c<<n;n+=3;while(n>=8){data.push_back(bits);bits>>=8;n-=8;}};
        for(char pixel:p.pixels){code(4);code(pixel-'0');}code(5);if(n)data.push_back(bits);
        b.push_back(2);for(size_t at=0;at<data.size();at+=255){const auto count=std::min(size_t(255),data.size()-at);b.push_back(std::uint8_t(count));b.insert(b.end(),data.begin()+at,data.begin()+at+count);}b.push_back(0);
    }b.push_back(0x3b);return b;
}
void save(const std::filesystem::path& p,const spray::Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());require(bool(f));}
void pixel(const spray::Animation& animation,unsigned frame,unsigned x,unsigned y,std::array<unsigned,4> expected){
    const auto& image=animation.frames.at(frame).image;const auto at=(size_t(y)*image.Width+x)*4;
    for(unsigned i=0;i<4;++i)require(image.RGBA.at(at+i)==expected[i]);
}
void testGif(const std::filesystem::path& out){
    const auto path=out/L"透明差分动画.gif";
    const std::vector<GifPart> parts{{1,0,2,2,1,10,"1111"},{1,0,1,1,2,20,"2"},{2,1,1,1,3,30,"3"},{0,0,1,1,0,0,"0"}};
    const auto bytes=gifBytes(parts);save(path,bytes);spray::Dimensions original{999,999};auto animation=client::ImportSpray(path,&original);
    require(original.width==4&&original.height==3&&animation.frames.size()==4&&animation.plays==3);
    require(animation.frames[0].duration==100&&animation.frames[1].duration==200&&animation.frames[2].duration==300&&animation.frames[3].duration==100);
    pixel(animation,0,0,0,{0,0,0,0});pixel(animation,0,1,0,{255,0,0,255});pixel(animation,0,2,1,{255,0,0,255});
    pixel(animation,1,1,0,{0,255,0,255});pixel(animation,1,2,1,{255,0,0,255});
    pixel(animation,2,1,0,{0,0,0,0});pixel(animation,2,2,1,{0,0,255,255});
    pixel(animation,3,1,0,{0,0,0,0});pixel(animation,3,2,1,{255,0,0,255});
    const auto encoded=spray::EncodeAnimation(animation);const auto decoded=spray::DecodeAnimation(encoded);require(decoded.plays==3&&decoded.frames.size()==4);
    for(size_t i=0;i<4;++i)require(decoded.frames[i].image.RGBA==animation.frames[i].image.RGBA);
    save(path,gifBytes(parts,0));require(client::ImportSpray(path).plays==0);
    save(path,gifBytes(parts,-1));require(client::ImportSpray(path).plays==1);
    save(path,gifBytes({{1,0,1,1,2,1,"1",false},{0,0,1,1,1,2,"2",false}},-1,4,3,3));animation=client::ImportSpray(path);
    pixel(animation,0,0,0,{0,0,255,255});pixel(animation,1,1,0,{0,0,255,255});require(animation.frames[0].duration==100&&animation.frames[1].duration==20);
    save(path,gifBytes({{0,0,1,1,0,65535,"1",true,true}},-1));animation=client::ImportSpray(path);pixel(animation,0,0,0,{0,255,0,255});require(animation.frames[0].duration==655350);
    save(path,gifBytes({{0,0,4,4,0,10,"1111333322220000",true,false,true}},-1,4,4));animation=client::ImportSpray(path);
    pixel(animation,0,0,0,{255,0,0,255});pixel(animation,0,0,1,{0,255,0,255});pixel(animation,0,0,2,{0,0,255,255});pixel(animation,0,0,3,{0,0,0,0});
    for(size_t size=0;size<bytes.size();++size){save(path,spray::Bytes(bytes.begin(),bytes.begin()+size));rejects(path);}
    save(path,gifBytes({{65535,0,1,1,0,10,"1"}}));rejects(path);
    save(path,gifBytes({{0,0,1,1,4,10,"1"}}));rejects(path);
    save(path,gifBytes({{0,0,1,1,0,10,"1"}},0,4096,2049));rejects(path);
    std::vector<GifPart> many(256,{0,0,1,1,0,10,"1"});save(path,gifBytes(many,0,1,1));require(client::ImportSpray(path).frames.size()==256);
    many.push_back(many.back());save(path,gifBytes(many,0,1,1));rejects(path);
    save(path,gifBytes(many,0,4096,1024));rejects(path); // Tiny deltas still require whole-canvas compositing work.
    original={999,999};try{client::ImportSpray(path,&original);}catch(const std::exception&){}require(original.width==999&&original.height==999);
}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2);std::filesystem::path out=argv[1];std::filesystem::create_directories(out);hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    for(auto format:{GUID_ContainerFormatPng,GUID_ContainerFormatJpeg,GUID_ContainerFormatBmp,GUID_ContainerFormatTiff}){
        auto p=out/(L"娴嬭瘯鍥剧墖-"+std::to_wstring(checks)+L".bin");writeImage(p,format,1024,256);spray::Dimensions original;auto animation=client::ImportSpray(p,&original);const auto& image=animation.First();require(animation.frames.size()==1);
        require(original.width==1024&&original.height==256);
        require(image.Width==512&&image.Height==128);require(image.RGBA.size()==512*128*4);
        require(image.RGBA[0]>190&&image.RGBA[1]>50&&image.RGBA[1]<90&&image.RGBA[2]<40&&image.RGBA[3]==255);
        std::filesystem::resize_file(p,12);rejects(p);
    }
    auto p=out/L"wide.bmp";writeImage(p,GUID_ContainerFormatBmp,8192,32);spray::Dimensions original;auto wideAnimation=client::ImportSpray(p,&original);const auto& wide=wideAnimation.First();require(wide.Width==512&&wide.Height==2);require(original.width==8192&&original.height==32);
    p=out/L"too-wide.bmp";writeImage(p,GUID_ContainerFormatBmp,32769,1);rejects(p);
    p=out/L"large.png";{std::ofstream f(p);f.put('x');}std::filesystem::resize_file(p,spray::MaxBytes+1);rejects(p);
    p=out/L"empty.png";{std::ofstream f(p);}rejects(p);
    p=out/L"ten-megabytes.bmp";writeImage(p,GUID_ContainerFormatBmp,1,1);std::filesystem::resize_file(p,spray::MaxBytes);require(client::ImportSpray(p).First().Width==1);
    std::filesystem::resize_file(p,spray::MaxBytes+1);rejects(p);testGif(out);
    CoUninitialize();std::cout<<checks<<" real WIC static/GIF composition, timing and 10MiB import checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
