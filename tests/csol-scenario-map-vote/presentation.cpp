// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/map_vote_presentation.hpp"
#include "../../native/csol/image_codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace mv=csol::scenario::map_vote;
unsigned checks{},failures{};
void Check(bool pass,const char* label){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    const std::filesystem::path root=argv[1];const auto text=Read(root/mv::PresentationResource);
    const auto p=mv::ParsePresentation(text);
    Check(p.width==932&&p.height==510,"original default container932x510");
    Check(p.font=="DFYuanW9-GB"&&p.fontSize==16&&p.fontWeight==0,"actual Default16 scheme");
    Check(p.columns==3&&p.strideX==210&&p.strideY==147,"original continuous three-column card grid");
    Check(p.list.x==53&&p.list.y==19&&p.list.w==638&&p.list.h==432,"original list viewport");
    Check(p.scrollbar.x==676&&p.scrollbar.y==19&&p.scrollbar.w==14&&p.scrollbar.h==431,"original scrollbar rectangle");
    Check(p.info.x==698&&p.info.y==19&&p.info.w==206&&p.info.h==432,"original information panel");
    Check(p.card.w==200&&p.card.h==134&&p.cardImage.h==97&&p.cardLabel.w==170,"actual modern card layout");
    Check(p.confirm.x==715&&p.confirm.y==352&&p.confirm.w==172&&p.confirm.h==41,"original GlowButton placement");
    Check(p.maps.size()==7&&p.Find(98)&&p.Find(98)->mapName=="zs_deadend","seven original season identities, no gameplay enablement");
    const auto resources=mv::Resources(p);
    Check(resources.size()==60,"deduplicated complete60-file download closure");
    for(const auto& resource:resources){
        const auto bytes=Read(root/resource);Check(!bytes.empty(),"each declared resource exists and is nonempty");
        if(resource.ends_with(".tga")){
            const auto image=csol::DecodeTGA({reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()});
            Check(image.Width>0&&image.Height>0&&image.RGBA.size()==std::size_t(image.Width)*image.Height*4,"original TGA decodes");
        }
    }
    const auto malformed=[&](std::string invalid){try{mv::ParsePresentation(invalid);return false;}catch(const std::exception&){return true;}};
    Check(malformed("{}"),"missing required fields rejected");
    auto invalid=text;auto at=invalid.find("gfx/csol/mapvote/v1/");invalid.replace(at,19,"../unsafe/");
    Check(malformed(invalid),"resource traversal rejected");
    invalid=text;at=invalid.find("\"fontSize\":16");invalid.replace(at,13,"\"fontSize\":0");
    Check(malformed(invalid),"invalid font rejected");
    invalid=text;at=invalid.find("\"width\":932");invalid.replace(at,11,"\"width\":400");
    Check(malformed(invalid),"controls outside container rejected");
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
