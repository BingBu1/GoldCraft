// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequences.hpp"
#include "model_codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>

static std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot read sequence fixture");
    std::vector<std::byte> b(size_t(f.tellg()));f.seekg(0);f.read((char*)b.data(),b.size());return b;
}
static int Word(const std::vector<std::byte>& b,size_t at){int n;memcpy(&n,b.data()+at,4);return n;}
static void Put(std::vector<std::byte>& b,size_t at,int n){memcpy(b.data()+at,&n,4);}
static void Check(bool v){if(!v)throw std::runtime_error("sequence invariant failed");}
int wmain(int argc,wchar_t** argv)
{
    try
    {
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
            Check(Word(merged,164)==count && Word(merged,72)==merged.size());
            const int old=Word(base,168),now=Word(merged,168);
            Check(!memcmp(base.data()+old,merged.data()+now,size_t(Word(base,164))*176));
            // Loaders repack textures; appended sequences must precede them.
            const auto oldTextures=Word(base,188),newTextures=Word(merged,188);
            Check(now+count*176==newTextures);
            Check(!memcmp(base.data()+oldTextures,merged.data()+newTextures,Word(base,72)-oldTextures));
            for(size_t i=244;i<size_t(oldTextures);++i)
            {
                const auto relative=int(i)-Word(base,184);
                if(relative>=0 && relative<Word(base,180)*80 && relative%80>=76)continue;
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
            std::cout<<name<<" files="<<files.size()<<" sequences="<<count<<" bytes="<<merged.size()<<'\n';
        }
        std::cout<<"PASS models="<<tested<<" appended="<<appended<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
