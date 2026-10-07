#include "goldcraft/wire.hpp"
#include "goldcraft/pairing.hpp"
#include "goldcraft/view_interpolator.hpp"
#include "goldcraft/atlas.hpp"
#include "goldcraft/avatar.hpp"
#include "goldcraft/hud.hpp"
#include "goldcraft/particles.hpp"
#include "goldcraft/world_objects.hpp"
#include "goldcraft/combat.hpp"
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

using namespace goldcraft;
static int checks = 0;
static void check(bool ok, const char* label) { ++checks; if (!ok) throw std::runtime_error(label); }
static void reject(std::function<void()> operation, const char* label) {
    bool rejected = false; try { operation(); } catch (const ProtocolError&) { rejected = true; } check(rejected, label);
}
int main() {
    try {
        Writer w; w.u8(0xfe); w.u16(0x1234); w.u32(0x89abcdef); w.i32(-100); w.u64(0x1122334455667788); w.f32(-12.5f); w.string("goldcraft");
        Reader r(w.data); check(r.u8()==0xfe, "u8"); check(r.u16()==0x1234, "u16"); check(r.u32()==0x89abcdef, "u32"); check(r.i32()==-100,"i32");
        check(r.u64()==0x1122334455667788, "u64"); check(r.f32()==-12.5f,"f32"); check(r.string()=="goldcraft","string"); r.finish();
        reject([&]{r.u8();},"read overflow"); reject([]{Writer a; a.f32(std::numeric_limits<float>::infinity());},"infinite outbound");
        Writer nan; nan.u32(0x7fc00000); reject([&]{Reader a(nan.data); a.f32();},"NaN inbound");
        auto session=parse_key("00112233445566778899aabbccddeeff"); auto token=parse_key("ffeeddccbbaa99887766554433221100");
        reject([]{parse_key("short");},"short key"); reject([]{parse_key("gg112233445566778899aabbccddeeff");},"invalid hex");
        Writer hello; hello.bytes(session); hello.bytes(token); hello.u32(2); hello.u32(0);
        auto bytes=encode_frame(Type::hello,0,1,hello.data);
        check(bytes.size()==72,"hello frame size"); check(bytes[0]=='G'&&bytes[1]=='C'&&bytes[2]=='F'&&bytes[3]=='1',"magic bytes");
        auto h=decode_header(std::span(bytes).first(header_bytes));
        HostHandshake hand(session,token,Role::host_client,Role::fabric_client,0x123456789abcdef0);
        hand.accept(h,hello.data); check(hand.ready(),"successful handshake");
        reject([&]{hand.accept(h,hello.data);},"duplicate hello");
        Header message{Type::input,0,hand.epoch(),2}; hand.accept(message,{});
        reject([&]{hand.accept(message,{});},"duplicate input");
        message.sequence=4; reject([&]{hand.accept(message,{});},"missing sequence");
        message.sequence=3; message.epoch=1; reject([&]{hand.accept(message,{});},"stale epoch");
        HostHandshake wrong(session,session,Role::host_client,Role::fabric_client,7); reject([&]{wrong.accept(h,hello.data);},"wrong token");
        HostHandshake wrongRole(session,token,Role::host_server,Role::fabric_server,7); reject([&]{wrongRole.accept(h,hello.data);},"wrong role");
        auto corrupt=bytes; corrupt[8]=0xff; corrupt[9]=0xff; corrupt[10]=0xff; corrupt[11]=0x7f;
        reject([&]{decode_header(std::span(corrupt).first(header_bytes));},"oversized frame");
        corrupt=bytes; corrupt[12]=1; reject([&]{decode_header(std::span(corrupt).first(header_bytes));},"unknown flags");
        for(std::size_t n=0;n<header_bytes;++n) reject([&]{decode_header(std::span(bytes).first(n));},"truncated header");
        PairRegistry registry; registry.new_world(10);
        auto a=registry.connect(1,100,1); auto b=registry.connect(2,200,2);
        check(registry.bind(10,1,100,a.token,session)==PairStatus::ok,"bind A");
        check(registry.bind(10,1,100,a.token,session)==PairStatus::ok,"idempotent bind A");
        check(registry.bind(10,2,200,b.token,session)==PairStatus::uuid_in_use,"one UUID cannot bind two clients");
        check(registry.bind(10,2,200,a.token,token)==PairStatus::wrong_token,"same-machine cross-pair token");
        check(registry.bind(10,2,200,b.token,token)==PairStatus::ok,"bind B");
        registry.disconnect(1); registry.connect(1,101,3);
        check(registry.bind(10,1,100,a.token,session)==PairStatus::stale_player,"reused edict serial");
        registry.new_world(11);
        check(registry.bind(10,2,200,b.token,token)==PairStatus::wrong_world,"map-change stale binding");
        check(!registry.get(2),"map clears bindings");
        for(Vec3 p : {Vec3{0,0,0},Vec3{-4096,8192,512},Vec3{32,-64,96}}) {
            auto converted=to_goldsrc(to_minecraft(p)); check(converted.x==p.x&&converted.y==p.y&&converted.z==p.z,"coordinate roundtrip");
        }
        check(sizeof(float)==4,"32-bit float");
        const std::array<std::uint8_t,9> crc_input{'1','2','3','4','5','6','7','8','9'};
        check(crc32(crc_input)==0xcbf43926u,"standard CRC32 check vector for BSP transfer");
        ViewInterpolator view;
        for(int i=0;i<8;i++)view.push({{i*0.2f,59,74},1.62f},i*0.05,100+i*0.05+(i%2?0.004:0.002));
        float previous=view.at(100.152).feet.x;
        for(int frame=1;frame<=20;frame++) {
            float current=view.at(100.152+frame*0.005).feet.x;
            check(std::abs((current-previous)-0.02f)<0.00001f,"20 Hz poses render with uniform 200 Hz camera steps despite transport jitter");previous=current;
        }
        check(!view.push({{100,100,100},1},0.1,100.5),"stale view sample rejected");
        check(std::abs(view.at(102).feet.x-1.4f)<0.00001f,"packet loss holds latest pose, no wall-crossing extrapolation");
        view.push({{100,70,80},1.2f},0.4,100.405);
        check(view.at(100.405).feet.x==100,"teleport snaps without sweeping camera through map");
        view.push({{101,70,80},1.2f},0.8,100.805);
        check(view.at(100.805).feet.x==101,"resume after stalled producer starts a fresh view timeline");
        view.clear();view.push({{0,0,0},1.62f},1,101);view.push({{0,0,0},1.27f},1.05,101.05);
        check(std::abs(view.at(101.075).eye-1.445f)<0.00001f,"crouch eye height interpolates with movement");
        Writer patch;patch.u32(1);patch.u32(15);patch.u32(3);patch.u32(1);patch.u32(1);patch.u32(0xaabbccdd);
        Reader patchReader(patch.data);auto regions=read_atlas_patches(patchReader,16,16);
        check(regions.size()==1&&regions[0].x==15&&regions[0].rgba[0]==0xdd&&regions[0].rgba[3]==0xaa,"atlas RGBA patch preserves coordinates and alpha");
        auto badPatch=patch.data;badPatch[4]=16;
        reject([&]{Reader in(badPatch);read_atlas_patches(in,16,16);},"atlas right-edge overrun");
        badPatch=patch.data;badPatch[12]=0;
        reject([&]{Reader in(badPatch);read_atlas_patches(in,16,16);},"zero-sized atlas patch");
        badPatch=patch.data;badPatch.resize(badPatch.size()-1);
        reject([&]{Reader in(badPatch);read_atlas_patches(in,16,16);},"truncated atlas pixels");
        badPatch=patch.data;for(int i=12;i<16;i++)badPatch[i]=255;
        reject([&]{Reader in(badPatch);read_atlas_patches(in,16,16);},"atlas x86 dimension overflow");
        Writer many;many.u32(1025);reject([&]{Reader in(many.data);read_atlas_patches(in,16,16);},"atlas patch count budget");
        PlayerIdentity avatar{2,7,21,session};PlayerPresentation hostAvatar{avatar,113};
        check(replaces_player(avatar,hostAvatar),"paired alive controlled avatar replaces exactly one CS body");
        hostAvatar.flags=49;check(!replaces_player(avatar,hostAvatar),"CS form immediately restores native body even with a stale MC mesh");
        hostAvatar.flags=113;
        hostAvatar.identity.serial++;
        check(!replaces_player(avatar,hostAvatar),"old avatar cannot hide reused player slot");
        hostAvatar.identity=avatar;hostAvatar.identity.uuid=token;
        check(!replaces_player(avatar,hostAvatar),"different Minecraft UUID cannot hide player");
        hostAvatar.identity=avatar;hostAvatar.flags=1;
        check(!replaces_player(avatar,hostAvatar),"unpaired native player remains visible");
        hostAvatar.flags=57;check(!replaces_player(avatar,hostAvatar),"bot remains visible");
        hostAvatar.flags=48;check(!replaces_player(avatar,hostAvatar),"dead player is not replaced by stale live avatar");
        Writer avatarWire;avatarWire.u32(1);avatarWire.u32(2);avatarWire.u32(7);avatarWire.u32(21);avatarWire.bytes(session);
        Reader avatarReader(avatarWire.data);check(read_rendered_players(avatarReader).at(0)==avatar,"avatar identity preserves UUID and connection generation");
        auto duplicateAvatar=avatarWire.data;duplicateAvatar[0]=2;duplicateAvatar.insert(duplicateAvatar.end(),avatarWire.data.begin()+4,avatarWire.data.end());
        reject([&]{Reader in(duplicateAvatar);read_rendered_players(in);},"duplicate avatar slots rejected");
        avatarWire.data.pop_back();reject([&]{Reader in(avatarWire.data);read_rendered_players(in);},"truncated avatar identity rejected");
        Writer hud;hud.u16(0x8002);hud.u32(0);hud.u16(1);hud.u32(0x80402010);hud.u32(0xff010203);
        Reader hudReader(hud.data);const auto pixels=read_hud_pixels(hudReader,5,1);
        check(pixels.size()==20&&pixels[12]==0x10&&pixels[15]==0x80&&pixels[19]==0xff,"HUD transparent repeat and literal RGBA preserve alpha");
        reject([&]{Reader in(hud.data);read_hud_pixels(in,0,1);},"empty HUD dimensions rejected");
        reject([&]{Reader in(hud.data);read_hud_pixels(in,0xffffffffu,1024);},"HUD x86 allocation overflow rejected");
        reject([&]{Reader in(hud.data);read_hud_pixels(in,4,1);},"HUD run overflow rejected");
        auto truncatedHud=hud.data;truncatedHud.pop_back();
        reject([&]{Reader in(truncatedHud);read_hud_pixels(in,5,1);},"truncated HUD literal rejected");
        auto trailingHud=hud.data;trailingHud.push_back(0);
        reject([&]{Reader in(trailingHud);read_hud_pixels(in,5,1);},"HUD trailing data rejected");
        Writer repeatedHud;repeatedHud.u16(0x8000);repeatedHud.u8(0);
        reject([&]{Reader in(repeatedHud.data);read_hud_pixels(in,1,1);},"truncated HUD repeat rejected");
        Writer particles;particles.u64(123);particles.u64(7);particles.u64(10);particles.u32(2);particles.u32(1);particles.u32(1);
        particles.u32(0);particles.u32(1);particles.u32(4);
        for(int i=0;i<4;i++){particles.f32(1);particles.f32(66);particles.f32(-3);particles.f32(i%2?1:0);particles.f32(i/2?1:0);particles.u32(0x80102030);}
        Reader particleReader(particles.data);auto particleFrame=read_particles(particleReader);
        check(particleFrame.count==1&&particleFrame.batches.size()==1&&particleFrame.batches[0].texture==0,"terrain particles reuse block atlas");
        auto point=to_goldsrc(particleFrame.batches[0].vertices[0].position);
        check(point.x==32&&point.y==96&&point.z==64&&particleFrame.batches[0].vertices[0].color==0x80102030,"particle axes scale and alpha survive native decode");
        check(current_particles(particleFrame,123,7,9,2),"new particle frame accepted for current life");
        check(!current_particles(particleFrame,124,7,9,2)&&!current_particles(particleFrame,123,7,9,3),"old map or old life particles never render");
        check(!current_particles(particleFrame,123,8,1,2)&&!current_particles(particleFrame,123,7,10,2),"old atlas generation and duplicate frame rejected");
        check(current_particles(particleFrame,123,6,100,2),"new resource generation can restart its revision");
        auto malformed=particles.data;malformed.pop_back();
        reject([&]{Reader in(malformed);read_particles(in);},"truncated particle vertex rejected");
        malformed=particles.data;malformed.push_back(0);
        reject([&]{Reader in(malformed);read_particles(in);},"trailing particle bytes rejected");
        malformed=particles.data;malformed[36]=65;
        reject([&]{Reader in(malformed);read_particles(in);},"particle texture budget enforced");
        malformed=particles.data;malformed[40]=2;
        reject([&]{Reader in(malformed);read_particles(in);},"unknown particle flags rejected");
        malformed=particles.data;malformed[44]=3;
        reject([&]{Reader in(malformed);read_particles(in);},"incomplete particle quad rejected");
        malformed=particles.data;for(int i=44;i<48;i++)malformed[i]=255;
        reject([&]{Reader in(malformed);read_particles(in);},"x86 particle count overflow rejected before allocation");
        malformed=particles.data;malformed[48]=0;malformed[49]=0;malformed[50]=0xc0;malformed[51]=0x7f;
        reject([&]{Reader in(malformed);read_particles(in);},"NaN particle positions rejected");
        Writer emptyParticles;emptyParticles.u64(123);emptyParticles.u64(7);emptyParticles.u64(11);emptyParticles.u32(2);emptyParticles.u32(0);emptyParticles.u32(0);
        Reader emptyParticleReader(emptyParticles.data);check(read_particles(emptyParticleReader).batches.empty(),"empty particle frame explicitly removes expired particles");
        Writer objects;objects.u64(123);objects.u64(2);objects.u32(1);objects.u64(77);objects.u32(object_block|object_usable);objects.f32(75);
        for(float v:{-32.0f,-64.0f,0.0f,0.0f,-32.0f,32.0f})objects.f32(v);
        auto worldObjects=decode_world_objects(objects.data);
        check(worldObjects.objects.size()==1&&worldObjects.objects[0].key==77&&worldObjects.objects[0].min[1]==-64,"native collision object fixed-width decode");
        auto badObjects=objects.data;badObjects.pop_back();reject([&]{decode_world_objects(badObjects);},"truncated collision cannot partially update native world");
        badObjects=objects.data;badObjects[28]=0;reject([&]{decode_world_objects(badObjects);},"collision object kind is required");
        badObjects=objects.data;badObjects[36]=0;badObjects[37]=0;badObjects[38]=0xc0;badObjects[39]=0x7f;
        reject([&]{decode_world_objects(badObjects);},"NaN collision bounds rejected");
        badObjects=objects.data;badObjects[16]=2;badObjects.insert(badObjects.end(),objects.data.begin()+20,objects.data.end());
        reject([&]{decode_world_objects(badObjects);},"duplicate collision object identities rejected");
        Writer noObjects;noObjects.u64(123);noObjects.u64(3);noObjects.u32(0);
        check(decode_world_objects(noObjects.data).objects.empty(),"empty collision snapshot explicitly removes all native proxies");
        Writer mobDamage;mobDamage.u64(123);mobDamage.u64(2);mobDamage.u64(77);
        mobDamage.u32(1);mobDamage.u32(4);mobDamage.u32(8);mobDamage.u32(1);mobDamage.f32(15);
        mobDamage.f32(320);mobDamage.f32(-96);mobDamage.f32(24);
        mobDamage.u32(0);mobDamage.u32(0);mobDamage.u32(0);
        auto bite=read_mob_damage(mobDamage.data);
        check(mobDamage.data.size()==68&&bite.source_key==77&&bite.amount==15&&bite.source.y==-96,"fixed-width authoritative mob damage decode");
        check(current_mob_damage(bite,123,1,4,8),"current mob damage permitted");
        check(!current_mob_damage(bite,123,2,4,8),"replayed bite rejected");
        check(!current_mob_damage(bite,124,1,4,8),"previous map bite rejected");
        check(!current_mob_damage(bite,123,1,5,8),"reconnected slot rejects old attacker event");
        check(!current_mob_damage(bite,123,1,4,9),"real respawn rejects previous body damage");
        auto badDamage=mobDamage.data;badDamage.pop_back();reject([&]{read_mob_damage(badDamage);},"truncated bite rejected");
        badDamage=mobDamage.data;badDamage.push_back(0);reject([&]{read_mob_damage(badDamage);},"trailing bite data rejected");
        badDamage=mobDamage.data;badDamage[24]=0;reject([&]{read_mob_damage(badDamage);},"world cannot be a native damage victim");
        badDamage=mobDamage.data;badDamage[36]=4;reject([&]{read_mob_damage(badDamage);},"unknown mob damage kind rejected");
        badDamage=mobDamage.data;badDamage[40]=0;badDamage[41]=0;badDamage[42]=0xc0;badDamage[43]=0x7f;
        reject([&]{read_mob_damage(badDamage);},"nonfinite mob damage rejected");
        badDamage=mobDamage.data;for(int i=40;i<44;i++)badDamage[i]=0;reject([&]{read_mob_damage(badDamage);},"zero mob damage rejected");
        Writer vitals;vitals.u64(123);vitals.u64(7);vitals.u32(1);vitals.u32(4);vitals.u32(3);vitals.bytes(session);vitals.f32(-25);
        vitals.u32(0);vitals.u32(0);vitals.u32(0);vitals.u32(0);vitals.u64(0);
        auto loss=read_vitals_delta(vitals.data);
        check(vitals.data.size()==72&&loss.delta==-25&&loss.spawn==3,"fixed-width final MC health event");
        check(current_vitals_delta(loss,123,6,4,3,session),"final MC health event matches actual native spawn");
        check(!current_vitals_delta(loss,123,7,4,3,session),"health event replay cannot damage twice");
        check(!current_vitals_delta(loss,123,6,4,4,session),"previous real spawn cannot damage new native life");
        check(!current_vitals_delta(loss,124,6,4,3,session)&&!current_vitals_delta(loss,123,6,5,3,session),"old map and reused connection health rejected");
        check(!current_vitals_delta(loss,123,6,4,3,token),"another player UUID cannot claim health");
        auto badVitals=vitals.data;badVitals.pop_back();reject([&]{read_vitals_delta(badVitals);},"truncated vitals rejected");
        badVitals=vitals.data;for(int i=44;i<48;i++)badVitals[i]=0;reject([&]{read_vitals_delta(badVitals);},"zero vitals change rejected");
        std::cout << "{\"passed\":true,\"checks\":" << checks << ",\"pointerBits\":" << sizeof(void*)*8 << "}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
