// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequences.hpp"
#include "model_codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <span>
#include <cstdint>

static std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot read sequence fixture");
    std::vector<std::byte> b(size_t(f.tellg()));f.seekg(0);f.read((char*)b.data(),b.size());return b;
}
static int Word(const std::vector<std::byte>& b,size_t at){int n;memcpy(&n,b.data()+at,4);return n;}
static void Put(std::vector<std::byte>& b,size_t at,int n){memcpy(b.data()+at,&n,4);}
static void Check(bool v){if(!v)throw std::runtime_error("sequence invariant failed");}
template<class T> static T Value(std::span<const std::byte> bytes,size_t at)
{
    if(at>bytes.size() || sizeof(T)>bytes.size()-at)
        throw std::runtime_error("bounded animation read outside payload: offset="+std::to_string(at)+
            " bytes="+std::to_string(sizeof(T))+" size="+std::to_string(bytes.size()));
    T result;memcpy(&result,bytes.data()+at,sizeof(result));return result;
}
template<class T> static void Store(std::vector<std::byte>& bytes,size_t at,T value)
{ Check(at<=bytes.size() && sizeof(T)<=bytes.size()-at);memcpy(bytes.data()+at,&value,sizeof(value)); }
using Scales=std::vector<std::array<float,6>>;
static Scales BoneUnits(const std::vector<std::byte>& model)
{
    Scales result(Word(model,140));const size_t start=Word(model,144);
    for(size_t bone=0;bone<result.size();++bone)for(int axis=0;axis<6;++axis)
        result[bone][axis]=Value<float>(model,start+bone*112+88+axis*4);
    return result;
}
struct FrameReads {short first{},second{};bool nextRun{};};
// Bounded read-set oracle for the actual GoldSrc position/quaternion channel
// branches (ReGameDLL animation.cpp). Rotation reads angle2 even at s=0 and
// on a one-frame sequence. Position's last explicit sample instead holds.
// Byte roles cover all channels so a sample cannot alias another run header.
class AnimationOracle
{
public:
    AnimationOracle(std::span<const std::byte> block,int bones,int blends) : m_block(block),m_roles(block.size()),m_tables(size_t(bones)*blends*12)
    {
        Check(m_tables<=block.size());std::fill_n(m_roles.begin(),m_tables,std::uint8_t{1});
    }
    std::vector<FrameReads> Read(size_t table,int axis,int count,bool source=false)
    {
        const auto offset=Value<std::uint16_t>(m_block,table+axis*2);
        if(!offset)return std::vector<FrameReads>(count);
        Check(table+offset>=m_tables);
        std::vector<FrameReads> result;result.reserve(count);
        for(int frame=0;frame<count;++frame)
        {
            size_t at=table+offset;int k=frame;
            auto run=Header(at);
            while(run.total<=k)
            {k-=run.total;at+=2*(run.valid+1);run=Header(at);}
            const unsigned first=run.valid>k?unsigned(k+1):run.valid;
            unsigned second=first;
            if(axis>=3)
            {
                if(run.valid>k+1)second=unsigned(k+2);
                else if(run.total<=k+1)second=run.valid+2;
            }
            else if(run.valid>k)
            {
                if(run.valid>k+1)second=unsigned(k+2);
            }
            else if(run.total<=k+1)second=run.valid+2;
            const short firstValue=Sample(at+2*first);
            // The real resource has no guaranteed terminal lookahead. Its
            // final frame is sampled at fraction0, so only first contributes.
            // The output must still make the actual consumer read safe; that
            // unused read may alias another channel's header, and must not be
            // classified/requantized as a meaningful sample in that channel.
            const bool terminal=frame+1==count;
            const short secondValue=terminal?(source?firstValue:Value<short>(m_block,at+2*second)):
                Sample(at+2*second);
            result.push_back({firstValue,secondValue,second==run.valid+2});
        }
        return result;
    }
private:
    struct Run {unsigned valid,total;};
    void Mark(size_t at,std::uint8_t role)
    {
        Check(at<=m_block.size() && 2<=m_block.size()-at);
        for(size_t i=at;i<at+2;++i)
        {Check(!m_roles[i] || m_roles[i]==role);m_roles[i]=role;}
    }
    Run Header(size_t at)
    {
        Mark(at,2);Run run{Value<std::uint8_t>(m_block,at),Value<std::uint8_t>(m_block,at+1)};
        Check(run.valid>0 && run.total>=run.valid);return run;
    }
    short Sample(size_t at){Mark(at,4);return Value<short>(m_block,at);}
    std::span<const std::byte> m_block;
    std::vector<std::uint8_t> m_roles;
    size_t m_tables;
};
struct Samples {std::uint64_t channels{},frames{},held{},transitions{},nextRunReads{},finalNextRunReads{};};
static Samples CompareUnits(const std::vector<std::byte>& base,
    const std::vector<std::vector<std::byte>>& files,const std::vector<std::byte>& merged)
{
    const auto original=BoneUnits(base),target=BoneUnits(merged);
    Check(original.size()==target.size());
    const int total=Word(merged,164),descriptors=Word(merged,168),bones=int(original.size());
    for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
    {
        const float fixed=axis<3?1.f/256:1.f/10240;
        const float expected=total>198?std::max(original[bone][axis],fixed):original[bone][axis];
        Check(std::isfinite(target[bone][axis]) && target[bone][axis]==expected);
    }
    Samples stats;int index{};
    const auto track=[&](std::span<const std::byte> descriptor,std::span<const std::byte> source)
    {
        const int frames=Value<int>(descriptor,56),blends=Value<int>(descriptor,120);
        const size_t mergedDescriptor=descriptors+size_t(index)*176;
        Check(frames==Value<int>(merged,mergedDescriptor+56) && blends==Value<int>(merged,mergedDescriptor+120));
        const size_t anim=Value<int>(merged,mergedDescriptor+124);
        size_t end=descriptors;
        for(int other=0;other<total;++other)
        {
            const size_t next=Value<int>(merged,descriptors+size_t(other)*176+124);
            if(next>anim)end=std::min(end,next);
        }
        Check(end>anim);
        const auto packed=std::span<const std::byte>(merged).subspan(anim,end-anim);
        AnimationOracle sourceReads(source,bones,blends),packedReads(packed,bones,blends);
        for(int blend=0;blend<blends;++blend)for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
        {
            const size_t table=(size_t(blend)*bones+bone)*12;
            const auto before=sourceReads.Read(table,axis,frames,true),after=packedReads.Read(table,axis,frames);
            // Original normal-gameplay selector uses the sampled final index.
            const double unit=index>=198?(axis<3?1.f/256:1.f/10240):original[bone][axis];
            const double scale=target[bone][axis],epsilon=scale*.50001+1e-10;
            ++stats.channels;stats.frames+=frames;
            for(int frame=0;frame<frames;++frame)
            {
                const double expected=before[frame].first*unit,actual=after[frame].first*scale;
                if(!std::isfinite(actual) || std::abs(actual-expected)>epsilon)
                    throw std::runtime_error("pose unit mismatch at sequence "+std::to_string(index)+
                        " bone "+std::to_string(bone)+" axis "+std::to_string(axis));
                if(total<=198)Check(before[frame].first==after[frame].first);
                // Nonfinal interpolation uses actual two-sample consumer
                // reads. Legal final-frame fraction is0; its extra read is
                // bounds-tested above but its value must not alter the pose.
                if(frame+1<frames)
                {
                    const double expectedSecond=before[frame].second*unit,actualSecond=after[frame].second*scale;
                    Check(std::isfinite(actualSecond) && std::abs(actualSecond-expectedSecond)<=epsilon);
                    if(total<=198)Check(before[frame].second==after[frame].second);
                    const double expectedHalf=(before[frame].first+double(before[frame].second))*.5*unit;
                    const double actualHalf=(after[frame].first+double(after[frame].second))*.5*scale;
                    Check(std::isfinite(actualHalf) && std::abs(actualHalf-expectedHalf)<=epsilon);
                    if(before[frame].first==before[frame].second)++stats.held;else ++stats.transitions;
                }
                if(before[frame].nextRun){++stats.nextRunReads;if(frame+1==frames)++stats.finalNextRunReads;}
            }
        }
        ++index;
    };
    const auto native=std::span<const std::byte>(base);
    const int nativeEnd=Word(base,180)>0?Word(base,188):Word(base,72);
    for(int sequence=0;sequence<Word(base,164);++sequence)
    {
        const auto descriptor=native.subspan(Word(base,168)+size_t(sequence)*176,176);
        const size_t anim=Value<int>(descriptor,124);
        track(descriptor,native.subspan(anim,size_t(nativeEnd)-anim));
    }
    for(const auto& file:files)
    {
        const auto bytes=std::span<const std::byte>(file);const int count=Word(file,0),start=Word(file,4);
        size_t cursor=8;
        for(int sequence=0;sequence<count;++sequence)
        {
            const size_t descriptor=start+size_t(sequence)*176;
            cursor+=size_t(Value<int>(bytes,descriptor+48))*76+size_t(Value<int>(bytes,descriptor+60))*20;
        }
        for(int sequence=0;sequence<count;++sequence)
        {
            const auto descriptor=bytes.subspan(start+size_t(sequence)*176,176);
            const size_t end=sequence+1==count?size_t(start):cursor+
                size_t(Value<int>(bytes,start+size_t(sequence+1)*176+124)-Value<int>(descriptor,124));
            Check(end>=cursor && end<=size_t(start));track(descriptor,bytes.subspan(cursor,end-cursor));cursor=end;
        }
    }
    Check(index==total);return stats;
}
static std::vector<std::byte> SyntheticAnimation(int bones)
{
    constexpr int blends=2;const size_t tables=size_t(bones)*blends*12;
    std::vector<std::byte> result(tables);
    for(int blend=0;blend<blends;++blend)for(int bone=0;bone<bones;++bone)for(int axis=0;axis<6;++axis)
    {
        const size_t table=(size_t(blend)*bones+bone)*12,at=result.size();
        result.resize(at+18);Store(result,table+axis*2,static_cast<std::uint16_t>(at-table));
        result[at]=std::byte{2};result[at+1]=std::byte{3};
        Store(result,at+2,short(-32768));Store(result,at+4,short(1234+blend+bone+axis));
        result[at+6]=std::byte{3};result[at+7]=std::byte{3};
        Store(result,at+8,short(32767));Store(result,at+10,short(-2000));Store(result,at+12,short(0));
        // Rotation's final sample reads the next run's first value. This is
        // dedicated tail storage, never the adjacent channel's header/value.
        result[at+14]=std::byte{1};result[at+15]=std::byte{1};Store(result,at+16,short(-12345));
    }
    return result;
}
static std::vector<std::byte> ModelWithAnimation(int count,const Scales& scales,
    const std::vector<std::byte>& block,int frames,int blends=1)
{
    const int descriptors=244+int(scales.size())*112,anim=descriptors+count*176;
    std::vector<std::byte> result(anim);
    result.insert(result.end(),block.begin(),block.end());
    Put(result,0,0x54534449);Put(result,4,10);Put(result,72,int(result.size()));
    Put(result,140,int(scales.size()));Put(result,144,244);Put(result,164,count);Put(result,168,descriptors);
    for(size_t bone=0;bone<scales.size();++bone)for(int axis=0;axis<6;++axis)
        Store(result,244+bone*112+88+axis*4,scales[bone][axis]);
    for(int sequence=0;sequence<count;++sequence)
    {const size_t at=descriptors+size_t(sequence)*176;Put(result,at+56,frames);Put(result,at+120,blends);Put(result,at+124,anim);}
    return result;
}
static std::vector<std::byte> SeqWithAnimation(int count,const std::vector<std::byte>& block,int frames,int blends=1)
{
    const size_t start=8+count*block.size();
    std::vector<std::byte> result(start+count*176);Put(result,0,count);Put(result,4,int(start));
    for(int sequence=0;sequence<count;++sequence)
    {
        memcpy(result.data()+8+sequence*block.size(),block.data(),block.size());
        const size_t at=start+size_t(sequence)*176;
        Put(result,at+56,frames);Put(result,at+120,blends);Put(result,at+124,10000+int(sequence*block.size()));
    }
    return result;
}
static std::vector<std::byte> SyntheticModel(int count,const Scales& scales)
{ return ModelWithAnimation(count,scales,SyntheticAnimation(int(scales.size())),6,2); }
static std::vector<std::byte> SyntheticSeq(int count,int bones)
{ return SeqWithAnimation(count,SyntheticAnimation(bones),6,2); }
static std::string Rejected(const std::vector<std::byte>& base,const std::vector<std::vector<std::byte>>& seq)
{
    try{csol::AppendSequences(base,seq);}catch(const std::exception& error){return error.what();}
    throw std::runtime_error("malformed animation was accepted");
}
static std::vector<std::byte> TailAnimation(int axis,int total,bool tail)
{
    std::vector<std::byte> block(tail?20:16);
    Store(block,axis*2,std::uint16_t{12});
    block[12]=std::byte{1};block[13]=std::byte(total);Store(block,14,short(1234));
    if(tail){block[16]=std::byte{1};block[17]=std::byte{1};Store(block,18,short(-23456));}
    return block;
}
static void CheckReadSafety()
{
    const Scales scales{{.01f,.01f,.01f,.0003f,.0003f,.0003f}};
    for(const auto fixture:{std::array{3,1},std::array{0,2}})
    {
        const int axis=fixture[0],frames=fixture[1];const auto block=TailAnimation(axis,frames,true);
        AnimationOracle oracle(block,1,1);const auto expected=oracle.Read(0,axis,frames);
        Check(expected.back().first==1234 && expected.back().second==-23456 && expected.back().nextRun);
        for(int count:{1,198})
        {
            const auto base=ModelWithAnimation(count,scales,block,frames);
            const std::vector<std::vector<std::byte>> files{SeqWithAnimation(1,block,frames)};
            const auto stats=CompareUnits(base,files,csol::AppendSequences(base,files));
            Check(stats.finalNextRunReads==std::uint64_t(count+1));
            // Real resources omit terminal lookahead: legal values remain
            // accepted and packed output must provide readable unused bytes.
            // Cover no bytes, header-only, and a partial unused tail sample.
            for(size_t size:{size_t(16),size_t(18),size_t(19)})
            {
                auto missing=block;missing.resize(size);
                const auto missingNative=ModelWithAnimation(count,scales,missing,frames);
                CompareUnits(missingNative,files,csol::AppendSequences(missingNative,files));
                const std::vector<std::vector<std::byte>> missingSeq{SeqWithAnimation(1,missing,frames)};
                CompareUnits(base,missingSeq,csol::AppendSequences(base,missingSeq));
            }
            // This is the actual in-range first sample, never optional tail.
            auto missingValue=block;missingValue.resize(15);
            Rejected(ModelWithAnimation(count,scales,missingValue,frames),files);
            Rejected(base,{SeqWithAnimation(1,missingValue,frames)});
            // Two frames demanded from a one-frame run require a second run.
            const auto missingRun=TailAnimation(axis,1,false);
            Rejected(ModelWithAnimation(count,scales,missingRun,2),files);
            Rejected(base,{SeqWithAnimation(1,missingRun,2)});
        }
    }
    // Position valid=total=1 holds its sample: no fictional tail requirement.
    const auto position=TailAnimation(0,1,false);
    const auto positionModel=ModelWithAnimation(1,scales,position,1);
    const std::vector<std::vector<std::byte>> positionSeq{SeqWithAnimation(1,position,1)};
    CompareUnits(positionModel,positionSeq,csol::AppendSequences(positionModel,positionSeq));

    // Two channels may share both a run and its sampled values when the unit
    // conversion agrees. Rotation's lookahead is shared in this case too.
    std::vector<std::byte> shared(28);
    Store(shared,0,std::uint16_t{12});Store(shared,2,std::uint16_t{12});
    Store(shared,6,std::uint16_t{20});Store(shared,8,std::uint16_t{20});
    shared[12]=shared[13]=shared[20]=shared[21]=shared[24]=shared[25]=std::byte{1};
    Store(shared,14,short(15000));Store(shared,22,short(-20000));Store(shared,26,short(23000));
    for(int count:{1,198})
    {
        const auto base=ModelWithAnimation(count,scales,shared,1);
        const std::vector<std::vector<std::byte>> files{SeqWithAnimation(1,shared,1)};
        CompareUnits(base,files,csol::AppendSequences(base,files));
    }
    for(size_t overlap:{size_t(14),size_t(15)})
    {
        // Position's sample occupies14/15, rotation's run header occupies
        //14/15 or15/16. Reject either full or single-byte role overlap.
        std::vector<std::byte> block(overlap+8);
        Store(block,0,std::uint16_t{12});Store(block,6,static_cast<std::uint16_t>(overlap));
        block[12]=block[13]=std::byte{1};Store(block,14,short(258));
        block[overlap]=block[overlap+1]=std::byte{1};Store(block,overlap+2,short(-30000));
        block[overlap+4]=block[overlap+5]=std::byte{1};Store(block,overlap+6,short(31000));
        for(int count:{1,198})
        {
            const auto good=ModelWithAnimation(count,scales,shared,1);
            const std::vector<std::vector<std::byte>> goodSeq{SeqWithAnimation(1,shared,1)};
            Rejected(ModelWithAnimation(count,scales,block,1),goodSeq);
            Rejected(good,{SeqWithAnimation(1,block,1)});
        }
    }
    std::cout<<"PASS bounded GoldSrc final reads, absent terminal padding accepted, missing legal samples/runs and header aliases rejected; compatible sharing retained\n";
}
static void CheckUnits()
{
    const Scales mixed{{0.f,.01f,.002f,.000012f,.0003f,0.f},{.02f,0.f,.001f,.00005f,0.f,.001f}};
    // Final197/198 in MDL, across MDL/SEQ, within SEQ1, within SEQ2 and
    // between SEQ1/SEQ2; source-file identity must never choose the unit.
    for(const auto counts:{std::array{199,1,1},std::array{198,1,1},std::array{196,3,1},
        std::array{195,2,2},std::array{197,1,1},std::array{1,197,1},std::array{196,1,1},std::array{1,1,1}})
    {
        const auto base=SyntheticModel(counts[0],mixed);
        const std::vector seq{SyntheticSeq(counts[1],2),SyntheticSeq(counts[2],2)};
        const auto merged=csol::AppendSequences(base,seq);const auto stats=CompareUnits(base,seq,merged);
        Check(stats.held && stats.transitions);
        std::cout<<"PASS synthetic native="<<counts[0]<<" seq1="<<counts[1]<<" seq2="<<counts[2]<<" frames="<<stats.frames<<'\n';
    }
    const auto zero=SyntheticModel(196,Scales(2));const std::vector zeroSeq{SyntheticSeq(1,2),SyntheticSeq(1,2)};
    CompareUnits(zero,zeroSeq,csol::AppendSequences(zero,zeroSeq));
    auto invalid=SyntheticModel(1,mixed);const std::vector<std::vector<std::byte>> seq{SyntheticSeq(1,2)};
    Store(invalid,244+88,-.01f);Rejected(invalid,seq);
    // The equal-unit fast copy must still reject malformed native and SEQ RLE.
    auto badNative=zero;const int anim=Word(badNative,Word(badNative,168)+124);
    badNative[anim+48+1]=std::byte{};Rejected(badNative,zeroSeq);
    auto badSeq=zeroSeq;badSeq[0][8+48+1]=std::byte{};Rejected(zero,badSeq);
    std::cout<<"PASS zero scales preserved without NaN; equal-unit native/SEQ RLE validation retained\n";
}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        CheckReadSafety();
        CheckUnits();
        if(argc!=2)throw std::runtime_error("usage: csol_sequence_tests <common>");
        const auto root=std::filesystem::path(argv[1]);
        int tested=0,appended=0;
        for(const auto* name:{"leet","urban"})
        {
            const auto path=std::string("models/player/")+name+"/"+name+".mdl";
            auto base=Read(root/path);csol::DecodeStudioModel(base);
            auto files=csol::SequenceFiles(path,[&](const auto& s){return std::filesystem::exists(root/s);});
            Check(!files.empty());
            std::vector<std::vector<std::byte>> seq;
            int count=Word(base,164);
            for(const auto& f:files){seq.push_back(Read(root/f));count+=Word(seq.back(),0);}
            auto merged=csol::AppendSequences(base,seq);
            const auto samples=CompareUnits(base,seq,merged);
            Check(Word(merged,164)==count && Word(merged,72)==merged.size());
            const int old=Word(base,168),now=Word(merged,168);
            for(int i=0;i<Word(base,164);++i)for(size_t j=0;j<176;++j)
                if(j<124 || j>=128)Check(base[old+i*176+j]==merged[now+i*176+j]);
            // Loaders repack textures; appended sequences must precede them.
            const auto oldTextures=Word(base,188),newTextures=Word(merged,188);
            Check(now+count*176==newTextures);
            Check(!memcmp(base.data()+oldTextures,merged.data()+newTextures,Word(base,72)-oldTextures));
            for(size_t i=244;i<size_t(oldTextures);++i)
            {
                const auto relative=int(i)-Word(base,184);
                if(relative>=0 && relative<Word(base,180)*80 && relative%80>=76)continue;
                const auto boneRelative=int(i)-Word(base,144);
                if(boneRelative>=0 && boneRelative<Word(base,140)*112 && boneRelative%112>=88)continue;
                Check(base[i]==merged[i]);
            }
            int descriptor=Word(base,164);
            for(const auto& data:seq)
            {
                for(int i=0;i<Word(data,0);++i,++descriptor)
                {
                    const size_t a=Word(data,4)+i*176,b=now+descriptor*176;
                    for(size_t j=0;j<176;++j)if(!(j>=52&&j<56)&&!(j>=64&&j<68)&&!(j>=124&&j<128))Check(data[a+j]==merged[b+j]);
                }
            }
            for(int variant=0;variant<5;++variant)
            {
                auto bad=seq;
                if(variant==0)bad[0].resize(7);
                if(variant==1)Put(bad[0],0,2049);
                if(variant==2)Put(bad[0],Word(bad[0],4)+48,-1);
                if(variant==3)Put(bad[0],Word(bad[0],4)+156,1);
                if(variant==4)Put(bad[0],Word(bad[0],4)+176+124,-1);
                bool rejected=false;try{csol::AppendSequences(base,bad);}catch(const std::exception&){rejected=true;}Check(rejected);
            }
            ++tested;appended+=count-Word(base,164);
            std::cout<<name<<" files="<<files.size()<<" sequences="<<count<<" bytes="<<merged.size()
                <<" channels="<<samples.channels<<" frames="<<samples.frames<<" held="<<samples.held<<" transitions="<<samples.transitions
                <<" nextRunReads="<<samples.nextRunReads<<" finalNextRunReads="<<samples.finalNextRunReads<<'\n';
        }
        const std::string malformed="models/player/zombi_host/zombi_host.mdl";
        auto zombie=Read(root/malformed);csol::DecodeStudioModel(zombie);
        const auto zombieFiles=csol::SequenceFiles(malformed,[&](const auto& s){return std::filesystem::exists(root/s);});
        Check(!zombieFiles.empty());std::vector<std::vector<std::byte>> zombieSeq;
        for(const auto& file:zombieFiles)zombieSeq.push_back(Read(root/file));
        const auto zombieError=Rejected(zombie,zombieSeq);
        std::cout<<"PASS zombi_host remains rejected: "<<zombieError<<"; scenario first wave is NOT accepted\n";
        std::cout<<"PASS models="<<tested<<" appended="<<appended<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
