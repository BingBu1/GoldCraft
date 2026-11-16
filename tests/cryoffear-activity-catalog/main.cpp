// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/activity_catalog.hpp"
#include "native/csol/model_codec.hpp"
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <iterator>
#include <new>

using namespace guest::cof;
namespace
{
unsigned long long checks{},allocations{};
void CheckAt(bool value,int line){++checks;if(!value){std::fprintf(stderr,"activity catalog check %llu failed at line %d\n",checks,line);std::exit(1);}}
#define Check(value) CheckAt((value),__LINE__)
struct Context
{
    std::uint32_t seed{};bool current{true},fail{},replace{},replaceCurrent{};
    ActivityCatalog* catalog{};unsigned used{};std::array<unsigned,6144> events{};
};
bool Current(void* p) noexcept
{
    auto& c=*static_cast<Context*>(p);
    if(c.replaceCurrent){c.replaceCurrent=false;Check(c.catalog->Prepare({}));}
    return c.current;
}
bool Random(void* p,int low,int high,int& result) noexcept
{
    auto& c=*static_cast<Context*>(p);c.seed=c.seed*1664525U+1013904223U;
    result=high>=0?int(c.seed%(std::uint32_t(high)+1U)):std::bit_cast<int>(c.seed);
    Check(c.used+3<=c.events.size());c.events[c.used++]=low;c.events[c.used++]=high;c.events[c.used++]=result;
    if(c.replace){c.replace=false;Check(c.catalog->Prepare({}));}
    return !c.fail;
}
ActivityRandomHost Host(Context& c){return {&c,Current,Random};}
}
void* operator new(std::size_t size){++allocations;if(auto* p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
int main(int argc,char** argv)
{
    if(argc<3)return 2;std::ifstream input(argv[1]),expected(argv[2]);if(!input||!expected)return 2;
    unsigned count{},query{},seed{},cases{};
    while(input>>std::hex>>count)
    {
        if(!(input>>std::hex>>query>>seed)||count>2048)return 3;
        std::vector<ActivitySequence> sequences(count);
        for(auto& s:sequences){unsigned a,w;if(!(input>>std::hex>>a>>w))return 3;
            s={std::bit_cast<int>(a),std::bit_cast<int>(w)};}
        ActivityCatalog catalog;Check(catalog.Prepare(sequences));Context c;c.seed=seed;c.catalog=&catalog;
        const auto before=allocations;int selected=999;
        Check(catalog.Lookup(std::bit_cast<int>(query),Host(c),selected)==ActivityLookupResult::Applied);
        Check(allocations==before);
        unsigned want{},length{};
        if(!(expected>>std::hex>>want>>length)||want!=unsigned(selected)||length!=c.used)return 5;
        for(unsigned i=0;i<length;++i){if(!(expected>>std::hex>>want)||want!=c.events[i])return 5;++checks;}
        ++cases;
    }
    if(!input.eof())return 3;if(!cases)return 6;unsigned extra{};if(expected>>std::hex>>extra)return 7;
    ActivityCatalog catalog;Context c;c.catalog=&catalog;int selected=999;
    Check(catalog.Lookup(70,Host(c),selected)==ActivityLookupResult::MissingBackend&&selected==999);
    const std::array<ActivitySequence,3> sequences{{{70,3},{26,1},{70,4}}};
    Check(catalog.Prepare(sequences));c.fail=true;
    Check(catalog.Lookup(70,Host(c),selected)==ActivityLookupResult::MissingBackend&&selected==999);
    c={};c.catalog=&catalog;c.replace=true;
    Check(catalog.Lookup(70,Host(c),selected)==ActivityLookupResult::Cancelled&&selected==999);
    Check(catalog.Prepare(sequences));c.replaceCurrent=true;
    Check(catalog.Lookup(70,Host(c),selected)==ActivityLookupResult::Cancelled&&selected==999);
    Check(catalog.Prepare(sequences));c.current=false;
    Check(catalog.Lookup(70,Host(c),selected)==ActivityLookupResult::Cancelled&&selected==999);
    c={};c.catalog=&catalog;const auto before=allocations;
    for(unsigned n=0;n<10000;++n){c.used=0;Check(catalog.Lookup(70,Host(c),selected)==ActivityLookupResult::Applied);}
    Check(allocations==before);
    // Compare complete model storage and the exact non-pixel engine prefix.
    for(int arg=3;arg<argc;++arg)
    {
        std::ifstream file(argv[arg],std::ios::binary);Check(file.good());
        std::vector<char> raw{std::istreambuf_iterator<char>(file),{}};
        std::vector<std::byte> data(raw.size());std::memcpy(data.data(),raw.data(),raw.size());
        if(std::to_integer<unsigned>(data[4])!=10)
        {
            ActivityCatalog encoded;Check(!encoded.PrepareModel(data));
            Check(csol::DecodeStudioModel(data));
        }
        const auto read=[&](unsigned at){std::int32_t v;std::memcpy(&v,data.data()+at,4);return v;};
        const int count=read(164),offset=read(168);Check(count>0&&offset>=244);
        std::vector<ActivitySequence> rows;
        for(int i=0;i<count;++i)rows.push_back({read(offset+i*176+40),read(offset+i*176+44)});
        ActivityCatalog direct,model,prefix;Check(direct.Prepare(rows)&&model.PrepareModel(data));
        const int pixel=read(188);
        if(pixel>244&&pixel<=int(data.size())&&read(180)>0)
            Check(prefix.PrepareModel(std::span(data).first(pixel),PlayerModelExtent::ValidatedCachePrefix));
        std::vector<int> activities;for(auto row:rows)activities.push_back(row.activity);
        std::sort(activities.begin(),activities.end());activities.erase(std::unique(activities.begin(),activities.end()),activities.end());
        for(auto activity:activities)for(unsigned seed:{0U,11U,0xffffffffU})
        {
            Context a,b;a.seed=b.seed=seed;int first=-99,second=-99;
            Check(direct.Lookup(activity,Host(a),first)==ActivityLookupResult::Applied);
            Check(model.Lookup(activity,Host(b),second)==ActivityLookupResult::Applied);
            Check(first==second&&a.used==b.used&&a.events==b.events);
            if(pixel>244&&pixel<=int(data.size())&&read(180)>0)
            {
                Context p;p.seed=seed;int selected=-99;
                Check(prefix.Lookup(activity,Host(p),selected)==ActivityLookupResult::Applied);
                Check(first==selected&&a.events==p.events);
            }
        }
        const auto overwrite=[&](unsigned at,int value){std::memcpy(data.data()+at,&value,4);};
        overwrite(164,-1);Check(!model.PrepareModel(data));overwrite(164,count);
        overwrite(164,2049);Check(!model.PrepareModel(data));overwrite(164,count);
        overwrite(168,-1);Check(!model.PrepareModel(data));overwrite(168,offset);
        const int logical=read(72);overwrite(72,logical+1);Check(!model.PrepareModel(data));overwrite(72,logical);
        Check(!model.PrepareModel(std::span(data).first(243)));
        Context a,b;int first=-99,second=-99;
        Check(direct.Lookup(rows.front().activity,Host(a),first)==ActivityLookupResult::Applied);
        Check(model.Lookup(rows.front().activity,Host(b),second)==ActivityLookupResult::Applied&&first==second);
    }
    std::printf("activity catalog: %u original cases, %llu checks passed\n",cases,checks);
}
