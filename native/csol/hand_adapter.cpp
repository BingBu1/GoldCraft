// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/runtime.hpp"
#include "hand_adapter.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace csol {
namespace {
struct Model {
    std::span<const std::byte> bytes;
    template<class T> std::span<const T> range(int offset,int count=1) const {
        if(offset<0 || count<0 || size_t(offset)>bytes.size() || size_t(count)>(bytes.size()-offset)/sizeof(T))
            throw std::runtime_error("hand adapter: model range outside file");
        return {reinterpret_cast<const T*>(bytes.data()+offset),size_t(count)};
    }
    const studiohdr_t& header() const {return range<studiohdr_t>(0)[0];}
};
template<size_t N> std::string Text(const char (&s)[N]) {
    std::string result(s,strnlen(s,N));
    for(auto& c:result)c=char(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
struct Writer {
    std::vector<std::byte> bytes;
    int append(const void* data,size_t count) {
        bytes.resize((bytes.size()+3)&~size_t(3));
        if(count>64*1024*1024 || bytes.size()+count>64*1024*1024)throw std::runtime_error("hand adapter exceeds model budget");
        const int offset=int(bytes.size());bytes.resize(bytes.size()+count);
        if(count)memcpy(bytes.data()+offset,data,count);return offset;
    }
    template<class T> int append(std::span<const T> data){return append(data.data(),data.size_bytes());}
    template<class T> void write(int offset,const T& value){memcpy(bytes.data()+offset,&value,sizeof(value));}
};
std::vector<int> MeshBones(const Model& file,const mstudiomodel_t& model,const mstudiomesh_t& mesh) {
    auto vertices=file.range<uint8_t>(model.vertinfoindex,model.numverts);
    auto normals=file.range<uint8_t>(model.norminfoindex,model.numnorms);
    std::vector<int> used;int cursor=mesh.triindex;
    for(int commands=0;;++commands) {
        if(commands>131072)throw std::runtime_error("hand adapter: triangle budget exceeded");
        const int count=file.range<int16_t>(cursor)[0];cursor+=2;if(!count)break;
        if(std::abs(count)<3)throw std::runtime_error("hand adapter: invalid triangle command");
        auto stream=file.range<int16_t>(cursor,std::abs(count)*4);cursor+=int(stream.size_bytes());
        for(int i=0;i<std::abs(count);++i) {
            const int v=stream[i*4],n=stream[i*4+1];
            if(v<0 || v>=vertices.size() || n<0 || n>=normals.size())throw std::runtime_error("hand adapter: invalid vertex");
            used.push_back(vertices[v]);used.push_back(normals[n]);
        }
    }
    std::sort(used.begin(),used.end());used.erase(std::unique(used.begin(),used.end()),used.end());return used;
}
int CopyTriangles(const Model& file,const mstudiomesh_t& mesh,Writer& out) {
    int end=mesh.triindex;
    for(int commands=0;;++commands) {
        if(commands>131072)throw std::runtime_error("hand adapter: triangle budget exceeded");
        const int n=file.range<int16_t>(end)[0];end+=2;if(!n)break;
        file.range<int16_t>(end,std::abs(n)*4);end+=std::abs(n)*8;
    }
    return out.append(file.range<std::byte>(mesh.triindex,end-mesh.triindex));
}
std::vector<int16_t> ArmTriangles(const Model& file,const mstudiomodel_t& model,
    const mstudiomesh_t& mesh,std::span<const uint8_t> sides,int mask,int& triangles) {
    const auto bones=file.range<uint8_t>(model.vertinfoindex,model.numverts);
    const auto normals=file.range<uint8_t>(model.norminfoindex,model.numnorms);
    std::vector<int16_t> output;int cursor=mesh.triindex;triangles=0;
    for(int commands=0;;++commands) {
        if(commands>131072)throw std::runtime_error("hand adapter: triangle budget exceeded");
        const int count=file.range<int16_t>(cursor)[0];cursor+=2;if(!count)break;
        const int length=std::abs(count);if(length<3)throw std::runtime_error("invalid arm triangle command");
        const auto stream=file.range<int16_t>(cursor,length*4);cursor+=int(stream.size_bytes());
        for(int i=2;i<length;++i) {
            // GoldSrc strips alternate winding; fans keep their first vertex.
            const int indices[]{count<0?0:(i%2?i-1:i-2),count<0?i-1:(i%2?i-2:i-1),i};
            int side{};
            for(int index:indices) {
                const int v=stream[index*4],n=stream[index*4+1];
                if(v<0 || v>=bones.size() || n<0 || n>=normals.size() || bones[v]>=sides.size() || normals[n]>=sides.size())
                    throw std::runtime_error("invalid donor arm vertex");
                const int next=sides[bones[v]];
                if(!next || next!=sides[normals[n]] || (side && side!=next))throw std::runtime_error("donor triangle crosses arm skeletons");
                side=next;
            }
            if(!(mask&side))continue;
            output.push_back(3);
            for(int index:indices)output.insert(output.end(),stream.begin()+index*4,stream.begin()+index*4+4);
            ++triangles;
        }
    }
    output.push_back(0);return output;
}
}
AdaptedHands AdaptViewmodelHands(std::span<const std::byte> targetBytes,std::span<const std::byte> donorBytes,const HandAdapterOptions& options) {
    AdaptedHands result;
    auto reject=[&](const char* why){result.reason=why;result.bytes.clear();result.bodygroups.clear();return result;};
    const Model target{targetBytes},donor{donorBytes};
    auto h=target.header();const auto& d=donor.header();
    for(const auto* header:std::array<const studiohdr_t*,2>{&h,&d})if(header->id!=0x54534449 || header->version!=10 || header->numbones<1 || header->numbones>128 ||
        header->numtextures<1 || header->numtextures>1024 || header->numbodyparts<1 || header->numbodyparts>64 ||
        header->numskinref<1 || header->numskinref>1024 || header->numskinfamilies<1 || header->numskinfamilies>256)
        return reject("unsupported model layout");
    auto tb=target.range<mstudiobone_t>(h.boneindex,h.numbones),db=donor.range<mstudiobone_t>(d.boneindex,d.numbones);
    std::unordered_map<std::string,int> names;
    for(int i=0;i<h.numbones;++i)if(!names.emplace(Text(tb[i].name),i).second)return reject("duplicate target bone name");
    std::vector<int> map(d.numbones,-1);std::vector<std::array<float,12>> transforms(d.numbones,{1,0,0,0,0,1,0,0,0,0,1,0});
    for(int i=0;i<d.numbones;++i)if(auto it=names.find(Text(db[i].name));it!=names.end())map[i]=it->second;
    std::unordered_set<int> explicitBones;
    for(const auto& binding:options.bones) {
        int index=-1;for(int i=0;i<d.numbones;++i)if(Text(db[i].name)==binding.donor)index=i;
        auto to=names.find(binding.target);
        if(index<0 || to==names.end() || !explicitBones.insert(index).second)return reject("invalid explicit bone mapping");
        // Accept rigid local corrections only: no shear/reflection that would
        // require different normal transforms or reverse triangle winding.
        const auto& m=binding.transform;
        for(float v:m)if(!std::isfinite(v) || std::abs(v)>4096)return reject("invalid bone transform");
        for(int a=0;a<3;++a)for(int b=0;b<3;++b){float dot{};for(int c=0;c<3;++c)dot+=m[a*4+c]*m[b*4+c];
            if(std::abs(dot-(a==b?1.f:0.f))>.001f)return reject("bone transform must be rigid");}
        const float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
        if(det<.999f)return reject("bone transform must preserve orientation");
        map[index]=to->second;transforms[index]=m;
    }
    // The donor's hands group contains complete male/female geometry. Weapon
    // bones and donor animation poses are deliberately not transplanted.
    const mstudiobodyparts_t* hands=nullptr;
    for(const auto& g:donor.range<mstudiobodyparts_t>(d.bodypartindex,d.numbodyparts))if(Text(g.name)=="hands" && g.nummodels==2)hands=&g;
    if(!hands)return reject("donor has no paired hand geometry");
    const auto handModels=donor.range<mstudiomodel_t>(hands->modelindex,2);
    std::unordered_set<int> used,textureIndices;
    const auto donorSkins=donor.range<int16_t>(d.skinindex,d.numskinref*d.numskinfamilies);
    for(const auto& model:handModels)for(const auto& mesh:donor.range<mstudiomesh_t>(model.meshindex,model.nummesh)) {
        if(mesh.skinref<0 || mesh.skinref>=d.numskinref)return reject("invalid donor skin reference");
        const int texture=donorSkins[mesh.skinref];
        if(texture<0 || texture>=d.numtextures)return reject("invalid donor texture");
        for(int family=1;family<d.numskinfamilies;++family)if(donorSkins[family*d.numskinref+mesh.skinref]!=texture)return reject("donor hands change with weapon skin");
        textureIndices.insert(texture);
        for(int b:MeshBones(donor,model,mesh))used.insert(b);
    }
    // Include ancestors: finger parents need matching lengths and topology too.
    std::vector<int> ancestors(used.begin(),used.end());
    for(size_t i=0;i<ancestors.size();++i) {
        const int b=ancestors[i];if(b<0 || b>=d.numbones)return reject("invalid donor bone");
        const int parent=db[b].parent;if(parent>=b || parent<-1)return reject("invalid donor hierarchy");
        if(parent>=0 && used.insert(parent).second)ancestors.push_back(parent);
    }
    std::unordered_set<int> targetBones;
    for(int b:used) {
        if(map[b]<0 || !targetBones.insert(map[b]).second)return reject("hand skeleton does not match");
        if(!explicitBones.contains(b)) {
            const int parent=db[b].parent;
            if(tb[map[b]].parent!=(parent>=0?map[parent]:-1))return reject("hand bone hierarchy differs");
            // Root transforms and joint angles are animation poses; retaining
            // the target values preserves its grip. Child translations encode
            // the actual rig dimensions and must agree for automatic mapping.
            if(parent>=0)for(int axis=0;axis<3;++axis)if(std::abs(tb[map[b]].value[axis]-db[b].value[axis])>.05f)return reject("hand bone lengths differ; mapping required");
        }
    }
    result.mappedBones=int(targetBones.size());
    std::vector<uint8_t> donorSides(d.numbones),targetSides(h.numbones);
    for(int b:used) {
        int root=b;while(db[root].parent>=0)root=db[root].parent;
        const auto name=Text(db[root].name);
        const uint8_t side=name=="bone01"?1:name=="bone04"?2:0;
        donorSides[b]=side;targetSides[map[b]]=side;
    }
    std::unordered_set<std::string> handTextures;
    if(options.textures.empty())handTextures={"view_glove.bmp","view_skin.bmp","view_finger.bmp","hand.bmp","thumb.bmp"};
    else for(const auto& name:options.textures)handTextures.insert(name);
    const auto textures=target.range<mstudiotexture_t>(h.textureindex,h.numtextures);
    const auto skins=target.range<int16_t>(h.skinindex,h.numskinref*h.numskinfamilies);
    const auto groups=target.range<mstudiobodyparts_t>(h.bodypartindex,h.numbodyparts);
    struct Submodel {mstudiomodel_t model;std::vector<mstudiomesh_t> meshes;};
    std::vector<std::vector<Submodel>> kept;bool left=false,right=false,conditional=false,incompleteArms=false;int weaponMeshes{};
    for(const auto& group:groups) {
        if(group.nummodels<1 || group.nummodels>256 || group.base<1)return reject("unsupported bodygroup");
        std::vector<Submodel> models;
        HandBodygroup visibility{group.base,{}};
        for(const auto& model:target.range<mstudiomodel_t>(group.modelindex,group.nummodels)) {
            Submodel sub{model,{}};
            uint8_t arms{};std::unordered_set<int> coverage;
            for(const auto& mesh:target.range<mstudiomesh_t>(model.meshindex,model.nummesh)) {
                if(mesh.skinref<0 || mesh.skinref>=h.numskinref)return reject("invalid target skin reference");
                bool hand=true,any=false;
                for(int family=0;family<h.numskinfamilies;++family) {
                    const int texture=skins[family*h.numskinref+mesh.skinref];if(texture<0 || texture>=h.numtextures)return reject("invalid target texture");
                    const bool match=handTextures.contains(Text(textures[texture].name));hand&=match;any|=match;
                }
                if(any && !hand)return reject("skin families disagree about hand mesh");
                if(!hand){sub.meshes.push_back(mesh);++weaponMeshes;continue;}
                const auto bones=MeshBones(target,model,mesh);
                for(int b:bones)if(!targetBones.contains(b))return reject("hand texture also covers weapon geometry");
                for(int b:bones) {
                    arms|=targetSides[b];
                    // Coverage follows the verified donor hierarchy, including
                    // explicitly mapped rigs, and cannot loop on target data.
                    for(int donorBone:used)if(map[donorBone]==b)
                        for(int p=donorBone;p>=0;p=db[p].parent)coverage.insert(map[p]);
                }
                ++result.removedMeshes;
            }
            if(arms)for(int b:used)
                if((donorSides[b]&arms) && !coverage.contains(map[b]))
                    incompleteArms=true;
            left|=(arms&1)!=0;right|=(arms&2)!=0;
            visibility.visibility.push_back(arms);
            models.push_back(std::move(sub));
        }
        conditional|=std::any_of(visibility.visibility.begin(),visibility.visibility.end(),[&](uint8_t mask){return mask!=visibility.visibility.front();});
        result.bodygroups.push_back(std::move(visibility));
        kept.push_back(std::move(models));
    }
    if(!result.removedMeshes || !weaponMeshes)return reject("no separate hand and weapon meshes");
    if(options.bones.empty() && (!left || !right))return reject("incomplete standard hand rig");
    if(conditional && incompleteArms)return reject("conditional bodygroup contains a partial arm");
    if(!conditional)result.bodygroups.clear();
    else for(int b:used)if(!donorSides[b])return reject("conditional donor needs separate left and right arm roots");
    Writer out{{targetBytes.begin(),targetBytes.end()}};
    std::vector<mstudiobodyparts_t> outputGroups(groups.begin(),groups.end());
    int base=1;
    for(size_t g=0;g<kept.size();++g) {
        std::vector<mstudiomodel_t> models;
        for(auto& sub:kept[g]) {
            sub.model.nummesh=int(sub.meshes.size());sub.model.meshindex=out.append(std::span<const mstudiomesh_t>(sub.meshes));models.push_back(sub.model);
        }
        outputGroups[g].modelindex=out.append(std::span<const mstudiomodel_t>(models));
        if(groups[g].base>std::numeric_limits<int>::max()/groups[g].nummodels)return reject("bodygroup base overflow");
        base=std::max(base,groups[g].base*groups[g].nummodels);
    }
    std::vector<mstudiotexture_t> outputTextures(textures.begin(),textures.end());
    std::unordered_map<int,int> textureMap;
    std::vector<int> orderedTextures(textureIndices.begin(),textureIndices.end());std::sort(orderedTextures.begin(),orderedTextures.end());
    for(int texture:orderedTextures) {
        auto t=donor.range<mstudiotexture_t>(d.textureindex,d.numtextures)[texture];
        if(t.width<1 || t.height<1 || t.width>4096 || t.height>4096)return reject("invalid donor texture dimensions");
        t.index=out.append(donor.range<std::byte>(t.index,t.width*t.height+768));
        textureMap[texture]=int(outputTextures.size());outputTextures.push_back(t);
    }
    std::vector<mstudiomodel_t> transplanted;
    for(int gender=0;gender<2;++gender) {
        auto model=handModels[gender];strcpy_s(model.name,gender?"cso_hand_female":"cso_hand_male");
        auto copyVertices=[&](int count,int& boneIndex,int& vectorIndex,bool normal) {
            const auto bones=donor.range<uint8_t>(boneIndex,count);const auto points=donor.range<std::array<float,3>>(vectorIndex,count);
            std::vector<uint8_t> indices;std::vector<std::array<float,3>> positions;
            for(int i=0;i<count;++i) {
                const int b=bones[i];if(b>=map.size() || map[b]<0)throw std::runtime_error("unmapped donor vertex");
                indices.push_back(uint8_t(map[b]));std::array<float,3> p{};
                for(int axis=0;axis<3;++axis){p[axis]=normal?0:transforms[b][axis*4+3];for(int c=0;c<3;++c)p[axis]+=transforms[b][axis*4+c]*points[i][c];}
                positions.push_back(p);
            }
            boneIndex=out.append(std::span<const uint8_t>(indices));vectorIndex=out.append(std::span<const std::array<float,3>>(positions));
        };
        copyVertices(model.numverts,model.vertinfoindex,model.vertindex,false);
        copyVertices(model.numnorms,model.norminfoindex,model.normindex,true);
        for(int variant=0;variant<(conditional?4:1);++variant) {
            auto selected=model;std::vector<mstudiomesh_t> meshes;
            if(conditional)strcpy_s(selected.name,"csol_conditional_arm");
            for(auto mesh:donor.range<mstudiomesh_t>(handModels[gender].meshindex,handModels[gender].nummesh)) {
                if(conditional) {
                    int count{};auto stream=ArmTriangles(donor,handModels[gender],mesh,donorSides,variant,count);
                    if(!count)continue;
                    mesh.triindex=out.append(std::span<const int16_t>(stream));mesh.numtris=count;
                } else mesh.triindex=CopyTriangles(donor,mesh,out);
                mesh.skinref=h.numskinref+textureMap.at(donorSkins[mesh.skinref])-h.numtextures;
                meshes.push_back(mesh);
            }
            selected.nummesh=int(meshes.size());selected.meshindex=out.append(std::span<const mstudiomesh_t>(meshes));transplanted.push_back(selected);
        }
    }
    mstudiobodyparts_t group{};strcpy_s(group.name,"csol_adapted_hands");group.nummodels=int(transplanted.size());group.base=base;
    group.modelindex=out.append(std::span<const mstudiomodel_t>(transplanted));outputGroups.push_back(group);
    const int added=int(outputTextures.size())-h.numtextures;std::vector<int16_t> outputSkins;
    for(int family=0;family<h.numskinfamilies;++family) {
        outputSkins.insert(outputSkins.end(),skins.begin()+family*h.numskinref,skins.begin()+(family+1)*h.numskinref);
        for(int i=0;i<added;++i)outputSkins.push_back(int16_t(h.numtextures+i));
    }
    h.bodypartindex=out.append(std::span<const mstudiobodyparts_t>(outputGroups));h.numbodyparts=int(outputGroups.size());
    h.textureindex=out.append(std::span<const mstudiotexture_t>(outputTextures));h.numtextures=int(outputTextures.size());
    h.skinindex=out.append(std::span<const int16_t>(outputSkins));h.numskinref+=added;
    h.length=int(out.bytes.size());out.write(0,h);result.bytes=std::move(out.bytes);return result;
}
}
