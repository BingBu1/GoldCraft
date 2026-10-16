package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.*;
import net.minecraft.entity.EntityPose;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.*;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.Vec3d;
import net.minecraft.network.NetworkThreadUtils;
import net.minecraft.network.packet.c2s.play.TeleportConfirmC2SPacket;
import net.minecraft.network.packet.s2c.play.PositionFlag;
import java.nio.file.*;
import java.lang.reflect.Field;
import java.util.*;

/** Production session/effect/movement code; MC API and transport are observers.
 * Actual Sponge-injected vanilla request and unchanged ACK bytecode execute
 * against those observers. Real BSP supplies clip geometry. */
public final class HostRelocationProbe {
    private static int checks;
    private static BspMap map;
    private static Vec3d free, clip;
    private static void check(boolean ok,String message){checks++;if(!ok)throw new AssertionError(checks+": "+message);}
    private static Vec3d mc(BspMap.Vec p){return new Vec3d(p.x()/32.,(p.z()-36)/32.+64,-p.y()/32.);}
    private static Field field(Class<?> type,String name)throws Exception{var f=type.getDeclaredField(name);f.setAccessible(true);return f;}
    private static Object state(ServerPlayerEntity player)throws Exception{return ((Map<?,?>)field(HostSession.class,"PLAYERS").get(null)).get(player.getUuid());}
    private static void set(Object state,String name,Object value)throws Exception{field(state.getClass(),name).set(state,value);}
    private static class Fixture {
        MinecraftServer server=new MinecraftServer();
        ServerPlayerEntity player=new ServerPlayerEntity();
        HostWorldState host=new HostWorldState();
        HostEffectGrant.Grant grant;
        HostPlayerEffects.Binding binding;
        Fixture(int axes)throws Exception{
            HostSession.stopped();HostCollision.states.clear();LocalQpcClock.millis=1000;
            GoldCraft.negotiated=true;GoldCraft.generation=9;GoldCraft.frequency=1000000;GoldCraft.poses.clear();GoldCraft.reopens=0;
            GoldCraft.HOST_WORLD=host;host.map=map;player.server=server;player.world=server.world;player.pos=free;
            player.networkHandler=new ServerPlayNetworkHandler();player.networkHandler.player=player;server.manager.players.put(player.uuid,player);
            host.actor=actor(player.uuid,10,2,1,true);HostSession.paired(player.uuid,host.epoch());
            HostSession.tick(server,host); // Native placement; awaiting the real client's ACK.
            check(player.networkHandler.requestedTeleportPos!=null,"initial placement pending");
            ack();HostSession.ready(player.uuid,7,10,2,true);
            var movement=axes==0?HostEffects.MovementState.unrestricted():new HostEffects.MovementState(HostEffects.POSITION_HOLD,0,axes,0,1);
            var target=new HostEffects.Target(7,HostEffects.PLAYER,1,10,2);
            var point=new HostEffectGrant.Point((float)(free.x*32),(float)(-free.z*32),(float)((free.y-64)*32));
            grant=new HostEffectGrant.Grant(HostEffectGrant.OPEN,5,9,new HostEffects.Snapshot(target,1,movement),player.uuid,1,
                new LocalQpcLease(1000000,1000,1700,1),true,point,point);
            HostSession.effectGrant(server,host,grant);HostSession.tick(server,host);
            check(player.networkHandler.requestedTeleportPos!=null,"grant alignment pending");
            ack();HostSession.tick(server,host);
            binding=(HostPlayerEffects.Binding)field(state(player).getClass(),"effects").get(state(player));
            check(binding!=null&&HostPlayerEffects.readiness(binding,1000000).ready(),"OPEN at native feet ready");
            grant=renew(HostEffectGrant.UPDATE,2);
            HostSession.effectGrant(server,host,grant);HostSession.tick(server,host);
            check(HostSession.relocationGrant(player,binding)==grant,"UPDATE admitted through actual session");
        }
        HostWorldState.Actor actor(UUID uuid,int serial,int life,int flags,boolean form){
            return new HostWorldState.Actor(1,serial,3,life,flags,2,uuid,form,
                new BspMap.Vec((float)free.x,(float)free.y,(float)free.z));
        }
        HostEffectGrant.Grant renew(int operation,long publication){
            return new HostEffectGrant.Grant(operation,5,9,grant.snapshot(),player.uuid,1,
                new LocalQpcLease(1000000,LocalQpcClock.millis,LocalQpcClock.millis+700,publication),false,grant.nativeFeet(),grant.nativeAnchorFeet());
        }
        void request(Vec3d target){player.networkHandler.requestTeleport(target.x,target.y,target.z,0,0);}
        void ack(){ack(player.networkHandler.requestedTeleportId);}
        void ack(int id){player.networkHandler.onTeleportConfirm(new TeleportConfirmC2SPacket(id));}
        HostPlayerEffects.Readiness readiness(){return HostPlayerEffects.readiness(binding,1000000);}
    }
    private static void findPoints(){
        var b=map.bounds(0);
        for(float z=68;z<780&&(free==null||clip==null);z+=32)
            for(float x=b.min().x()+32;x<b.max().x()&&(free==null||clip==null);x+=32)
                for(float y=b.min().y()+32;y<b.max().y()&&(free==null||clip==null);y+=32){
                    var p=new BspMap.Vec(x,y,z);
                    if(map.contents(0,1,p)!=BspMap.SOLID){if(free==null)free=mc(p);continue;}
                    if(map.classify(0,new BspMap.Bounds(new BspMap.Vec(x-15.9f,y-15.9f,z-35.9f),new BspMap.Vec(x+15.9f,y+15.9f,z+35.9f)))==BspMap.CLEAR)clip=mc(p);
                }
        check(free!=null&&clip!=null,"actual map has free feet and render-clear player-clip target");
    }
    private static void admittedAndPending()throws Exception{
        var f=new Fixture(0);var original=f.readiness();
        // Relocating to a different free height is enough to detect old-anchor realignment.
        Vec3d destination=null;
        for(int delta:new int[]{32,64,96,-32}){var candidate=free.add(new Vec3d(0,delta/32.,0));if(HostMovement.canTeleport(f.player,candidate)){destination=candidate;break;}}
        check(destination!=null,"distinct valid destination");
        f.request(destination);check(f.readiness()!=null&&!f.readiness().ready(),"pending does not revoke binding or publish pose");
        int poses=GoldCraft.poses.size(),requests=f.player.networkHandler.requests;
        HostSession.tick(f.server,f.host);check(GoldCraft.poses.size()==poses,"pending suppresses proved pose");
        check(f.player.networkHandler.requests==requests,"pending does not realign to old native feet");
        HostPlayerEffects.vanillaCorrection(f.player,()->f.request(f.player.pos));
        check(f.readiness()!=null&&!f.readiness().ready(),"resend retains binding and pending barrier");
        check(f.readiness().lease().equals(original.lease()),"request/resend did not extend lease");
        f.ack(f.player.networkHandler.requestedTeleportId-1);HostSession.tick(f.server,f.host);
        check(!f.readiness().ready()&&GoldCraft.poses.size()==poses,"wrong ACK keeps pose barrier");
        f.ack();HostSession.tick(f.server,f.host);
        check(f.readiness().ready()&&GoldCraft.poses.size()==poses+1,"vanilla ACK allows actual session publication");
        var r=new Wire.Reader(GoldCraft.poses.getLast());r.bytes(84);
        check(r.f32()==(float)destination.x&&r.f32()==(float)destination.y&&r.f32()==(float)destination.z,"published target rather than old native anchor");
        f.request(free);int oldId=f.player.networkHandler.requestedTeleportId;
        f.request(destination);f.ack(oldId);
        check(f.readiness()!=null&&!f.readiness().ready(),"old ACK cannot confirm successive request");
        LocalQpcClock.millis=original.lease().deadlineMillis();
        check(f.readiness()==null,"pending cannot restart expired deadline");
        HostSession.tick(f.server,f.host);check(GoldCraft.reopens>0,"expired pending requests native reopen");
    }
    private static void rejectedLifecycle()throws Exception{
        for(int condition=0;condition<24;condition++){
            var f=new Fixture(0);var s=state(f.player);
            switch(condition){
                case 0 -> set(s,"grant",f.renew(HostEffectGrant.OPEN,3));
                case 1 -> set(s,"ready",false);
                case 2 -> set(s,"lastReady",System.nanoTime()-2_000_000_000L);
                case 3 -> set(s,"placed",false);
                case 4 -> set(s,"enabled",false);
                case 5 -> set(s,"entity",new ServerPlayerEntity());
                case 6 -> set(s,"effectPlayer",new ServerPlayerEntity());
                case 7 -> set(s,"effectHandler",new ServerPlayNetworkHandler());
                case 8 -> f.player.networkHandler.open=false;
                case 9 -> f.player.networkHandler.channel=false;
                case 10 -> {var other=new ServerPlayerEntity();other.networkHandler=f.player.networkHandler;f.player.networkHandler.player=other;}
                case 11 -> GoldCraft.negotiated=false;
                case 12 -> GoldCraft.generation++;
                case 13 -> GoldCraft.frequency++;
                case 14 -> f.host.epoch++;
                case 15 -> f.host.actor=f.actor(f.player.uuid,11,2,1,true);
                case 16 -> f.host.actor=f.actor(f.player.uuid,10,3,1,true);
                case 17 -> f.host.actor=f.actor(f.player.uuid,10,2,0,true);
                case 18 -> f.host.actor=f.actor(f.player.uuid,10,2,1,false);
                case 19 -> f.player.world=new ServerWorld();
                case 20 -> f.player.alive=false;
                case 21 -> f.player.removed=true;
                case 22 -> LocalQpcClock.millis=1700;
                case 23 -> set(s,"grant",f.renew(HostEffectGrant.UPDATE,3)); // Received, not installed.
            }
            f.request(free);check(f.readiness()==null,"lifecycle rejects condition "+condition);
        }
    }
    private static void geometryAndHolds()throws Exception{
        var f=new Fixture(0);
        check(HostMovement.canTeleport(f.player,free),"free clip target");
        check(!HostMovement.canTeleport(f.player,clip),"invisible player clip must reject teleport");
        for(Vec3d p:List.of(new Vec3d(Double.NaN,64,0),new Vec3d(0,Double.POSITIVE_INFINITY,0),
                new Vec3d(513,64,0),new Vec3d(0,577,0),new Vec3d(0,64,-513),new Vec3d(0,-449,0)))
            check(!HostMovement.canTeleport(f.player,p),"invalid/native bound target");
        f.player.world.empty=false;check(!HostMovement.canTeleport(f.player,free),"MC block/entity collision");f.player.world.empty=true;
        f.player.world.border=false;check(!HostMovement.canTeleport(f.player,free),"world border");f.player.world.border=true;
        f.player.pose=EntityPose.CROUCHING;HostMovement.canTeleport(f.player,free);check(f.host.collision.lastHull==3,"crouch hull3");f.player.pose=EntityPose.STANDING;
        var lo=new BspMap.Vec(-16000,-16000,-16000);var hi=new BspMap.Vec(16000,16000,16000);
        f.host.brushes.add(new HostWorldState.Brush(true,false,0,new BspMap.Vec(0,0,0),new BspMap.Vec(0,0,0),lo,hi));f.host.collision.solidBrush=true;
        check(!HostMovement.canTeleport(f.player,free),"solid dynamic brush query");
        f.request(free);check(f.readiness()==null,"solid target retires external relocation");
        for(int axes=1;axes<8;axes++){
            f=new Fixture(axes);
            Vec3d violation=free.add(new Vec3d((axes&1)!=0?.25:0,(axes&4)!=0?.25:0,(axes&2)!=0?.25:0));
            f.request(violation);check(f.readiness()==null,"held axes reject "+axes);
        }
    }
    private static void renewalAndReplacement()throws Exception{
        var f=new Fixture(0);var old=f.binding;f.request(free);LocalQpcClock.millis+=100;
        f.grant=f.renew(HostEffectGrant.UPDATE,3);HostSession.effectGrant(f.server,f.host,f.grant);HostSession.tick(f.server,f.host);
        check(f.readiness()!=null&&!f.readiness().ready()&&f.readiness().lease().equals(f.grant.lease()),"native renewal during pending");
        f.ack();HostSession.tick(f.server,f.host);check(f.readiness().ready(),"renewal resumes existing binding");
        var previous=f.player;var replacement=new ServerPlayerEntity();replacement.server=f.server;replacement.world=f.server.world;replacement.pos=free;
        replacement.uuid=previous.uuid;replacement.networkHandler=previous.networkHandler;replacement.networkHandler.player=replacement;
        HostSession.respawned(replacement);f.server.manager.players.put(replacement.uuid,replacement);
        check(HostPlayerEffects.readiness(old,1000000)==null,"respawn retires old binding");
        check(HostSession.relocationGrant(replacement,old)==null,"replacement cannot inherit old binding");
        HostPlayerEffects.teleporting(previous,free.x,free.y,free.z);check(HostSession.relocationGrant(replacement,old)==null,"old instance cannot mint replacement relocation");
    }
    private static void vanillaAcknowledgement()throws Exception{
        var f=new Fixture(0);var h=f.player.networkHandler;
        h.requestedTeleportId=Integer.MAX_VALUE-1;h.ticks=123;
        h.requestTeleport(free.x,free.y,free.z,0,0,EnumSet.of(PositionFlag.X,PositionFlag.Y,PositionFlag.Z));
        check(h.requestedTeleportId==0&&h.sent.id()==0,"vanilla teleport ID wraps");
        check(h.sent.x()==0&&h.sent.y()==0&&h.sent.z()==0,"relative packet subtracts old position");
        check(h.requestedTeleportPos.squaredDistanceTo(free)==0&&h.prevTeleportCheckTicks==123,"absolute pending target and request tick");
        check(!f.readiness().ready(),"relative packet still waits for ACK");
        NetworkThreadUtils.onThread=false;
        try{f.ack();throw new AssertionError("off-thread ACK accepted");}catch(IllegalStateException expected){}
        finally{NetworkThreadUtils.onThread=true;}
        check(h.requestedTeleportPos!=null&&!f.readiness().ready(),"main-thread boundary precedes ACK mutation");
        f.player.teleportation=true;f.ack();
        check(h.requestedTeleportPos==null&&f.readiness().ready(),"matching ACK clears barrier");
        check(f.player.teleportationDone==1&&!f.player.teleportation,"vanilla teleport lifecycle completed");
        check(h.updatedX==free.x&&h.updatedY==free.y&&h.updatedZ==free.z,"ACK updates vanilla position baseline");
        f.ack();check(!h.open&&f.readiness()==null,"duplicate matching ACK disconnects, retiring authority");
    }
    private static void vanillaRiding()throws Exception{
        var f=new Fixture(0);var vehicle=new net.minecraft.entity.Entity();vehicle.seat=free;
        int poses=GoldCraft.poses.size(),requests=f.player.networkHandler.requests;
        f.player.mountingAllowed=false;
        check(!f.player.startRiding(vehicle,false),"vanilla preserves base mounting rejection");
        check(f.readiness().ready()&&f.player.networkHandler.requests==requests,"rejected mount creates no teleport");
        f.player.mountingAllowed=true;
        check(f.player.startRiding(vehicle,false)&&f.player.hasVehicle(),"vanilla accepted mount requests seat alignment");
        check(f.player.networkHandler.requests==requests+1&&!f.readiness().ready(),"mount retains binding behind real ACK barrier");
        HostSession.tick(f.server,f.host);
        check(GoldCraft.poses.size()==poses&&f.player.networkHandler.requests==requests+1,"pending mount does not reopen or pull to native anchor");
        f.ack();HostSession.tick(f.server,f.host);
        check(f.readiness().ready()&&GoldCraft.poses.size()==poses+1&&f.player.hasVehicle(),"mounted session publishes after ACK");
        // Observe a passenger seat supplied by vehicle physics; this does not
        // simulate or claim to validate BoatEntity physics itself.
        vehicle.seat=free.add(new Vec3d(0,1,0));vehicle.updatePassengerPosition(f.player);
        HostSession.tick(f.server,f.host);
        var r=new Wire.Reader(GoldCraft.poses.getLast());r.bytes(84);
        check(r.f32()==(float)vehicle.seat.x&&r.f32()==(float)vehicle.seat.y&&r.f32()==(float)vehicle.seat.z,"session publishes passenger position without ordinary movement observation");
        check(f.player.networkHandler.requests==requests+1,"passenger movement does not realign to old native anchor");
    }
    public static void main(String[] args)throws Exception{
        map=BspMap.read(Files.readAllBytes(Path.of(args[0])));findPoints();
        admittedAndPending();rejectedLifecycle();geometryAndHolds();renewalAndReplacement();vanillaAcknowledgement();vanillaRiding();
        System.out.println(checks+" production relocation/session/BSP checks with real Sponge-injected vanilla request/ACK; observing Minecraft/transport/QPC boundaries, no live game execution");
    }
}
