// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../native/csol/client/assets.hpp"

namespace bodygroup_fixture {
using namespace csol::client;
template<class T> int Append(std::vector<std::byte>& bytes,std::span<const T> values) {
    bytes.resize((bytes.size()+3)&~size_t(3));const int offset=int(bytes.size());
    bytes.resize(bytes.size()+values.size_bytes());
    if(!values.empty())memcpy(bytes.data()+offset,values.data(),values.size_bytes());return offset;
}
// Toggle each real source bodypart independently, including left/right hands
// and weapon geometry. Original triangle commands stay untouched.
inline std::vector<std::byte> Make(const std::vector<std::byte>& source) {
    auto out=source;auto header=Range<studiohdr_t>(source,0)[0];
    auto groups=Range<mstudiobodyparts_t>(source,header.bodypartindex,header.numbodyparts);
    std::vector<mstudiobodyparts_t> output;int base=1;
    for(auto group:groups) {
        if(group.nummodels!=1)throw std::runtime_error("fixture needs one submodel per part");
        const std::array<mstudiomodel_t,2> states{Range<mstudiomodel_t>(source,group.modelindex)[0],mstudiomodel_t{}};
        group.modelindex=Append(out,std::span<const mstudiomodel_t>(states));group.nummodels=2;group.base=base;base*=2;output.push_back(group);
    }
    header.bodypartindex=Append(out,std::span<const mstudiobodyparts_t>(output));header.length=int(out.size());
    memcpy(out.data(),&header,sizeof(header));return out;
}
// Read the production GPU buffers, not the adapter's triangle parser. A cyclic
// rotation is equivalent, but reversing winding must remain a different key.
using Triangle=std::array<float,30>;
inline std::vector<Triangle> Triangles(const Asset& asset,int group,int submodel) {
    GLint bytes{};glBindBuffer(GL_ARRAY_BUFFER,asset.vbo);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);
    std::vector<Vertex> vertices(bytes/sizeof(Vertex));glGetBufferSubData(GL_ARRAY_BUFFER,0,bytes,vertices.data());
    glBindVertexArray(asset.vao);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,asset.ibo);glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);
    std::vector<unsigned> indices(bytes/sizeof(unsigned));glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,bytes,indices.data());
    const auto bones=asset.Data<mstudiobone_t>(asset.Header().boneindex,asset.Header().numbones);
    std::vector<std::string> names;for(const auto& bone:bones)names.push_back(Name(bone.name));std::sort(names.begin(),names.end());
    auto index=[&](int bone){return float(std::lower_bound(names.begin(),names.end(),Name(bones[bone].name))-names.begin());};
    std::vector<Triangle> triangles;
    for(const auto& batch:asset.batches)if(batch.group==group && batch.submodel==submodel)
        for(unsigned i=0;i<batch.count;i+=3) {
            Triangle best{};
            for(int rotation=0;rotation<3;++rotation) {
                Triangle key{};
                for(int corner=0;corner<3;++corner) {
                    const auto& v=vertices.at(indices.at(batch.offset+i+(corner+rotation)%3));auto at=key.begin()+corner*10;
                    std::copy_n(v.position,3,at);std::copy_n(v.normal,3,at+3);std::copy_n(v.uv,2,at+6);at[8]=index(v.bone);at[9]=index(v.normalBone);
                }
                if(!rotation || key<best)best=key;
            }
            triangles.push_back(best);
        }
    std::sort(triangles.begin(),triangles.end());return triangles;
}
}
