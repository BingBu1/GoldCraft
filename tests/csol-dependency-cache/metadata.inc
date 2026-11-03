// SPDX-License-Identifier: GPL-3.0-or-later
// Test the optional provider boundary; real Windows/PAK behavior has its own DLL test.
namespace fixture {
using Identity=goldcraft::filesystem::ResourceStamp;
using MetadataAPI=goldcraft::filesystem::ResourceStampAPI;
unsigned queries,openedQueries,factoryQueries;
std::set<std::string> queryFails,openedFails;
std::map<std::string,Identity> overrides;
unsigned openedFailureAt;
int malformedAPI;
const void* const ownerToken=reinterpret_cast<const void*>(std::uintptr_t(0x1234));

Identity IdentityFor(const std::string& name,const Entry& entry,bool packed)
{
    std::uint64_t id=14695981039346656037ull;
    for(auto ch:name){id^=static_cast<unsigned char>(ch);id*=1099511628211ull;}
    Identity result{1,id,packed?2u:1u,entry.virtualSize?entry.virtualSize:entry.bytes.size(),11,22,
        entry.generation,packed?128u:0u,entry.virtualSize?entry.virtualSize:entry.bytes.size(),packed?1u:0u};
    if(packed)result.size+=128;
    if(auto it=overrides.find(name);it!=overrides.end())result=it->second;
    return result;
}
int QueryMetadata(const void* owner,const char* name,Identity* result,std::uint32_t size)
{
    CHECK(owner==ownerToken);CHECK(name && result && size==sizeof(*result));++queries;
    if(queryFails.contains(name) || cannotOpen.contains(name))return 0;
    const auto* entry=Locate(name);if(!entry)return 0;
    const auto it=pack.find(name);*result=IdentityFor(name,*entry,it!=pack.end() && entry==&it->second);return 1;
}
int OpenedMetadata(const void* owner,void* file,Identity* result,std::uint32_t size)
{
    CHECK(owner==ownerToken);CHECK(file && result && size==sizeof(*result));++openedQueries;
    const auto& handle=*static_cast<Handle*>(file);
    if(openedFails.contains(handle.name) || openedFailureAt==openedQueries)return 0;
    *result=IdentityFor(handle.name,handle.entry,handle.packed);return 1;
}
IBaseInterface* MetadataFactory(const char* name,int*)
{
    ++factoryQueries;CHECK(std::string_view(name)==goldcraft::filesystem::ResourceStampVersion);
    if(malformedAPI==1)return nullptr;
    static MetadataAPI api{};api={sizeof(MetadataAPI),QueryMetadata,OpenedMetadata};
    if(malformedAPI==2)--api.size;
    if(malformedAPI==3)api.query=nullptr;
    if(malformedAPI==4)api.opened=nullptr;
    return reinterpret_cast<IBaseInterface*>(&api);
}
void EnableMetadata()
{
    metadataMode=true;g_FileSystemFactory=MetadataFactory;g_pFileSystem=reinterpret_cast<IFileSystem*>(const_cast<void*>(ownerToken));
    queries=openedQueries=factoryQueries=openedFailureAt=0;malformedAPI=0;
    queryFails.clear();openedFails.clear();overrides.clear();
}
void MetadataMixedMethods()
{
    const auto oldChecks=checks;
    std::printf("mixed-method: metadata probe followed by SHA read\n");std::fflush(stdout);
    Base();EnableMetadata();ResourceBatch batch;
    CHECK(batch.Probe("models/cache.mdl",true).metadata);CHECK(readCalls.empty());
    openedFails.insert("models/cache.mdl");CHECK(batch.Read("models/cache.mdl")==Raw("models/cache.mdl"));
    CHECK(!batch.inputs.at("models/cache.mdl").stamp.reusable);

    std::printf("mixed-method: actual cache invalidation with fallback on an earlier input\n");std::fflush(stdout);
    Base();EnableMetadata();Pass("models/cache.mdl",false);
    Set("models/cache.mdl",Model("weapons/cache_b.wav"));openedFails.insert("materials/common.json");
    Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_b.wav"));CHECK(dependencyCache.Size()==0);
    openedFails.clear();Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);CHECK(readCalls.empty());

