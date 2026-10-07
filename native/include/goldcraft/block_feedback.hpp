#pragma once
#include "goldcraft/wire.hpp"

namespace goldcraft {
struct FeedbackVertex {Vec3 position;float u,v;std::uint32_t color;};
struct CrackMesh {unsigned stage;std::vector<FeedbackVertex> vertices;};
struct BlockFeedback {std::uint64_t epoch,revision;std::uint32_t life;std::vector<Vec3> lines;std::vector<CrackMesh> cracks;};
inline BlockFeedback read_block_feedback(Reader& r){
    BlockFeedback frame;frame.epoch=r.u64();frame.revision=r.u64();frame.life=r.u32();
    const auto lines=r.u32();
    if(!frame.epoch||!frame.revision||!frame.life||lines>2048||lines%2||std::size_t(lines)*12>r.remaining())throw ProtocolError("Invalid block outline");
    frame.lines.reserve(lines);for(unsigned i=0;i<lines;++i)frame.lines.push_back({r.f32(),r.f32(),r.f32()});
    const auto batches=r.u32();if(batches>32)throw ProtocolError("Too many block crack meshes");
    std::size_t total=0;
    for(unsigned i=0;i<batches;++i){
        const auto stage=r.u32(),count=r.u32();total+=count;
        if(stage>9||count%4||total>65536||std::size_t(count)*24>r.remaining())throw ProtocolError("Invalid block crack mesh");
        CrackMesh mesh{stage,{}};mesh.vertices.reserve(count);
        for(unsigned j=0;j<count;++j)mesh.vertices.push_back({{r.f32(),r.f32(),r.f32()},r.f32(),r.f32(),r.u32()});
        frame.cracks.push_back(std::move(mesh));
    }
    r.finish();return frame;
}
}
