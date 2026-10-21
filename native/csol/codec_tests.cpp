// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_codec.hpp"
#include "sprite_codec.hpp"
#include "model_json.hpp"
#include "body_effects.hpp"
#include "events.hpp"
#include "image_codec.hpp"
#include "presentation.hpp"
#include "material_state.hpp"
#include "dependencies.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>

static int checks;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static void PresentationChecks()
{
    // Captured wire layout, including a model index beyond the 16-bit range.
    const std::array<uint8_t,18> packet{2,1,0x78,0x56,0x34,0x12,2,0,3,0,4,0,29,0,30,0,1,0};
    const auto part=csol::ParsePartPresentation(packet);
    Check(part && part->Model==0x12345678 && part->Slot==1 && part->Sequence==2 && part->Skin==3 && part->Body==4 && part->Ammo==29 && part->Maximum==30 && part->Flags==1,"part wire fields differ");
    for(size_t i=0;i<packet.size();++i)Check(!csol::ParsePartPresentation(std::span(packet).first(i)),"truncated part accepted");
    for(auto field:{0,1,12,14,16}){auto bad=packet;bad[field]=field==14?0:255;Check(!csol::ParsePartPresentation(bad),"invalid part state accepted");}
    const std::array<uint8_t,16> legacy{1,0,0x34,0x12,1,0,0,0,0,0,1,0,30,0,0,0};
    const auto old=csol::ParsePartPresentation(legacy);Check(old && old->Model==0x1234 && old->Sequence==1,"legacy part wire changed");
    const std::array<uint8_t,11> view{1,0x78,0x56,0x34,0x12,2,0,0x67,0x45,0x23,1};
    const auto state=csol::ParseViewPresentation(view);Check(state && state->Model==0x12345678 && state->Skin==2 && state->Body==0x1234567,"view wire fields differ");
    for(size_t i=0;i<view.size();++i)Check(!csol::ParseViewPresentation(std::span(view).first(i)),"truncated view accepted");
    auto bad=view;bad[10]=128;Check(!csol::ParseViewPresentation(bad),"negative encoded body accepted");
    csol::PendingPresentation<csol::PartPresentation> pending;pending.Set(*part,10);
    Check(!pending.Take(7,10.5) && pending.Value.has_value(),"pending state lost before matching model");
    Check(pending.Take(part->Model,11).has_value() && !pending.Take(part->Model,11),"pending not consumed once");
    pending.Set(*part,10);Check(!pending.Take(part->Model,12.01) && !pending.Value,"stale pending state applied");
    pending.Set(*part,10);Check(!pending.Take(part->Model,9) && !pending.Value,"pending survived clock reset");
}
static std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open test input");
    const auto size = file.tellg();
    std::vector<std::byte> data((size_t)size);
    file.seekg(0); file.read((char*)data.data(), size);
    Check(bool(file), "short test read");
    return data;
}
static rapidjson::Document Json(const std::filesystem::path& path)
{
    const auto bytes = Read(path);
    rapidjson::Document doc;
    doc.Parse((const char*)bytes.data(), bytes.size());
    Check(!doc.HasParseError(), "invalid test manifest");
    return doc;
}
static void MaterialChecks(const std::filesystem::path& root)
{
    const auto blends=[](csol::Blend blend){return std::array<int,2>{blend==csol::Blend::Add?1:770,blend==csol::Blend::Alpha?771:1};};
    const auto oracle=Json(root/"material-draw-oracle.json");
    for(const auto& row:oracle.GetArray())
    {
        auto policy=csol::ParseMaterialPolicy(row["name"].GetString());float alpha=row["alpha"].GetFloat();
        const auto& result=row["result"];const auto& expected=result["state"];const auto& fade=result["fade"];
        const auto draw=csol::EvaluateMaterial(policy,0,{fade["active"].GetBool(),false,fade["alpha"].GetFloat()},alpha);
        const auto blend=blends(draw.blend);
        Check(draw.cull==expected["2884"].GetBool() && int(draw.depth)==expected["depth"][0].GetInt(),"material fade cull/depth oracle differs");
        if(expected.HasMember("blend"))Check(blend[0]==expected["blend"][0].GetInt() && blend[1]==expected["blend"][1].GetInt(),"material fade blend oracle differs");
        Check((draw.blend!=csol::Blend::Opaque)==expected["3042"].GetBool(),"material blend enable oracle differs");
        for(int c=0;c<4;++c)Check(std::abs(draw.color[c]-expected["color"][c].GetFloat())<.00001f,"material fade color oracle differs");
    }
    const auto loop=Json(root/"material-mesh-loop-oracle.json");float alpha=1;
    for(rapidjson::SizeType i=0;i<loop["inputs"].Size();++i)
    {
        const auto& input=loop["inputs"][i];const auto& expected=loop["trace"][i];
        const auto draw=csol::EvaluateMaterial(csol::ParseMaterialPolicy(input["name"].GetString()),0,{true,false,input["rule"]["alphaMin"].GetFloat()},alpha);
        Check(std::abs(alpha-expected["alpha"].GetFloat())<.00001f,"mesh alpha accumulator differs");
        for(int c=0;c<4;++c)Check(std::abs(draw.color[c]-expected["color"][c].GetFloat())<.00001f,"mesh color accumulator differs");
    }
    const auto common=Json(root/"common-material-oracle.json");
    for(const auto& row:common.GetArray())
    {
        csol::MaterialPolicy policy;const std::string_view mode=row["mode"].GetString();
        policy.commonAlpha=mode=="Translucent";policy.commonAdd=mode=="Additive";policy.commonOpacity=row["color"][3].GetFloat();
        for(int c=0;c<3;++c)policy.commonColor[c]=row["color"][c].GetFloat();
        float alpha=row["alpha"].GetFloat();const auto draw=csol::EvaluateMaterial(policy,0,{},alpha);
        Check(std::abs(alpha-row["alphaOut"].GetFloat())<.00001f,"common opacity oracle differs");
        for(int c=0;c<3;++c)Check(std::abs(draw.color[c]-row["colorOut"][c].GetFloat())<.00001f,"common color oracle differs");
    }
}
static std::string Hash(const void* data, size_t size)
{
    unsigned char digest[32];
    Check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)data, (ULONG)size, digest, 32) == 0, "SHA256 failed");
    const char* digits = "0123456789abcdef";
    std::string result;
    for (auto b : digest) { result += digits[b >> 4]; result += digits[b & 15]; }
    return result;
}
template<class Fn> static void Reject(Fn fn)
{
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "malformed input accepted");
}
static void Put(std::vector<std::byte>& bytes, size_t at, unsigned value)
{
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = std::byte((value >> (8 * i)) & 255);
}
static void BMPChecks()
{
    // Three pixels force row padding in both indexed and BGR24 encodings.
    const std::vector<uint8_t> expected{255,0,0,255,0,255,0,255,0,0,255,255,
        0,0,0,255,255,255,255,255,255,255,0,255};
    for(unsigned bits:{8u,24u,32u})for(bool topDown:{false,true})
    {
        const unsigned stride=bits==8?4:12,offset=bits==8?54+24:54;
        std::vector<std::byte> bmp(offset+stride*2);Put(bmp,0,0x4d42);Put(bmp,2,unsigned(bmp.size()));
        Put(bmp,10,offset);Put(bmp,14,40);Put(bmp,18,3);Put(bmp,22,topDown?unsigned(-2):2);
        Put(bmp,26,1|(bits<<16));Put(bmp,34,stride*2);
        if(bits==8){Put(bmp,46,6);for(int i=0;i<6;++i){bmp[54+4*i]=std::byte(expected[4*i+2]);bmp[55+4*i]=std::byte(expected[4*i+1]);bmp[56+4*i]=std::byte(expected[4*i]);}}
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x)
        {
            const unsigned index=y*3+x,p=offset+(topDown?y:1-y)*stride+x*(bits/8);
            if(bits==8)bmp[p]=std::byte(index);
            else {bmp[p]=std::byte(expected[index*4+2]);bmp[p+1]=std::byte(expected[index*4+1]);bmp[p+2]=std::byte(expected[index*4]);}
        }
        const auto image=csol::DecodeBMP(bmp);
        Check(image.Width==3 && image.Height==2 && image.RGBA==expected,"BMP color/padding/orientation differs");
        for(size_t end:{size_t(0),size_t(53),size_t(offset-1),bmp.size()-1})Reject([&]{csol::DecodeBMP(std::span(bmp).first(end));});
        auto bad=bmp;Put(bad,10,0xffffffffu);Reject([&]{csol::DecodeBMP(bad);});
        bad=bmp;Put(bad,14,0xffffffffu);Reject([&]{csol::DecodeBMP(bad);});
        bad=bmp;Put(bad,30,1);Reject([&]{csol::DecodeBMP(bad);});
        if(bits==8){bad=bmp;bad[offset]=std::byte{6};Reject([&]{csol::DecodeBMP(bad);});}
    }
}
static void DependencyChecks(const std::filesystem::path& root)
{
    auto read=[&](const std::string& file){return Read(root/file);};
    auto exists=[&](const std::string& file){return std::filesystem::is_regular_file(root/file);};
    const auto soul=csol::WeaponDependencies("models/v_soulreaperex.mdl",read,exists);
    for(const auto* needed:{"models/v_soulreaperex.json","sprites/ef_soulreaperex_cmode.spr","sprites/muzzleflash454.spr"})
        Check(std::find(soul.begin(),soul.end(),needed)!=soul.end(),"SoulReaper dependency missing");
    const auto parts=csol::WeaponDependencies("models/v_leapstrikegunex.mdl",read,exists);
    Check(std::find(parts.begin(),parts.end(),"models/v_leapstrikegunex_parts.mdl")!=parts.end(),"companion missing from download list");
    Reject([&]{csol::WeaponDependencies("../bad.mdl",read,exists);});
    Reject([&]{csol::WeaponDependencies("models/v_leapstrikegunex.mdl",read,[&](const std::string& file){return file!="models/v_leapstrikegunex_parts.mdl" && exists(file);});});
    Reject([&]{csol::WeaponDependencies("models/v_soulreaperex.mdl",read,[&](const std::string& file){return file!="sprites/ef_soulreaperex_cmode.spr" && exists(file);});});
    Reject([&]{csol::WeaponDependencies("models/v_soulreaperex.mdl",[&](const std::string& file){auto b=read(file);if(file.ends_with(".mdl"))b.resize(243);return b;},exists);});
    const auto external=csol::ExternalTexture("models/test.mdl","#512502hand.bmp",false,[](const std::string& file){return file=="models/texture/#512502hand.bmp";});
    Check(external=="models/texture/#512502hand.bmp","dimension prefix missing from resource path");
    Check(csol::ExternalTexture("models/test.mdl","#512502hand.bmp",false,[](const std::string& file){return file=="models/texture/hand.bmp";}).empty(),"unprefixed texture must not replace prefixed resource");
}
static void SourceChecks(const std::filesystem::path& oracleRoot,const std::filesystem::path& resourceRoot)
{
    const auto aliases=Json(oracleRoot/"model-redirect-static-oracle.json");
    for(const auto& row:aliases["records"].GetArray())
    {
        if(!row["index"].GetInt())continue;
        const std::string requested=row["requested"].GetString(),physical=row["model"].GetString();
        const auto source=csol::ResolveModelSource(requested,[](const std::string&){return std::vector<std::byte>{};},[&](const std::string& p){return p==physical;});
        Check(source.Redirected && source.File==physical && source.TexturePrefix==row["texturePrefix"].GetString(),"static model mapping differs from original engine");
        const auto descriptor=csol::SerializeModelSource(source);
        const auto prepared=csol::ResolveModelSource(requested,[&](const std::string&){std::vector<std::byte> b(descriptor.size());memcpy(b.data(),descriptor.data(),b.size());return b;},[&](const std::string& p){return p==csol::ModelSourceDescriptor(requested);});
        Check(prepared.Redirected && prepared.File==requested && prepared.PhysicalName==physical && prepared.TexturePrefix==source.TexturePrefix,"prepared alias lost physical provenance");
    }
    const auto textures=Json(oracleRoot/"texture-redirect-oracle.json");
    for(const auto& row:textures["cases"].GetArray())
    {
        csol::ModelSource model{"models/test.mdl","models/test.mdl","models/test.mdl",row["prefix"].GetString(),{},true};
        auto exists=[&](const std::string& path){for(const auto& name:row["available"].GetArray())if(path=="models/texture/"+std::string(name.GetString()))return true;return false;};
        const auto texture=csol::ResolveTextureSource(model,row["original"].GetString(),row["replacement"].GetString(),exists);
        Check(texture.Name==row["headerName"].GetString(),"alias texture name differs from engine");
        if(!row["uploads"].Empty())Check(texture.Resource==row["uploads"][0]["path"].GetString(),"alias texture upload path differs from engine");
        else Check(!texture.External || texture.EmbeddedFallback,"engine embedded texture unexpectedly replaced");
    }
    const auto configured=csol::ParseWeaponRedirects(R"({/* comment */"baseWpnName":"base","resourceName":"variant","texPrefix":"@skin_","vModelTexPrefix":""})","weapons/test.json");
    Check(configured.size()==3 && configured[0].Requested=="models/p_variant.mdl","explicit empty V prefix did not disable alias");
    const auto weaponOracle=Json(oracleRoot/"weapon-redirect-oracle.json");
    for(const auto& row:weaponOracle["cases"].GetArray())
    {
        rapidjson::StringBuffer json;rapidjson::Writer writer(json);row["input"].Accept(writer);
        const auto actual=csol::ParseWeaponRedirects({json.GetString(),json.GetSize()},"weapons/test.json");
        const auto& expected=row["records"];Check(actual.size()==expected.Size(),"configured model count differs from engine");
        for(size_t i=0;i<actual.size();++i){const auto& e=expected[rapidjson::SizeType(i)];Check(actual[i].Requested==e["requested"].GetString() && actual[i].Model==e["model"].GetString() && actual[i].TexturePrefix==e["texturePrefix"].GetString(),"configured model mapping differs from engine");}
    }
    const auto prefixOracle=Json(oracleRoot/"prefix-material-oracle.json");
    for(const auto& row:prefixOracle["cases"].GetArray())
    {
        auto exists=[&](const std::string& path){for(const auto& name:row["available"].GetArray())if(path=="models/texture/"+std::string(name.GetString()))return true;return false;};
        const auto actual=csol::ResolveTextureSource({"models/test.mdl","models/test.mdl","models/test.mdl",row["prefix"].GetString(),{},true},row["original"].GetString(),row["replacement"].GetString(),exists);
        // The captured helper starts with cleared material globals. It only
        // overrides those globals when the prefixed material probe succeeds;
        // the full loader has already parsed the ordinary texture before it.
        const auto draw=std::string(row["prefix"].GetString())+row["headerName"].GetString();
        const bool override=exists("models/texture/"+draw) || (draw.starts_with('$') && exists("models/texture/"+draw.substr(0,draw.find_last_of('.'))+".tga"));
        Check(actual.Name==row["headerName"].GetString(),"prefixed texture name differs from engine");
        if(override)Check(actual.Descriptor==row["descriptor"].GetString(),"prefixed descriptor override differs from engine");
        const auto bits=row["flags"][0].GetInt(),blend=row["flags"][1].GetInt();
        if(bits){const auto expected=blend=='0'?csol::Blend::Opaque:blend=='a'?csol::Blend::Alpha:actual.Name.size()>3 && actual.Name[3]=='a'?csol::Blend::AddAlpha:csol::Blend::Add;
            Check(actual.Policy.special && actual.Policy.cull==bool((bits-'0')&1) && actual.Policy.depth==bool((bits-'0')&2) && actual.Policy.blend==expected,"prefixed blend/depth/cull differ from engine");}
        for(const auto& upload:row["uploads"].GetArray())if(std::string_view(upload["kind"].GetString())=="embedded")
        {
            // A material probe is not an image upload. A missing explicit
            // bindTex also leaves the embedded image in the original engine;
            // it still remains a required dependency for resource delivery.
            Check(actual.Resource.empty() || !exists(actual.Resource),"prefix material probe incorrectly selected an available image");
            if(std::string_view(row["replacement"].GetString()).empty())
                Check(actual.Resource.empty() || actual.EmbeddedFallback,"missing optional prefix image did not retain embedded pixels");
        }
    }
    Reject([&]{csol::ParseWeaponRedirects(R"({"baseWpnName":"../bad","resourceName":"x","texPrefix":""})","weapons/test.json");});
    Reject([&]{csol::ParseWeaponRedirects(R"({"baseWpnName":"base","resourceName":"x","texPrefix":null})","weapons/test.json");});
    auto descriptor=[](std::string text){return [text](const std::string&){std::vector<std::byte> b(text.size());memcpy(b.data(),text.data(),b.size());return b;};};
    Check(csol::ModelSourceDescriptor("models/test.mdl")=="models/test_csol.json","source descriptor is incompatible with GoldSrc download filename checks");
    auto hasDescriptor=[](const std::string& p){return p=="models/test_csol.json";};
    Reject([&]{csol::ResolveModelSource("models/test.mdl",descriptor(R"({"version":1,"requested":"models/other.mdl","physical":"models/base.mdl","texturePrefix":"","configuration":""})"),hasDescriptor);});
    Reject([&]{csol::ResolveModelSource("models/test.mdl",descriptor(R"({"version":1,"requested":"models/test.mdl","physical":"../base.mdl","texturePrefix":"","configuration":""})"),hasDescriptor);});
    Reject([&]{csol::ResolveModelSource("models/test.mdl",descriptor(R"({"version":2,"requested":"models/test.mdl","physical":"models/base.mdl","texturePrefix":"","configuration":""})"),hasDescriptor);});
    const auto read=[&](const std::string& file){return Read(resourceRoot/file);};
    const auto exists=[&](const std::string& file){return std::filesystem::is_regular_file(resourceRoot/file);};
    const auto dual=csol::WeaponDependencies("models/v_alterationammogunex.mdl",read,exists);
    Check(std::find(dual.begin(),dual.end(),"models/v_alterationammogunex_parts01.mdl")!=dual.end() && std::find(dual.begin(),dual.end(),"models/v_alterationammogunex_parts02.mdl")!=dual.end(),"alias lost either companion");
    Check(std::find(dual.begin(),dual.end(),"models/v_alterationammogunex.json")!=dual.end(),"alias selected physical model JSON");
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Check(argc == 4, "expected model oracle, sprite oracle, resource root");
        BMPChecks();
        PresentationChecks();
        MaterialChecks(std::filesystem::path(argv[1]).parent_path());
        DependencyChecks(std::filesystem::path(argv[3]));
        SourceChecks(std::filesystem::path(argv[1]).parent_path(),std::filesystem::path(argv[3]));
        const auto models = Json(argv[1]);
        for (const auto& entry : models["models"].GetArray())
        {
            auto bytes = Read(std::filesystem::path(reinterpret_cast<const char8_t*>(entry["file"].GetString())));
            Check(Hash(bytes.data(), bytes.size()) == entry["sourceSHA256"].GetString(), "model source changed");
            Check(csol::DecodeStudioModel(bytes), "encoded model not decoded");
            Check(Hash(bytes.data(), bytes.size()) == entry["decodedSHA256"].GetString(), "original ICE oracle mismatch");
            const auto plain = bytes;
            Check(!csol::DecodeStudioModel(bytes) && bytes == plain, "plain MDL changed");
        }
        std::vector<std::byte> empty(244);
        Put(empty, 0, 0x54534449); Put(empty, 4, 20); Put(empty, 72, 244);
        for (unsigned length : {0u, 243u, 245u, 0xffffffffu})
        {
            auto bytes = empty; Put(bytes, 72, length); const auto before = bytes;
            Reject([&]{ csol::DecodeStudioModel(bytes); });
            Check(bytes == before, "failed decode changed bytes");
        }
        auto late = empty; Put(late, 204, 1); Put(late, 208, 244);
        Reject([&]{ csol::DecodeStudioModel(late); });
        const auto sprites = Json(argv[2]);
        size_t frames = 0, files = 0;
        for (const auto& entry : sprites["sprites"].GetArray())
        {
            const auto path = std::filesystem::path(argv[3]) / "sprites" / std::filesystem::path(reinterpret_cast<const char8_t*>(entry["file"].GetString()));
            const auto bytes = Read(path);
            if (bytes.size() < 8 || bytes[4] != std::byte{3}) continue;
            const auto sprite = csol::DecodeSpriteV3(bytes);
            const auto server = csol::DecodeSpriteV3(bytes, false);
            const auto& hashes = entry["rgbaSha256"];
            Check(sprite.Frames.size() == hashes.Size(), "frame count changed");
            Check(server.Frames.size() == sprite.Frames.size() && server.Width == sprite.Width && server.Height == sprite.Height, "server metadata differs");
            for (size_t i = 0; i < sprite.Frames.size(); ++i)
            {
                const auto& pixels = sprite.Frames[i].RGBA;
                Check(Hash(pixels.data(), pixels.size()) == hashes[(rapidjson::SizeType)i].GetString(), "RGBA oracle mismatch");
                Check(server.Frames[i].RGBA.empty(), "dedicated server decoded pixels");
                ++frames;
            }
            for (size_t end : {size_t(0), size_t(7), size_t(39), size_t(40), size_t(100), bytes.size() - 1})
                Reject([&]{ csol::DecodeSpriteV3(std::span(bytes).first(end)); });
            for (size_t offset : {size_t(8), size_t(12), size_t(20), size_t(24), size_t(28), size_t(36), size_t(40 + 28)})
            {
                auto corrupt = bytes; Put(corrupt, offset, 0xffffffffu);
                Reject([&]{ csol::DecodeSpriteV3(corrupt, false); });
            }
            auto extra = bytes; extra.push_back(std::byte{});
            Reject([&]{ csol::DecodeSpriteV3(extra, false); });
            ++files;
        }
        Check(files > 0 && frames > 0, "missing real sprite coverage");
        const auto oracleRoot = std::filesystem::path(argv[1]).parent_path();
        const auto animation = Json(oracleRoot / "bindspr-oracle.json");
        for (const auto& row : animation["cases"].GetArray())
        {
            csol::SpriteRule rule;
            rule.Sync=row["sync"].GetBool();rule.Loop=row["loop"].GetBool();
            rule.Start=row["start"].GetInt();rule.End=row["end"].GetInt();rule.Offset=row["offset"].GetInt();rule.Rate=row["rate"].GetDouble();
            const auto result=csol::AdvanceSpriteFrame(rule,row["fps"].GetDouble(),row["frames"].GetInt(),row["previous"].GetDouble(),row["seconds"].GetDouble(),row["modelFrame"].GetDouble());
            Check(std::abs(result-row["expected"].GetDouble())<.001,"original sprite playback oracle differs");
        }
        const auto body = Json(oracleRoot / "bodyfx-oracle.json");
        for(const auto& trace:body["traces"].GetArray())
        {
            csol::BodyEffects effect;effect.Reset(csol::BodyEffectProfile::Gungnir);
            for(const auto& row:trace["rows"].GetArray())
            {
                effect.Update(trace["sequence"].GetInt(),(float)row["time"].GetDouble());
                Check(effect.Mode==row["mode"].GetInt(),"body mode mismatch");
                for(int i=0;i<2;++i)
                {
                    const auto& expected=row["groups"][i];const auto& actual=effect.Groups[i+3];
                    Check(actual.Body==expected["body"].GetInt() && actual.Alpha==expected["alpha"].GetInt(),"body pulse oracle differs");
                }
            }
        }
        for(const auto& entry:sprites["hands"].GetArray())
        {
            const auto path=std::filesystem::path(argv[3])/"gfx/tattoo"/std::filesystem::path(reinterpret_cast<const char8_t*>(entry["file"].GetString()));
            const auto image=csol::DecodeTGA(Read(path));
            Check(Hash(image.RGBA.data(),image.RGBA.size())==entry["rgbaSha256"].GetString(),"hand TGA RGBA oracle differs");
        }
        const auto config=csol::ParseModelJSON(R"({"bindSpr":[{"texIndex":0,"sprName":"sprites/test.spr","sequence":[{"index":0,"startFrame":4},{"index":0,"startFrame":9}]}]})",1,1);
        Check(config.Sprites.size()==1 && config.Sprites[0].Rules[0].Start==4,"JSON duplicate precedence differs");
        Reject([&]{csol::ParseModelJSON(R"({"bindTex":[{"texIndex":0,"texName":"../bad.tga"}]})",1,1);});
        Reject([&]{csol::ParseModelJSON(R"({"bindSpr":[{"texIndex":0,"sprName":"../bad.spr","sequence":[]}]})",1,1);});
        const auto event=csol::ParseSpriteEvent(5051,"#I101S.2P30T999A1L0X5U0");
        Check(event && event->Attachment==5 && event->Follow==5 && event->Overlay && event->Duration(45)==1.5,"extended event semantics differ");
        Check(!csol::ParseSpriteEvent(5004,"weapons/reload.wav"),"ordinary event intercepted");
        Reject([&]{csol::ParseSpriteEvent(5640,"#I1");});
        Reject([&]{csol::ParseSpriteEvent(5000,"#S1e2");});
        Reject([&]{csol::ParseSpriteEvent(5000,"#U+I1");});
        const auto adjacent = csol::ParseSpriteEvent(5000,"#I12E3S+.2U0");
        Check(adjacent && adjacent->Resource == 12 && adjacent->Alternate == 3 && adjacent->Overlay,
            "adjacent E token consumed as exponent");
        Reject([&]{csol::ParseModelJSON(R"({"bindSpr":[{"texIndex":0,"sprName":"sprites/a.spr","sprFPS":null}]})",1,1);});
        const auto fades = Json(oracleRoot / "fade-oracle-vectors.json");
        for (const auto& row : fades.GetArray())
        {
            const auto& r = row["rule"];
            csol::FadeRule rule;
            rule.Offset=r["offsetFrame"].GetInt(); rule.In=r["fadeIn"].GetFloat(); rule.Out=r["fadeOut"].GetFloat();
            rule.Hold=r["holdTime"].GetFloat(); rule.Minimum=r["alphaMin"].GetFloat(); rule.Maximum=r["alphaMax"].GetFloat();
            rule.Once=r["isFadeInOnce"].GetBool();
            const auto actual=csol::EvaluateFade(rule,row["frame"].GetFloat(),row["fps"].GetFloat(),row["frames"].GetInt(),row["rate"].GetFloat(),row["completed"].GetBool());
            const auto& expected=row["result"];
            Check(actual.Active==expected["active"].GetBool() && actual.MarkCompleted==expected["marked"].GetBool() &&
                std::abs(actual.Alpha-expected["alpha"].GetFloat())<.00001f,"original fade oracle differs");
        }
        const auto lifecycle=Json(oracleRoot / "fade-lifecycle-oracle.json");
        for (const auto& row:lifecycle.GetArray())
        {
            const auto& before=row["before"]; const auto& after=row["after"];
            csol::FadeState state{before["completed"].GetBool(),before["current"].GetBool(),before["previous"].GetBool()};
            if (row["visible"].GetBool() && !row["unmatched"].GetBool()) state.Observe(row["result"]["marked"].GetBool());
            Check(state.Completed==after["completed"].GetBool() && state.CurrentActive==after["current"].GetBool() &&
                state.PreviousActive==after["previous"].GetBool(),"visible fade set lifecycle differs");
        }
        std::cout << "PASS checks=" << checks << " models=" << models["models"].Size()
            << " sprites=" << files << " RGBA_frames=" << frames << '\n';
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
