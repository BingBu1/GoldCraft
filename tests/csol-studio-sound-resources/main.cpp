// SPDX-License-Identifier: GPL-3.0-or-later
// Real ReHLDS SDK/loader and production collector; FS/advertising are doubles.
#define main DependencyCacheOriginalTests
#include "../csol-dependency-cache/loader.cpp"
#undef main

namespace
{
constexpr const char* Foot1="sound/zombi/boss_footstep_1.wav";
constexpr const char* Foot2="sound/zombi/boss_footstep_2.wav";
constexpr const char* Knife1="sound/zombi/oberon/knife_walk1.wav";
constexpr const char* Knife2="sound/zombi/oberon/knife_walk2.wav";

fixture::Bytes EventModel(int event,std::string_view options,int count=1)
{
    using namespace fixture;
    auto bytes=Model("");const auto offset=bytes.size();
    bytes.resize(offset+std::size_t(count)*sizeof(mstudioevent_t));
    Put(bytes,72,unsigned(bytes.size()));Put(bytes,356+48,unsigned(count));Put(bytes,356+52,unsigned(offset));
    for(int i=0;i<count;++i)
    {
        const auto at=offset+std::size_t(i)*sizeof(mstudioevent_t);
        Put(bytes,at+4,unsigned(event));String(bytes,at+12,options);
    }
    return bytes;
}
void Sounds()
{
    for(const auto* path:{Foot1,Foot2,Knife1,Knife2})fixture::Set(path,"wave fixture");
}
void OrdinaryModelDoesNotCollect()
{
    using namespace fixture;
    for(int malformed=0;malformed<5;++malformed)
    {
        Base();auto bytes=Model("weapons/cache_a.wav","ordinary.bmp");
        if(malformed==1)Put(bytes,168,unsigned(bytes.size()+1));
        if(malformed==2)Put(bytes,164,2049);
        if(malformed==3)Put(bytes,356+52,unsigned(bytes.size()+1));
        if(malformed==4)Put(bytes,356+48,4097);
        Set("models/cache.mdl",std::move(bytes));GoldCraftBeginCsolResources();Precache("models/cache.mdl");
        CHECK(resourceBatch->models==0);CHECK(resourceBatch->reads==0);
        CHECK(advertised.empty());CHECK(opens==0);GoldCraftEndCsolResources();
    }
}
void CheckSelection(std::string_view options,std::initializer_list<const char*> expected)
{
    using namespace fixture;Base();Sounds();Set("models/cache.mdl",EventModel(5008,options));
    Pass("models/cache.mdl",false);CHECK(advertised.size()==expected.size());
    for(const auto* path:expected)CHECK(Contains(path));
    Pass("models/cache.mdl",true);CHECK(advertised.size()==expected.size());
    for(const auto* path:expected)CHECK(Contains(path));
}
void InputReport()
{
    using namespace fixture;
    for(const auto& name:advertised)if(Has(name))ReadDisk(name);
    for(const auto& [name,value]:realInputs)
    {
        ReadDisk(name);std::printf("resource %s %zu ",name.c_str(),value.first);
        for(auto byte:value.second)std::printf("%02x",unsigned(byte));
        std::printf("\n");
    }
}
}

int main(int argc,char** argv)
{
    using namespace fixture;
    try
    {
        if(argc!=2)return 2;
        static_assert(sizeof(studiohdr_t)==244 && sizeof(mstudioevent_t)==76);
        const std::filesystem::path resources(argv[1]);
        OrdinaryModelDoesNotCollect();
        // A real Lastclue type200 model, with no Oberon registration callback.
        for(const auto* path:{"models/monster/zbs_bossL_big00.mdl","models/monster/zbs_bossL_big01.mdl"})
        {
            Reset();common=resources;
            Pass(path,false);CHECK(Contains(Foot1));CHECK(Contains(Foot2));
            CHECK(!Contains(Knife1));CHECK(!Contains(Knife2));
            CHECK(Contains("sound/Zombi/boss_death.wav"));
            Pass(path,true);CHECK(Contains(Foot1));CHECK(Contains(Foot2));
            InputReport();
        }
        for(const char* option:{"","0","3","  +3tail","-0","nonnumeric"})CheckSelection(option,{Foot1,Foot2});
        CheckSelection("1",{Knife1});CheckSelection("2",{Knife2});CheckSelection("4",{Foot1});
        for(const char* option:{"5","-1","10000","2147483647","999999999999999999999999"})CheckSelection(option,{});
        Base();Set("models/cache.mdl",EventModel(5008,"",0));Pass("models/cache.mdl",false);CHECK(advertised.empty());
        Base();Set("models/cache.mdl",EventModel(5004,"weapons/cache_a.wav"));
        Pass("models/cache.mdl",false);CHECK(advertised==std::vector<std::string>{"sound/weapons/cache_a.wav"});
        Base();Set("models/cache.mdl",EventModel(5005,"100weapons/cache_a.wav"));
        Pass("models/cache.mdl",false);CHECK(advertised==std::vector<std::string>{"sound/weapons/cache_a.wav"});
        // Two records and two models share exactly one advertised entry per WAV.
        Base();Sounds();Set("models/cache.mdl",EventModel(5008,"",2));
        Set("models/second.mdl",EventModel(5008,"3",2));
        GoldCraftBeginCsolResources();Precache("models/cache.mdl");Precache("models/second.mdl");Precache("models/cache.mdl");
        CHECK(advertised.size()==2);CHECK(Contains(Foot1));CHECK(Contains(Foot2));CHECK(opens==closes);GoldCraftEndCsolResources();
        // Imported SEQ event dependencies follow the same generic collector.
        Base();Sounds();Set("models/cache.mdl",EventModel(5008,"",0));
        auto seq=Sequence("");Put(seq,8+4,5008);String(seq,8+12,"4");Set("models/cache1.seq",std::move(seq));
        Pass("models/cache.mdl",false);CHECK(Contains(Foot1));CHECK(!Contains(Foot2));CHECK(Contains("models/cache1.seq"));
        // A required selected sample missing must reject the incomplete closure.
        Base();Sounds();Set("models/cache.mdl",EventModel(5008,""));vfs.erase(Foot2);
        Reject([]{Oracle("models/cache.mdl");},"missing weapon dependency: sound/zombi/boss_footstep_2.wav");
        std::printf("studio-sound-resources checks=%u failures=%u\n",checks,failures);return 0;
    }
    catch(const std::exception& error)
    {std::fprintf(stderr,"%s; checks=%u failures=%u\n",error.what(),checks,failures);return 1;}
}
