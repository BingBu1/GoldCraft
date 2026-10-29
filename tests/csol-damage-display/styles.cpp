// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/damage_styles.hpp"
#include "../../native/csol/image_codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace csol::damage;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Check(bool(file), "resource missing");
    std::vector<std::byte> data(static_cast<std::size_t>(file.tellg()));
    file.seekg(0); file.read(reinterpret_cast<char*>(data.data()), data.size());
    Check(bool(file), "resource read failed"); return data;
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Check(argc == 2 || argc == 3, "provide original common resource directory and optional layout output");
        const auto root = std::filesystem::path(argv[1]);
        const auto skins = ReadSkins(Read(root / StyleResource));
        Check(skins.size() == 46 && skins.front().id == 1 && skins.back().id == 46, "skin catalog");
        std::set<std::string> paths;
        for (const auto& skin : skins)
        {
            Check(skin.Find(Style::Dot) == skin.Find(Style::Normal), "DOT maps to NORMAL");
            Check(!skin.Find(Style::Text), "text events do not become decimal atlas values");
            paths.insert(skin.preview);
            for (const auto& p : skin.styles) paths.insert(p.resource);
            for (const auto& p : skin.icons) paths.insert(p.resource);
        }
        unsigned missing{};
        for (const auto& path : paths) {
            if (!std::filesystem::exists(root / path)) {
                Check(path == "resource/FloatingDamage/icon_Heal_skin21.tga", "unexpected missing resource");
                ++missing; continue;
            }
            const auto image = csol::DecodeTGA(Read(root / path)); Check(image.Width && image.Height, "TGA");
        }
        Numbers numbers;
        const auto& skin = skins.front(); const auto& part = *skin.Find(Style::Normal);
        numbers.Push({1,0,0,123}, {0,0,0}, skin.animation, 0); numbers.Advance(0);
        std::array<Quad,12> q;
        Check(LayoutDigits(numbers.Entries()[0],part,{410,51,512,64},{0,0,0},300,200,q)==3,"layout count");
        Check(q[0].u0 > .079f && q[0].u0 < .081f && q[0].u1 < .16f,"original widget UV, not equal tenths");
        Check(q[1].x-q[0].x == q[0].width+part.interval,"configured screen interval");
        Check(q[0].alpha==255,"initial alpha");
        const auto icon=LayoutIcon(numbers.Entries()[0],skin.icons.front(),part,{0,0,0},{q.data(),3});
        Check(icon && icon->x==168 && icon->y==92 && icon->width==109 && icon->height==109,"original critical icon anchor");
        const auto miss=LayoutMiss(numbers.Entries()[0],*skin.Find(Style::Miss),{289,64,512,64},{0,0,0},300,200);
        Check(miss && miss->width==289 && miss->u1==289.0f/512,"miss whole-image sizing");
        numbers.Advance(.6); LayoutDigits(numbers.Entries()[0],part,{410,51,512,64},{0,0,0},300,200,q);
        Check(q[0].alpha<255 && q[0].alpha>0,"original fade");
        bool rejected=false;
        try { const std::byte bad[]{std::byte{1}}; ReadSkins(bad); } catch(...) { rejected=true; }
        Check(rejected,"malformed config rejection");
        if (argc == 3) {
            std::ofstream out{std::filesystem::path(argv[2])};out<<"[";
            bool comma=false;
            for(const auto& s:skins) for(const auto style:{Style::Normal,Style::Head,Style::Heal}) {
                const auto* p=s.Find(style);const auto image=csol::DecodeTGA(Read(root/p->resource));
                const auto power=[](unsigned x){unsigned v=1;while(v<x)v*=2;return v;};
                const TextureMetrics metrics{image.Width,image.Height,power(image.Width),power(image.Height)};
                Numbers sample;sample.Push({1,std::uint8_t(style),0,12345},{},s.animation,0);sample.Advance(0);
                const auto count=LayoutDigits(sample.Entries()[0],*p,metrics,{},400,160,q);
                Check(count==5,"all skin digit layouts");
                if(comma)out<<',';comma=true;
                out<<"{\"skin\":"<<s.id<<",\"style\":"<<int(style)<<",\"image\":\""<<p->resource<<"\",\"padded\":["<<metrics.paddedWidth<<','<<metrics.paddedHeight<<"],\"quads\":[";
                for(std::size_t i=0;i<count;++i){if(i)out<<',';const auto& a=q[i];out<<'['<<a.x<<','<<a.y<<','<<a.width<<','<<a.height<<','<<a.u0<<','<<a.v0<<','<<a.u1<<','<<a.v1<<']';}
                out<<"]}";
            }
            out<<"]";Check(bool(out),"layout receipt write");
        }
        std::cout << "PASS: 46 original skins, " << paths.size()-missing << " decoded images, " << missing
            << " known missing skin21 heal icon; event/UV/interval/fade checks\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
