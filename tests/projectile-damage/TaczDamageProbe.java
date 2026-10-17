package dev.goldcraft.projectileprobe;

import com.google.gson.JsonObject;
import com.mojang.authlib.GameProfile;
import com.tacz.guns.entity.EntityKineticBullet;
import com.tacz.guns.init.ModDamageTypes;
import com.tacz.guns.util.TacHitResult;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.BspMap;
import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.HostRaycast;
import dev.goldcraft.world.NativePlayerEntity;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import dev.goldcraft.world.NativeProjectileDamage;
import net.minecraft.core.registries.Registries;
import net.minecraft.network.Connection;
import net.minecraft.network.PacketSendListener;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.PacketFlow;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ClientInformation;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.CommonListenerCookie;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.tags.DamageTypeTags;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.Vec3;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.UUID;

/** Actual NeoForge registries, TACZ transformed hit callback and native hurt body. */
@Mod("goldcraft_projectile_probe")
public final class TaczDamageProbe {
    static int checks,hits;static float total;static ServerLevel world;
    static Method eligible,contact,onHit;static Vec3 expected;static NativePlayerEntity body;
    static void check(boolean ok,String label){checks++;if(!ok)throw new AssertionError(label);}
    public TaczDamageProbe(){NeoForge.EVENT_BUS.addListener(TaczDamageProbe::run);}
    static class QuietConnection extends Connection {
        QuietConnection(){super(PacketFlow.SERVERBOUND);}
        @Override public boolean isConnected(){return true;}
        @Override public void send(Packet<?> packet){}
        @Override public void send(Packet<?> packet,PacketSendListener listener){}
        @Override public void send(Packet<?> packet,PacketSendListener listener,boolean flush){}
    }
    static Object invoke(Method method,Object receiver,Object... args){
        try{return method.invoke(receiver,args);}catch(Exception e){throw new RuntimeException(e);}
    }
    static boolean active(DamageSource source,int kind){return (boolean)invoke(eligible,null,source,kind);}
    static Vec3 point(NativePlayerEntity target,DamageSource source){return (Vec3)invoke(contact,null,target,source);}
    static void field(Object target,String name,Object value)throws Exception{
        var f=EntityKineticBullet.class.getDeclaredField(name);f.setAccessible(true);f.set(target,value);
    }
    static EntityKineticBullet bullet(ServerPlayer player)throws Exception{
        var bullet=new EntityKineticBullet(EntityKineticBullet.TYPE,world);bullet.setOwner(player);
        bullet.setPos(-12,71,0);field(bullet,"startPos",bullet.position());field(bullet,"armorIgnore",.25f);
        var amounts=new java.util.LinkedList<com.tacz.guns.resource.pojo.data.gun.ExtraDamage.DistanceDamagePair>();
        amounts.add(new com.tacz.guns.resource.pojo.data.gun.ExtraDamage.DistanceDamagePair(Float.POSITIVE_INFINITY,9));
        field(bullet,"damageAmount",amounts);field(bullet,"distanceAmount",Float.POSITIVE_INFINITY);return bullet;
    }
    static void exercise(MinecraftServer server)throws Exception{
        world=server.overworld();world.getChunk(0,0);
        eligible=Class.forName("dev.goldcraft.world.ActivePlayerDamage").getDeclaredMethod("eligible",DamageSource.class,int.class);eligible.setAccessible(true);
        contact=NativeProjectileDamage.class.getDeclaredMethod("contact",NativePlayerEntity.class,DamageSource.class);contact.setAccessible(true);
        onHit=EntityKineticBullet.class.getDeclaredMethod("onHitEntity",TacHitResult.class,Vec3.class,Vec3.class);onHit.setAccessible(true);
        var player=new ServerPlayer(server,world,new GameProfile(new UUID(44,55),"ProjectileProbe"),ClientInformation.createDefault());
        player.connection=new ServerGamePacketListenerImpl(server,new QuietConnection(),player,CommonListenerCookie.createInitial(player.getGameProfile(),false)){
            @Override public void send(Packet<?> packet){}
            @Override public void send(Packet<?> packet,PacketSendListener listener){}
        };
        var actor=new HostWorldState.Actor(2,3,42,1,1,11,100,0,new HostWorldState.Vector(0,0,228),new HostWorldState.Vector(0,0,0),0,0,
            new HostWorldState.Vector(-16,-16,-36),new HostWorldState.Vector(16,16,36),new UUID(0,0),7,0);
        body=new NativePlayerEntity(world,7,actor,(target,source,amount)->{
            hits++;total+=amount;check(active(source,2),"TACZ server player bullet enters active channel");
            check(NativeProjectileDamage.isProjectile(source),"bullet retains projectile kind");
            check(point(target,source)!=null&&point(target,source).distanceTo(expected)<.000001,"hurt has exact hit point, not tick-start position");
            return true;
        });
        var box=new NativePlayerHitboxEntity(NativePlayerHitboxEntity.TYPE.get(),world);box.follow(body);
        var bullet=bullet(player);expected=box.getBoundingBox().getCenter();
        check(bullet.position().distanceTo(expected)>4,"fast bullet starts outside native 128-unit contact radius");
        var hit=new TacHitResult(new EntityKineticBullet.EntityResult(box,expected,false));
        invoke(onHit,bullet,hit,bullet.position(),expected.add(2,0,0));
        check(hits==2,"actual TACZ callback retains normal and armor-piercing hurt halves");
        check(Math.abs(total-9)<.0001,"split bullet damage totals original base, no double multiplier");
        var normal=ModDamageTypes.Sources.bullet(world.registryAccess(),bullet,player,false);
        check(point(body,normal)==null,"actual transformed callback releases contact scope");
        for(String id:new String[]{"bullet","bullet_ignore_armor","bullet_void","bullet_void_ignore_armor"}){
            var key=ResourceKey.create(Registries.DAMAGE_TYPE,ResourceLocation.fromNamespaceAndPath("tacz",id));
            var holder=world.registryAccess().registryOrThrow(Registries.DAMAGE_TYPE).getHolderOrThrow(key);
            var source=new DamageSource(holder,bullet,player);
            check(!source.is(DamageTypeTags.IS_PROJECTILE),"TACZ itself does not supply vanilla projectile tag: "+id);
            check(NativeProjectileDamage.isProjectile(source)&&active(source,2),"four confirmed gun sources classified: "+id);
            check(!active(new DamageSource(holder,bullet,box),2),"nonplayer owner not granted multiplier: "+id);
            check(!active(source,3),"explosive route never granted multiplier: "+id);
        }
        for(var source:new DamageSource[]{world.damageSources().indirectMagic(bullet,player),world.damageSources().thorns(player),world.damageSources().onFire(),world.damageSources().wither()})
            check(!active(source,1),"passive/potion/environment damage remains unscaled");
        var other=bullet(player);
        NativeProjectileDamage.hit(bullet,hit,bullet.position(),()->{
            check(point(body,normal).equals(expected),"outer scope active");
            var nestedPoint=expected.add(.1,0,0);
            NativeProjectileDamage.hit(other,new EntityHitResult(box,nestedPoint),other.position(),()->{
                check(point(body,normal)==null,"different projectile cannot inherit scope");
                check(point(body,ModDamageTypes.Sources.bullet(world.registryAccess(),other,player,false)).equals(nestedPoint),"nested contact own identity");
            });
            check(point(body,normal).equals(expected),"nested scope restores outer hit");
        });
        try{NativeProjectileDamage.hit(bullet,hit,bullet.position(),()->{throw new IllegalStateException("probe");});}
        catch(IllegalStateException expectedFailure){}
        check(point(body,normal)==null,"exception clears contact scope");
        var host=GoldCraft.HOST_WORLD;byte[] bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
        host.world(new Wire.Writer().i64(7).string("cs_assault").toByteArray());
        host.bsp(new Wire.Writer().i64(7).i32((int)BspMap.checksum(bsp)).i32(bsp.length).bytes(bsp).toByteArray());
        var dimension=HostWorldState.class.getDeclaredField("dimension");dimension.setAccessible(true);dimension.set(host,"minecraft:overworld");
        HostCollision.attach(world,host);
        Vec3 blockedStart=null,blockedEnd=null;
        search:for(int y=66;y<=88;y+=2)for(int z=-30;z<=30;z+=3)for(int x=-30;x<=30;x+=3){
            var start=new Vec3(x,y,z);var end=start.add(20,0,0);
            if(HostRaycast.trace(host,start,end)!=null){blockedStart=start;blockedEnd=end;break search;}
        }
        check(blockedStart!=null,"real cs_assault blocking segment found");
        int before=hits;expected=blockedEnd;box.setPos(expected.x,expected.y-1,expected.z);
        var blockedHit=new TacHitResult(new EntityKineticBullet.EntityResult(box,expected,false));
        invoke(onHit,bullet,blockedHit,blockedStart,blockedEnd);
        check(hits==before,"TACZ cannot damage native proxy through host BSP");
        check(point(body,normal)==null,"blocked hit cannot leak context");
        host.clear();box.discard();body.discard();player.discard();
    }
    static void run(ServerStartedEvent event){
        var report=new JsonObject();
        try{exercise(event.getServer());report.addProperty("passed",true);}
        catch(Throwable error){report.addProperty("passed",false);report.addProperty("error",error.toString());error.printStackTrace();}
        finally{report.addProperty("checks",checks);report.addProperty("actualHurtCalls",hits);report.addProperty("baseDamageTotal",total);
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("projectile-result.json"),report.toString());}catch(Exception e){throw new RuntimeException(e);}}
    }
}
