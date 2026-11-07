// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the existing bounded GoldSrc sampler and 197/198 unit oracle unchanged.
// Its resource-specific old-prefix byte assertion is intentionally not called:
// the candidate reuses dead bytes, while live geometry is checked below.
#define wmain PreservedSequenceTestMain
#include "../../native/csol/sequence_tests.cpp"
#undef wmain
#include <functional>
#include <set>

namespace csol {
std::vector<std::byte> OldAppendSequences(std::span<const std::byte>,
    const std::vector<std::vector<std::byte>>&, SequenceReadPolicy);
}

namespace arena_test {
using Bytes = std::vector<std::byte>;
unsigned checks{}, cases{};
void Require(bool ok, const char* reason)
{ ++checks; if (!ok) throw std::runtime_error(reason); }
void Equal(std::span<const std::byte> a, std::span<const std::byte> b, const char* reason)
{ Require(a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin()),reason); }
std::span<const std::byte> Slice(const Bytes& bytes, size_t at, size_t size)
{
    Require(at<=bytes.size() && size<=bytes.size()-at,"fixture range outside model");
    return std::span(bytes).subspan(at,size);
}
size_t Add(Bytes& bytes,size_t length)
{ const auto at=bytes.size(); bytes.resize(at+length); return at; }
struct Fixture { Bytes model; std::vector<Bytes> seq; size_t arena{}, descriptors{}, body{}, geometry{}, transitions{}; };

Fixture Model(int count=3, bool shared=false, bool nonmonotonic=false)
{
    const Scales units{{.01f,.01f,.01f,.0003f,.0003f,.0003f}};
    const auto block=SyntheticAnimation(1);
    Fixture f; auto& b=f.model; b.resize(356); f.arena=b.size();
    for(int i=0;i<(shared?1:count);++i)b.insert(b.end(),block.begin(),block.end());
    f.descriptors=Add(b,size_t(count)*176);
    Put(b,0,0x54534449); Put(b,4,10); Put(b,140,1);Put(b,144,244);Put(b,164,count);Put(b,168,int(f.descriptors));
    for(int axis=0;axis<6;++axis)Store(b,244+88+axis*4,units[0][axis]);
    const auto events=Add(b,76),pivots=Add(b,20),group=Add(b,104);
    Put(b,172,1);Put(b,176,int(group));
    Put(b,events,1);Put(b,events+4,5001);Store(b,pivots,3.25f);Put(b,pivots+12,0);Put(b,pivots+16,5);
    for(int i=0;i<count;++i)
    {
        const auto at=f.descriptors+size_t(i)*176;
        const int slot=shared?0:nonmonotonic?count-1-i:i;
        Put(b,at+56,6);Put(b,at+120,2);Put(b,at+124,int(f.arena+slot*block.size()));
        Put(b,at+48,1);Put(b,at+52,int(events));Put(b,at+60,1);Put(b,at+64,int(pivots));
        Put(b,at+40,10+i);Put(b,at+68,1);Store(b,at+76,1.25f);Store(b,at+32,30.f);
    }
    // A real nested body/model/mesh chain, with vertex/normal bone tables and
    // a three-vertex strip plus terminator. Keep counts and consumer indices valid.
    const auto body=Add(b,76);f.body=body; const auto model=Add(b,112);
    const auto mesh=Add(b,20),vinfo=Add(b,3),vertices=Add(b,36),ninfo=Add(b,3),normals=Add(b,36);
    while(b.size()%2)b.push_back(std::byte{});
    const auto triangles=Add(b,28);f.geometry=vertices;
    Put(b,204,1);Put(b,208,int(body));Put(b,body+64,1);Put(b,body+68,1);Put(b,body+72,int(model));
    Put(b,model+72,1);Put(b,model+76,int(mesh));Put(b,model+80,3);Put(b,model+84,int(vinfo));Put(b,model+88,int(vertices));
    Put(b,model+92,3);Put(b,model+96,int(ninfo));Put(b,model+100,int(normals));
    Put(b,mesh,1);Put(b,mesh+4,int(triangles));Put(b,mesh+12,3);Put(b,mesh+16,0);
    Store(b,triangles,short(3));for(int v=0;v<3;++v){Store(b,triangles+2+v*8,short(v));Store(b,triangles+4+v*8,short(v));Store(b,vertices+v*12,float(v+1));Store(b,normals+v*12+8,1.f);}
    f.transitions=Add(b,4);
    const auto texture=Add(b,80),skin=Add(b,2);Put(b,180,1);Put(b,184,int(texture));Put(b,192,1);Put(b,196,1);Put(b,200,int(skin));
    Put(b,texture+68,1);Put(b,texture+72,1);const auto pixels=Add(b,769);Put(b,188,int(pixels));Put(b,texture+76,int(pixels));b[pixels]=std::byte{7};
    Put(b,72,int(b.size()));f.seq={SyntheticSeq(1,1)};return f;
}


