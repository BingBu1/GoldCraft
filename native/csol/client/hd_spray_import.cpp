// SPDX-License-Identifier: GPL-3.0-or-later
#include "hd_spray.hpp"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>

namespace csol::client {
namespace {
using Microsoft::WRL::ComPtr;
void CheckImage(HRESULT hr){if(FAILED(hr))throw std::runtime_error("图片损坏或格式不受支持");}
struct GifFrame {unsigned x{},y{},width{},height{},delay{100},disposal{};bool transparent{};};
struct Gif {spray::Dimensions canvas;std::array<std::uint8_t,4> background{};unsigned plays{1};std::vector<GifFrame> frames;};
// Read bounded container metadata; WIC remains the pixel/LZW decoder.
// Validate all blocks before allocating the compositing canvas.
Gif ReadGif(std::span<const std::uint8_t> bytes){
    spray::Reader r{bytes,6};Gif gif;
    auto u16=[&](){const unsigned lo=r.U8();return lo|(r.U8()<<8);};
    auto skip=[&](size_t n){spray::Check(n<=bytes.size()-r.at);r.at+=n;};
    auto block=[&](){const unsigned n=r.U8();spray::Check(n<=bytes.size()-r.at);auto b=bytes.subspan(r.at,n);r.at+=n;return b;};
    gif.canvas={u16(),u16()};spray::TextureDimensions(gif.canvas);
    if(std::uint64_t(gif.canvas.width)*gif.canvas.height>spray::MaxPixels)throw std::runtime_error("GIF 逻辑画布超过 8388608 像素");
    const unsigned flags=r.U8(),background=r.U8();r.U8();
    if(flags&128){const unsigned colors=2u<<(flags&7);spray::Check(background<colors&&colors*3<=bytes.size()-r.at);
        const auto* c=bytes.data()+r.at+background*3;gif.background={c[0],c[1],c[2],255};skip(colors*3);}
    GifFrame control;std::uint64_t sourcePixels=0;
    for(;;){const unsigned tag=r.U8();
        if(tag==0x3b){r.End();break;}
        if(tag==0x21){const unsigned kind=r.U8();
            if(kind==0xf9){spray::Check(r.U8()==4);const unsigned packed=r.U8(),delay=u16();r.U8();spray::Check(r.U8()==0);
                control.disposal=(packed>>2)&7;spray::Check(control.disposal<=3);control.transparent=(packed&1)!=0;control.delay=delay<2?100:delay*10;
            }else if(kind==0xff){const auto name=block();const bool loop=name.size()==11&&(std::memcmp(name.data(),"NETSCAPE2.0",11)==0||std::memcmp(name.data(),"ANIMEXTS1.0",11)==0);
                bool seen=false;for(auto data=block();!data.empty();data=block())if(loop&&!seen){spray::Check(data.size()==3&&data[0]==1);
                    const unsigned repeat=unsigned(data[1])|(unsigned(data[2])<<8);gif.plays=repeat?repeat+1:0;seen=true;}
                spray::Check(!loop||seen);
            }else {
                // Plain text cannot be reproduced by WIC's bitmap decoder.
                spray::Check(kind!=0x01);while(!block().empty()){}
            }
            continue;
        }
        spray::Check(tag==0x2c);GifFrame frame=control;control={};
        frame.x=u16();frame.y=u16();frame.width=u16();frame.height=u16();
        spray::Check(frame.width&&frame.height&&frame.x<gif.canvas.width&&frame.y<gif.canvas.height&&frame.width<=gif.canvas.width-frame.x&&frame.height<=gif.canvas.height-frame.y);
        const unsigned packed=r.U8();if(packed&128)skip((2u<<(packed&7))*3);
        const unsigned codeSize=r.U8();spray::Check(codeSize>=2&&codeSize<=8);bool data=false;
        for(auto b=block();!b.empty();b=block())data=true;spray::Check(data);
        gif.frames.push_back(frame);sourcePixels+=std::uint64_t(frame.width)*frame.height;
        if(gif.frames.size()>spray::MaxFrames)throw std::runtime_error("GIF 最多支持 256 帧");
        if(sourcePixels>128*1024*1024||std::uint64_t(gif.canvas.width)*gif.canvas.height*gif.frames.size()>128*1024*1024)
            throw std::runtime_error("GIF 总解码像素过多");
    }
    spray::Check(!gif.frames.empty());return gif;
}
Image ReadPixels(IWICImagingFactory* factory,IWICBitmapSource* source,spray::Dimensions size){
    ComPtr<IWICBitmapScaler> scaler;CheckImage(factory->CreateBitmapScaler(&scaler));CheckImage(scaler->Initialize(source,size.width,size.height,WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter> converter;CheckImage(factory->CreateFormatConverter(&converter));
    CheckImage(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    Image image{size.width,size.height,spray::Bytes(size_t(size.width)*size.height*4)};
    CheckImage(converter->CopyPixels(nullptr,size.width*4,UINT(image.RGBA.size()),image.RGBA.data()));return image;
}
spray::Animation DecodeGif(IWICImagingFactory* factory,IWICBitmapDecoder* decoder,const Gif& gif){
    UINT count{};CheckImage(decoder->GetFrameCount(&count));spray::Check(count==gif.frames.size());
    const auto output=spray::TextureDimensions(gif.canvas,count);
    Image canvas{gif.canvas.width,gif.canvas.height,spray::Bytes(size_t(gif.canvas.width)*gif.canvas.height*4)};
    auto fill=[&](const GifFrame& area,std::array<std::uint8_t,4> color){
        for(unsigned y=area.y;y<area.y+area.height;++y)for(unsigned x=area.x;x<area.x+area.width;++x)
            std::memcpy(canvas.RGBA.data()+(size_t(y)*canvas.Width+x)*4,color.data(),4);
    };
    if(!gif.frames.front().transparent)fill({0,0,canvas.Width,canvas.Height},gif.background);
    spray::Animation animation;animation.plays=gif.plays;
    for(unsigned i=0;i<count;++i){const auto& meta=gif.frames[i];
        ComPtr<IWICBitmapFrameDecode> frame;CheckImage(decoder->GetFrame(i,&frame));UINT w{},h{};CheckImage(frame->GetSize(&w,&h));
        spray::Check(w==meta.width&&h==meta.height);auto pixels=ReadPixels(factory,frame.Get(),{w,h});
        spray::Bytes previous;if(meta.disposal==3)previous=canvas.RGBA;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto* pixel=pixels.RGBA.data()+(size_t(y)*w+x)*4;
            if(pixel[3])std::memcpy(canvas.RGBA.data()+(size_t(y+meta.y)*canvas.Width+x+meta.x)*4,pixel,4);}
        ComPtr<IWICBitmap> bitmap;CheckImage(factory->CreateBitmapFromMemory(canvas.Width,canvas.Height,GUID_WICPixelFormat32bppRGBA,canvas.Width*4,UINT(canvas.RGBA.size()),canvas.RGBA.data(),&bitmap));
        animation.frames.push_back({ReadPixels(factory,bitmap.Get(),output),meta.delay});
        if(meta.disposal==2)fill(meta,meta.transparent?std::array<std::uint8_t,4>{}:gif.background);
        else if(meta.disposal==3)canvas.RGBA=std::move(previous);
    }return animation;
}
}
spray::Animation ImportSpray(const std::filesystem::path& path,spray::Dimensions* original){
    const auto size=std::filesystem::file_size(path);
    if(!size||size>spray::MaxBytes)throw std::runtime_error("图片文件不能超过 10 MB（10485760 字节）");
    spray::Bytes bytes(size);std::ifstream file(path,std::ios::binary);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("无法读取图片");
    const bool isGif=bytes.size()>=6&&(std::memcmp(bytes.data(),"GIF89a",6)==0||std::memcmp(bytes.data(),"GIF87a",6)==0);
    Gif gif;if(isGif)gif=ReadGif(bytes);
    const HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    struct ComScope{bool owned;~ComScope(){if(owned)CoUninitialize();}} scope{SUCCEEDED(co)};
    ComPtr<IWICImagingFactory> factory;CheckImage(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;CheckImage(factory->CreateStream(&stream));CheckImage(stream->InitializeFromMemory(bytes.data(),DWORD(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;CheckImage(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder));
    spray::Animation animation;spray::Dimensions dimensions;
    if(isGif){animation=DecodeGif(factory.Get(),decoder.Get(),gif);dimensions=gif.canvas;}
    else {
        ComPtr<IWICBitmapFrameDecode> frame;CheckImage(decoder->GetFrame(0,&frame));UINT w{},h{};CheckImage(frame->GetSize(&w,&h));
        dimensions={w,h};animation.frames.push_back({ReadPixels(factory.Get(),frame.Get(),spray::TextureDimensions(dimensions)),100});
    }
    if(original)*original=dimensions;return animation;
}
}