    std::printf("mixed-method: SHA read followed by metadata read after body budget is full\n");std::fflush(stdout);
    Base();EnableMetadata();ResourceBatch full;
    constexpr std::size_t limit=64u*1024u*1024u;
    vfs["large"]={{},limit,7};{auto data=full.Read("large");CHECK(data.size()==limit);}
    CHECK(full.retained==limit);openedFails.insert("models/cache.mdl");
    CHECK(full.Read("models/cache.mdl")==Raw("models/cache.mdl"));CHECK(!full.inputs.at("models/cache.mdl").bytes);
    CHECK(full.inputs.at("models/cache.mdl").stamp.reusable && !full.inputs.at("models/cache.mdl").stamp.metadata);
    openedFails.clear();CHECK(full.Read("models/cache.mdl")==Raw("models/cache.mdl"));
    CHECK(!full.inputs.at("models/cache.mdl").stamp.reusable);CHECK(readCalls["models/cache.mdl"]==2);
    CHECK(full.Read("models/cache.mdl")==Raw("models/cache.mdl"));
    CHECK(!full.inputs.at("models/cache.mdl").stamp.reusable);CHECK(readCalls["models/cache.mdl"]==3);
    DependencyCache rejected;const auto key=std::string("models/cache.mdl");
    rejected.Store(key,std::make_shared<const Files>(Files{key}),Inputs{{key,full.Probe(key,true)}},
                   [&](const std::string& name,bool content){return full.Probe(name,content);});
    CHECK(rejected.Size()==0);
    CHECK(opens==closes);Reset();std::printf("metadata-mixed checks=%u failures=%u\n",checks-oldChecks,failures);
}
void MetadataCases()
{
    static_assert(sizeof(Identity)==80 && offsetof(Identity,length)==64);
    static_assert(sizeof(MetadataAPI)==12 && offsetof(MetadataAPI,query)==4 && offsetof(MetadataAPI,opened)==8);
    const auto oldChecks=checks;
    Base();EnableMetadata();Pass("models/cache.mdl",false);
    CHECK(openedQueries==4);CHECK(hashCalls==0);CHECK(readCalls["models/cache.mdl"]==1);
    Pass("models/cache.mdl",true);CHECK(readCalls.empty());CHECK(opens==0 && closes==0);CHECK(hashCalls==0);
    CHECK(queries>=2);CHECK(factoryQueries>0);

    // Each field can independently invalidate a hit, without changing the path.
    const auto natural=IdentityFor("models/cache.mdl",vfs.at("models/cache.mdl"),false);
    const auto fields=std::array{&Identity::volume,&Identity::fileLow,&Identity::fileHigh,&Identity::size,
        &Identity::created,&Identity::modified,&Identity::changed,&Identity::offset,&Identity::length,&Identity::packed};
    for(auto field:fields){
        auto value=natural;value.*field+=7;overrides["models/cache.mdl"]=value;
        Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);CHECK(readCalls.empty());
        overrides.clear();Pass("models/cache.mdl",false);
    }

    // Completed same-size update, stable synthetic modified timestamp, new ChangeTime.
    Set("models/cache.mdl",Model("weapons/cache_b.wav"));Pass("models/cache.mdl",false);
    CHECK(Contains("sound/weapons/cache_b.wav") && !Contains("sound/weapons/cache_a.wav"));
    Pass("models/cache.mdl",true);CHECK(readCalls.empty());
    pack["models/cache.mdl"]={Model("weapons/cache_c.wav")};packFirst=true;
    Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_c.wav"));
    Pass("models/cache.mdl",true);CHECK(readCalls.empty());packFirst=false;Pass("models/cache.mdl",false);

    // Recheck absent optional JSON and trailing sequence probes even with zero body I/O.
    Set("sprites/cache.spr","sprite");
    Set("models/cache.json",R"({"bindSpr":[{"texIndex":0,"sprName":"sprites/cache.spr","sequence":[]}]})");
    Pass("models/cache.mdl",false);CHECK(Contains("sprites/cache.spr"));Pass("models/cache.mdl",true);CHECK(readCalls.empty());
    vfs.erase("models/cache.json");Pass("models/cache.mdl",false);CHECK(!Contains("sprites/cache.spr"));
    Set("models/cache1.seq",Sequence("weapons/cache_a.wav"));Pass("models/cache.mdl",false);CHECK(Contains("models/cache1.seq"));
    Pass("models/cache.mdl",true);CHECK(readCalls.empty());Set("models/cache2.seq",Sequence("weapons/cache_c.wav"));
    Pass("models/cache.mdl",false);CHECK(Contains("models/cache2.seq"));

    // Legacy factories and malformed optional APIs must still use exact SHA reads.
    for(int malformed=1;malformed<=4;++malformed){
        Base();EnableMetadata();malformedAPI=malformed;Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);
        CHECK(readCalls["models/cache.mdl"]==1);CHECK(hashCalls==2);CHECK(openedQueries==0 && queries==0);
    }

    // Unavailable native metadata is a content fallback, never an absent input.
    Base();EnableMetadata();queryFails.insert("models/cache.mdl");openedFails.insert("models/cache.mdl");
    Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);
    CHECK(readCalls["models/cache.mdl"]==1);CHECK(hashCalls==1);CHECK(Contains("sound/weapons/cache_a.wav"));
    Set("models/cache.mdl",Model("weapons/cache_b.wav"));Pass("models/cache.mdl",false);CHECK(Contains("sound/weapons/cache_b.wav"));
    openedFails.clear();queryFails.clear();Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);CHECK(readCalls.empty());

    Base();EnableMetadata();mutateOpened="models/cache.mdl";GoldCraftBeginCsolResources();
    Reject([&]{Precache("models/cache.mdl");},"resource changed during dependency preparation");
    CHECK(dependencyCache.Size()==0);GoldCraftEndCsolResources();
    mutateOpened.clear();Pass("models/cache.mdl",false);Pass("models/cache.mdl",true);

    // One failed side of the before/after probe must never publish metadata equality.
    for(unsigned failureAt:{1u,2u}){
        Base();EnableMetadata();openedFailureAt=failureAt;ResourceBatch batch;
        CHECK(batch.Read("models/cache.mdl")==Raw("models/cache.mdl"));
        CHECK(!batch.inputs.at("models/cache.mdl").stamp.metadata);CHECK(hashCalls==1);CHECK(opens==closes);
    }
    Base();EnableMetadata();openedFailureAt=2;failHash=true;ResourceBatch failed;
    CHECK(failed.Read("models/cache.mdl")==Raw("models/cache.mdl"));
    CHECK(!failed.inputs.at("models/cache.mdl").stamp.reusable);

    // Loss of readability follows original read error rather than reusing a cached closure.
    Base();EnableMetadata();Pass("models/cache.mdl",false);cannotOpen.insert("models/cache.mdl");ResetCounters();GoldCraftBeginCsolResources();
    Reject([&]{Precache("models/cache.mdl");},"missing resource");CHECK(advertised.empty());GoldCraftEndCsolResources();
    cannotOpen.clear();Pass("models/cache.mdl",true);CHECK(readCalls.empty());

    Base();EnableMetadata();ResourceBatch capped;
    for(unsigned i=0;i<65536;++i)capped.inputs.emplace("absent/"+std::to_string(i),ResourceBatch::Input{});
    CHECK(!capped.Probe("models/cache.mdl",true).reusable);CHECK(readCalls["models/cache.mdl"]==1);CHECK(opens==closes);
    Reset();std::printf("metadata-boundary checks=%u failures=%u\n",checks-oldChecks,failures);
}
}
