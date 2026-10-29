// SPDX-License-Identifier: GPL-3.0-or-later
#include "buy_menu.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
namespace {
int checks{};
void Check(bool ok){++checks;if(!ok)throw std::runtime_error("buy menu contract failed");}
}
int main(int argc,char** argv)
{
    using namespace csol::buy;
    if(argc!=2)return 2;
    try{
        for(const auto& item:Catalog()){
            Packet original{Kind::Item,65535,item.id,6100,std::numeric_limits<std::int32_t>::max(),Result::Available,2},decoded;
            auto bytes=Encode(original);Check(Decode(bytes,decoded));Check(decoded.request==65535&&decoded.id==item.id&&decoded.balance==original.balance&&decoded.price==6100);
        }
        Packet out{Kind::Result,7,144,10,20,Result::Success,2};const auto valid=Encode(out);
        for(std::size_t n=0;n<MessageBytes;++n)Check(!Decode({valid.data(),n},out));
        auto bad=valid;bad[0]=2;Check(!Decode(bad,out));bad=valid;bad[1]=9;Check(!Decode(bad,out));bad=valid;bad[14]=9;Check(!Decode(bad,out));bad=valid;bad[15]=3;Check(!Decode(bad,out));
        bad=valid;bad[9]=0x80;Check(!Decode(bad,out));bad=valid;bad[13]=0x80;Check(!Decode(bad,out));Check(out.request==7&&out.price==10&&out.balance==20);
        Check(Find(144)&&Find(144)->extendedPrice==4100);Check(Find(220)&&Find(220)->extendedPrice==30);Check(!Find(65535));
        std::ifstream stream(argv[1],std::ios::binary);const std::string json((std::istreambuf_iterator<char>(stream)),{});
        const auto p=ParsePresentation(json);Check(p.width==606&&p.height==460);Check(p.slots[0].x==12&&p.slots[0].y==50&&p.slots[0].w==194&&p.slots[0].h==25);Check(p.images.contains(144)&&p.images.contains(220)&&p.images.contains(725)&&p.images.contains(726));
        const auto resources=Resources(p);Check(resources.size()>20&&resources.front().starts_with("gfx/csol/buy/"));
        const auto reject=[&](std::string value){try{ParsePresentation(value);}catch(const std::exception&){return true;}return false;};
        auto changed=json;auto path=changed.find("gfx/csol/buy/");Check(path!=std::string::npos);changed.replace(path,13,"gfx/csol/../");Check(reject(changed));
        changed=json;const auto height=changed.find("\"height\":460");Check(height!=std::string::npos);changed.replace(height,12,"\"height\":240");Check(reject(changed));
        Check(reject("{}"));Check(reject(std::string(128*1024+1,' ')));
        std::cout<<"buy menu: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
