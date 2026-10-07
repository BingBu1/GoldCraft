#include "goldcraft/endpoint.hpp"
#include "goldcraft/hud.hpp"
#include "goldcraft/particles.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace goldcraft;
int main() {
    try {
        Endpoint a,b;
        auto keyA=parse_key("00112233445566778899aabbccddeeff"), keyB=parse_key("ffeeddccbbaa99887766554433221100");
        a.start({0,keyA,keyA,Role::host_client,Role::fabric_client});
        b.start({0,keyB,keyB,Role::host_client,Role::fabric_client});
        std::cout << "READY " << a.port() << ' ' << b.port() << std::endl;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
        while(std::chrono::steady_clock::now()<deadline) {
            for(auto* endpoint : {&a,&b}) {
                endpoint->poll();
                for(auto& message : endpoint->take_messages()) {
                    if(message.type==Type::input) endpoint->send(Type::player_pose,message.payload);
                    else if(message.type==Type::hud_frame){Reader r(message.payload);auto width=r.u32(),height=r.u32();endpoint->send(Type::player_pose,read_hud_pixels(r,width,height));}
                    else if(message.type==Type::particle_mesh){
                        Reader r(message.payload);auto frame=read_particles(r);Writer answer;
                        answer.u64(frame.epoch);answer.u64(frame.generation);answer.u64(frame.revision);answer.u32(frame.life);answer.u32(frame.count);answer.u32(static_cast<unsigned>(frame.batches.size()));
                        for(const auto& batch:frame.batches){
                            answer.u32(batch.texture);answer.u32(batch.flags);answer.u32(static_cast<unsigned>(batch.vertices.size()));
                            for(const auto& v:batch.vertices){auto gs=to_goldsrc(v.position);answer.f32(gs.x);answer.f32(gs.y);answer.f32(gs.z);answer.f32(v.u);answer.f32(v.v);answer.u32(v.color);}
                        }
                        endpoint->send(Type::player_pose,answer.data);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
