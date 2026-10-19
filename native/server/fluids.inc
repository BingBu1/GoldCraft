// Included by bridge.cpp inside its private namespace. Only the authenticated
// Minecraft server can publish these volumes; clients advertise prediction support.
fluids::Field fluid_field;
float fluid_updated=0;
int fluid_message=0;
std::uint64_t fluid_delivery_revision=0;
struct FluidDelivery {
    fluids::Snapshot state;
    Bytes pending;
    std::size_t offset=0;
};
std::array<FluidDelivery,65> fluid_deliveries;
const fluids::Field* ActiveFluids(){
    return link.connected()&&link.fluid_volumes()&&fluid_field.state().epoch==registry.world()
        &&gpGlobals->time>=fluid_updated&&gpGlobals->time-fluid_updated<1.0f?&fluid_field:nullptr;
}
void ResetFluids(){fluid_field.clear();fluid_updated=0;fluid_deliveries={};fluid_delivery_revision=0;}
void AcceptFluids(std::span<const std::uint8_t> bytes){
    if(!link.fluid_volumes())throw ProtocolError("Unnegotiated fluid authority");
    auto snapshot=fluids::decode(bytes);
    if(snapshot.epoch==registry.world()&&fluid_field.assign(std::move(snapshot)))fluid_updated=gpGlobals->time;
}
void SendFluids(){
    const auto* active=ActiveFluids();
    for(int slot=1;slot<=max_clients;++slot){
        auto* player=edicts+slot;auto& delivery=fluid_deliveries[slot];
        if(!fluid_message||!client_ready[slot]||player->free||(player->v.flags&FL_FAKECLIENT)
            ||!HasClientFeature(player,client_features::fluid,fluids::client_capability))continue;
        // Do not starve a bounded in-flight snapshot when flowing water changes
        // again; finish it before preparing the next. Authority loss cancels it.
        if(delivery.pending.empty()||!active){
            std::vector<fluids::Volume> nearby;
            if(active)for(const auto& volume:active->state().volumes){
                bool intersects=true;for(int a=0;a<3;++a)
                    if(volume.max[a]<player->v.origin[a]-512||volume.min[a]>player->v.origin[a]+512)intersects=false;
                if(intersects)nearby.push_back(volume);
            }
            if(delivery.state.epoch!=registry.world()||delivery.state.volumes!=nearby){
                delivery.state={registry.world(),++fluid_delivery_revision,std::move(nearby)};
                delivery.pending=fluids::encode(delivery.state);delivery.offset=0;
            }
        }
        auto send=[&](std::size_t offset,std::size_t total,std::span<const std::uint8_t> bytes){
            Writer w;w.u64(registry.world());w.u64(delivery.state.revision);w.u32(static_cast<std::uint32_t>(offset));w.u32(static_cast<std::uint32_t>(total));w.bytes(bytes);
            MESSAGE_BEGIN(MSG_ONE,fluid_message,nullptr,player);for(auto byte:w.data)WRITE_BYTE(byte);MESSAGE_END();
        };
        for(int n=0;n<8&&!delivery.pending.empty();++n){
            const auto count=std::min<std::size_t>(168,delivery.pending.size()-delivery.offset);
            send(delivery.offset,delivery.pending.size(),std::span(delivery.pending).subspan(delivery.offset,count));
            delivery.offset+=count;
            if(delivery.offset==delivery.pending.size()){delivery.pending.clear();delivery.offset=0;}
        }
        if(delivery.pending.empty()&&delivery.state.revision)send(0,0,{});
    }
}