void Compare(const Bytes& model,const std::vector<Bytes>& seq,const Bytes& old,const Bytes& now,
             csol::SequenceReadPolicy policy=csol::SequenceReadPolicy::StrictAllTracks)
{
    csol::SequenceSafetyView before,after;
    Require(csol::ReadSequenceSafety(old,before,policy)==csol::SequenceSafetyStatus::Ready,"old safety unavailable");
    Require(csol::ReadSequenceSafety(now,after,policy)==csol::SequenceSafetyStatus::Ready,"candidate safety invalid");
    Require(Word(old,164)==Word(now,164),"sequence count changed");
    Require(BoneUnits(old)==BoneUnits(now),"quantization scales changed");CheckConsumerBytes(model,now);
    const size_t oldRecords=Word(old,Word(old,168)-12),newRecords=Word(now,Word(now,168)-12);
    size_t previousEnd=244;bool allReady=true;
    for(int i=0;i<Word(old,164);++i)
    {
        const size_t a=Word(old,168)+size_t(i)*176,b=Word(now,168)+size_t(i)*176;
        for(size_t j=0;j<176;++j)if(!(j>=52&&j<56)&&!(j>=64&&j<68)&&!(j>=124&&j<128))Require(old[a+j]==now[b+j],"descriptor semantics changed");
        Equal(Slice(old,Word(old,a+52),size_t(Word(old,a+48))*76),Slice(now,Word(now,b+52),size_t(Word(now,b+48))*76),"events changed");
        Equal(Slice(old,Word(old,a+64),size_t(Word(old,a+60))*20),Slice(now,Word(now,b+64),size_t(Word(now,b+60))*20),"pivots changed");
        const size_t ra=oldRecords+size_t(i)*32,rb=newRecords+size_t(i)*32;
        Equal(Slice(old,ra+4,28),Slice(now,rb+4,28),"safety semantics changed");
        const size_t offset=Word(now,rb),bytes=Word(now,rb+4);
        Require(offset>=previousEnd,"output tracks overlap or are nonmonotonic");previousEnd=offset+bytes;
        Equal(Slice(old,Word(old,ra),Word(old,ra+4)),Slice(now,offset,bytes),"quantized animation/padding changed");
        const csol::SequenceSelection selected{std::uint32_t(offset),std::uint32_t(Word(now,b+56)),std::uint32_t(Word(now,b+120)),0,std::uint32_t(Word(now,140))};
        const auto expected=Word(now,rb+24)?csol::SequenceCheck::Ready:csol::SequenceCheck::InvalidTrack;
        allReady=allReady && expected==csol::SequenceCheck::Ready;
        Require(after.Check(selected,0)==expected,"selected track status changed");
    }
    if(allReady){const auto stats=CompareUnits(model,seq,now);CompareUnits(model,seq,old);
        std::cout<<"ORACLE channels="<<stats.channels<<" frames="<<stats.frames<<'\n';}
    Require(csol::AppendSequences(now,seq,policy)==now,"packed input lost idempotence");
}
void Case(const char* name,Fixture f,bool reuse,bool fallback=false)
{
    const auto untouched=f.model;
    const auto old=csol::OldAppendSequences(f.model,f.seq,csol::SequenceReadPolicy::StrictAllTracks);
    const auto now=csol::AppendSequences(f.model,f.seq);
    Compare(f.model,f.seq,old,now);Require(f.model==untouched,"input mutated");
    // Match the header-relative FindTransition table read, including a legacy
    // node outside the declared matrix. Eligibility must preserve that read.
    const int exit=Word(f.model,f.descriptors+164),entry=Word(f.model,f.descriptors+160);
    if(exit>0 && entry>0 && Word(f.model,236)>0)
    {
        const auto transition=[&](const Bytes& bytes){
            const size_t descriptor=Word(bytes,168);
            const auto address=size_t(Word(bytes,240))+
                size_t(Word(bytes,descriptor+164)-1)*Word(bytes,236)+Word(bytes,descriptor+160)-1;
            return Value<unsigned char>(bytes,address);
        };
        Require(transition(old)==transition(now),"FindTransition consumer byte changed");
    }
    if(reuse)Require(now.size()<old.size(),"eligible model failed to reuse arena");
    const size_t liveVertex=Word(f.model,Word(f.model,f.body+72)+88);
    for(const auto at:{liveVertex,size_t(Word(f.model,f.descriptors+52)),size_t(Word(f.model,f.descriptors+64))})
    {
        auto broken=now;broken[at]^=std::byte{1};bool rejected=false;
        try{CheckConsumerBytes(f.model,broken);}catch(const std::exception&){rejected=true;}
        Require(rejected,"consumer oracle missed corrupt geometry/event/pivot");
    }
    if(fallback)Require(now==old,"ineligible model did not preserve old packing fallback");
    ++cases;std::cout<<"CASE "<<name<<" old="<<old.size()<<" new="<<now.size()<<'\n';
}
void Synthetic()
{
    CheckReadSafety();CheckUnits();
    Case("independent native tracks / geometry / events / pivots",Model(),true);
    Case("nonmonotonic source offsets",Model(3,false,true),true);
    Case("compatible source sharing",Model(3,true),true);
    Case("shared source arena capacity insufficient",Model(198,true),false,true);
    auto f=Model();Put(f.model,f.descriptors+88,1);Case("unknown automove position",f,false,true);
    f=Model();Put(f.model,f.descriptors+92,1);Case("unknown automove angle",f,false,true);
    for(size_t field:{220u,224u,228u,232u}){f=Model();Put(f.model,field,1);Case("unknown sound field",f,false,true);}
    f=Model();Put(f.model,Word(f.model,176)+100,4);Case("nonzero sequence group base",f,false,true);
    f=Model();Put(f.model,Word(f.model,f.body+72)+104,1);Case("unknown deformation group",f,false,true);
    // Live data may be perfectly valid yet overlap a formerly dead descriptor.
    // Reuse must fall back, rather than erase it or reject an accepted model.
    f=Model();Put(f.model,f.descriptors+52,int(f.descriptors+176));Case("event references old descriptor arena",f,false,true);
    f=Model();Put(f.model,f.descriptors+64,int(f.descriptors+176));Case("pivot references old descriptor arena",f,false,true);
    f=Model();Put(f.model,Word(f.model,f.body+72)+88,int(f.descriptors+176));Case("nested vertex range in arena",f,false,true);
    f=Model();Put(f.model,236,2);Put(f.model,240,int(f.transitions));Put(f.model,f.descriptors+160,1);Put(f.model,f.descriptors+164,2);
    Case("declared transition nodes within matrix",f,true);
    Put(f.model,f.descriptors+160,3);Case("entry node outside declared transition matrix",f,false,true);
    Put(f.model,f.descriptors+160,1);Put(f.model,f.descriptors+164,3);Case("exit node outside declared transition matrix",f,false,true);
    Put(f.model,f.descriptors+164,-1);Case("negative exit transition node",f,false,true);
    Put(f.model,f.descriptors+164,0);Case("entry without exit transition node",f,false,true);
    f=Model();Put(f.model,f.descriptors+160,1);Case("node reference without transition matrix",f,false,true);
    f=Model();Put(f.model,236,1);Put(f.model,240,int(f.arena)-4);
    Put(f.model,f.descriptors+160,1);Put(f.model,f.descriptors+164,17);
    Case("FindTransition oversized exit reads native arena",f,false,true);
    f=Model();Put(f.model,236,2);Put(f.model,240,int(f.transitions));
    Put(f.seq[0],Word(f.seq[0],4)+160,1);Put(f.seq[0],Word(f.seq[0],4)+164,2);Case("SEQ legal transition nodes",f,true);
    Put(f.seq[0],Word(f.seq[0],4)+160,3);Case("SEQ entry outside transition matrix",f,false,true);
    Put(f.seq[0],Word(f.seq[0],4)+160,1);Put(f.seq[0],Word(f.seq[0],4)+164,0);Case("SEQ entry without exit",f,false,true);
    for(size_t field:{88u,92u}){f=Model();Put(f.seq[0],Word(f.seq[0],4)+field,1);Case("SEQ unknown automove",f,false,true);}
    f=Model();Put(f.model,180,0);Put(f.model,184,0);Put(f.model,188,0);Case("no textures",f,true);
    // Boundary unit oracle already covers 197/198. Add old-layout byte equality
    // on a large native count with independent tracks and a nonmonotonic order.
    Case("native final-index 198 boundary",Model(199,false,true),true);
}
void Resource(const std::filesystem::path& path)
{
    auto model=Read(path);csol::DecodeStudioModel(model);
    const auto names=csol::SequenceFiles(path.filename().string(),[&](const auto& p){return std::filesystem::exists(path.parent_path()/p);});
    std::vector<Bytes> seq;for(const auto& name:names)seq.push_back(Read(path.parent_path()/name));
    Require(!seq.empty(),"resource has no SEQ files");
    Bytes old,now;std::string oldError,newError;
    try{old=csol::OldAppendSequences(model,seq,csol::SequenceReadPolicy::AllowUnsupportedTracks);}catch(const std::exception& e){oldError=e.what();}
    try{now=csol::AppendSequences(model,seq,csol::SequenceReadPolicy::AllowUnsupportedTracks);}catch(const std::exception& e){newError=e.what();}
    Require(oldError==newError,"candidate changed resource rejection");
    if(!oldError.empty()){std::cout<<"RESOURCE rejected-equally "<<oldError<<'\n';return;}
    Compare(model,seq,old,now,csol::SequenceReadPolicy::AllowUnsupportedTracks);
    csol::SequenceSafetyView oldView,newView;
    Require(csol::ReadSequenceSafety(old,oldView,csol::SequenceReadPolicy::AllowUnsupportedTracks)==csol::SequenceSafetyStatus::Ready,"old audit view invalid");
    Require(csol::ReadSequenceSafety(now,newView,csol::SequenceReadPolicy::AllowUnsupportedTracks)==csol::SequenceSafetyStatus::Ready,"new audit view invalid");
    const size_t records=Word(now,Word(now,168)-12);int unsupported{};
    for(int i=0;i<Word(now,164);++i)if(!Word(now,records+size_t(i)*32+24))
    {
        const size_t a=Word(old,168)+size_t(i)*176,b=Word(now,168)+size_t(i)*176;
        const auto select=[&](const Bytes& bytes,size_t at){return csol::SequenceSelection{
            std::uint32_t(Word(bytes,at+124)),std::uint32_t(Word(bytes,at+56)),std::uint32_t(Word(bytes,at+120)),0,std::uint32_t(Word(bytes,140))};};
        const auto oldSelection=select(old,a),newSelection=select(now,b);
        for(float frame:{0.f,float(newSelection.frames-1)})
            Require(oldView.Check(oldSelection,frame)==csol::SequenceCheck::InvalidTrack &&
                    newView.Check(newSelection,frame)==csol::SequenceCheck::InvalidTrack,"unsupported old/new sampling disagreement");
        const char* label=reinterpret_cast<const char*>(now.data()+b);
        std::cout<<"UNSUPPORTED index="<<i<<" label="<<std::string_view(label,strnlen(label,32))
            <<" frames="<<newSelection.frames<<" blends="<<newSelection.blends
            <<" bytes="<<Word(now,records+size_t(i)*32+4)<<" old=InvalidTrack new=InvalidTrack\n";
        ++unsupported;
    }
    if(unsupported)
    {
        std::string strictOld,strictNew;
        try{csol::OldAppendSequences(model,seq,csol::SequenceReadPolicy::StrictAllTracks);}catch(const std::exception& e){strictOld=e.what();}
        try{csol::AppendSequences(model,seq);}catch(const std::exception& e){strictNew=e.what();}
        Require(!strictOld.empty() && strictOld==strictNew,"strict rejection changed");
        Require(csol::ReadSequenceSafety(old,oldView)==csol::SequenceSafetyStatus::Invalid &&
                csol::ReadSequenceSafety(now,newView)==csol::SequenceSafetyStatus::Invalid,"strict bounds accepted mixed tracks");
        std::cout<<"STRICT rejected-equally "<<strictOld<<'\n';
    }
    std::cout<<"TRACKS total="<<Word(now,164)<<" ready="<<Word(now,164)-unsupported<<" unsupported="<<unsupported<<'\n';
    std::cout<<"RESOURCE old="<<old.size()<<" new="<<now.size()<<" saved="<<old.size()-now.size()<<" sequences="<<Word(now,164)<<'\n';
}
}
int wmain(int argc,wchar_t** argv)
{
    try{
        if(argc==3 && std::wstring_view(argv[1])==L"--original")return PreservedSequenceTestMain(2,argv+1);
        if(argc==1)arena_test::Synthetic();else if(argc==2)arena_test::Resource(argv[1]);else throw std::runtime_error("expected zero args, model path, or --original common");
        std::cout<<"sequence_arena checks="<<arena_test::checks<<" cases="<<arena_test::cases<<" failures=0\n";return 0;
    }catch(const std::exception& e){std::cerr<<"sequence_arena FAILED: "<<e.what()<<'\n';return 1;}
}
