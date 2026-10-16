"""Run production relocation and real Sponge-injected vanilla request/ACK methods.

Minecraft world/player/network APIs are observing adapters. Vanilla bytecode comes
from the pinned local development JAR and stays in build. No live game acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--map', type=Path, required=True)
p.add_argument('--output', type=Path, default=ROOT/'build/relocation-tests')
a = p.parse_args()
out = a.output.resolve()
if not out.is_relative_to(ROOT/'build'):
    raise ValueError('Output must stay in workspace build')
for part in (out, *out.parents):
    if part.is_symlink() or part.is_junction():
        raise ValueError('Redirected output')
    if part == ROOT:
        break
src, classes = out/'src', out/'classes'
classes.mkdir(parents=True, exist_ok=True)
adapters = {
    'net/minecraft/util/math/Vec3d.java': '''public record Vec3d(double unused) {}''',
    'net/minecraft/util/math/Box.java': '''public class Box {
public double minX,minY,minZ,maxX,maxY,maxZ;
public Box(double a,double b,double c,double d,double e,double f){minX=a;minY=b;minZ=c;maxX=d;maxY=e;maxZ=f;}
public Box offset(Vec3d v){return new Box(minX+v.x,minY+v.y,minZ+v.z,maxX+v.x,maxY+v.y,maxZ+v.z);}
public Box contract(double e){return new Box(minX+e,minY+e,minZ+e,maxX-e,maxY-e,maxZ-e);}
public Box stretch(Vec3d v){return this;}}''',
    'net/minecraft/util/math/MathHelper.java': '''public class MathHelper {
public static double clamp(double v,double a,double b){return Math.clamp(v,a,b);}
public static float wrapDegrees(float v){return v;}}''',
    'net/minecraft/util/Identifier.java': '''public record Identifier(String text){
public static Identifier of(String s){return new Identifier(s);}public String toString(){return text;}}''',
    'net/minecraft/registry/RegistryKeys.java': '''public class RegistryKeys {public static final Object WORLD=new Object();}''',
    'net/minecraft/registry/RegistryKey.java': '''import net.minecraft.util.Identifier;
public record RegistryKey(Identifier value){public Identifier getValue(){return value;}
public static RegistryKey of(Object ignored,Identifier value){return new RegistryKey(value);}}''',
    'net/minecraft/world/GameMode.java': '''public enum GameMode {SURVIVAL,CREATIVE,ADVENTURE,SPECTATOR;
public int getId(){return ordinal();}public String getName(){return name();}public static GameMode byId(int id){return values()[id];}}''',
    'net/minecraft/world/World.java': '''import net.minecraft.entity.Entity;import net.minecraft.util.math.*;
import net.minecraft.registry.RegistryKey;import net.minecraft.util.Identifier;import java.util.*;
public class World {public boolean empty=true,border=true;public String dimension="goldcraft:host";public Box checkedBox;
public RegistryKey getRegistryKey(){return new RegistryKey(Identifier.of(dimension));}
public boolean isSpaceEmpty(Entity e,Box box){checkedBox=box;return empty;}
public List<Object> getEntityCollisions(Entity e,Box box){return List.of();}
public Border getWorldBorder(){return new Border();}public class Border {public boolean contains(Box b){return border;}}}''',
    'net/minecraft/server/world/ServerWorld.java': '''public class ServerWorld extends net.minecraft.world.World {}''',
    'net/minecraft/entity/EntityPose.java': '''public enum EntityPose {STANDING,CROUCHING,SWIMMING,FALL_FLYING;}''',
    'net/minecraft/entity/Entity.java': '''import net.minecraft.util.math.*;import net.minecraft.world.World;import java.util.*;
public class Entity {public enum RemovalReason {KILLED;}public boolean noClip,spectator,removed,alive=true,onGround=true,sneak;
public World world;public Vec3d pos=Vec3d.ZERO,velocity=Vec3d.ZERO;public EntityPose pose=EntityPose.STANDING;public float fallDistance;
public Entity riding;public Vec3d seat;public boolean mountingAllowed=true;
public boolean startRiding(Entity target,boolean force){if(!mountingAllowed)return false;riding=target;return true;}
public void updatePassengerPosition(Entity passenger){passenger.pos=seat;}
public void setOnGround(Vec3d motion){onGround=false;}
public boolean isSpectator(){return spectator;}public boolean isRemoved(){return removed;}public boolean isAlive(){return alive;}
public World getWorld(){return world;}public Vec3d getPos(){return pos;}public double getX(){return pos.x;}public double getY(){return pos.y;}public double getZ(){return pos.z;}
public Box getBoundingBox(){return new Box(pos.x-.3,pos.y,pos.z-.3,pos.x+.3,pos.y+1.8,pos.z+.3);}
public static Vec3d adjustMovementForCollisions(Entity e,Vec3d v,Box b,World w,List<Object> others){return v;}
public EntityPose getPose(){return pose;}public float getStepHeight(){return .6f;}public boolean isOnGround(){return onGround;}
public boolean isSneaking(){return sneak;}public Vec3d getVelocity(){return velocity;}public void setVelocity(double x,double y,double z){velocity=new Vec3d(x,y,z);}
public double squaredDistanceTo(double x,double y,double z){return pos.squaredDistanceTo(new Vec3d(x,y,z));}
public float getYaw(){return 0;}public float getPitch(){return 0;}public float getWidth(){return .6f;}public float getHeight(){return 1.8f;}}''',
    'net/minecraft/entity/LivingEntity.java': '''public class LivingEntity extends Entity {}''',
    'net/minecraft/entity/player/PlayerEntity.java': '''public class PlayerEntity extends net.minecraft.entity.LivingEntity {}''',
    'net/minecraft/entity/damage/DamageSource.java': '''public class DamageSource {}''',
    'net/minecraft/server/MinecraftServer.java': '''import net.minecraft.server.world.ServerWorld;import net.minecraft.registry.RegistryKey;
import net.minecraft.server.network.ServerPlayerEntity;import java.util.*;
public class MinecraftServer {public boolean onThread=true;public ServerWorld world=new ServerWorld();public Manager manager=new Manager();
public boolean isOnThread(){return onThread;}public ServerWorld getWorld(RegistryKey key){return world.getRegistryKey().equals(key)?world:null;}
public Manager getPlayerManager(){return manager;}public class Manager {public Map<UUID,ServerPlayerEntity> players=new HashMap<>();
public ServerPlayerEntity getPlayer(UUID uuid){return players.get(uuid);}
public void sendStatusEffects(net.minecraft.entity.LivingEntity entity,net.minecraft.server.network.ServerPlayNetworkHandler handler){}
public ServerPlayerEntity respawnPlayer(ServerPlayerEntity p,boolean ignored,net.minecraft.entity.Entity.RemovalReason reason){return p;}}}''',
    'net/minecraft/server/network/ServerPlayerEntity.java': '''import net.minecraft.entity.player.PlayerEntity;import net.minecraft.server.*;
import net.minecraft.server.world.ServerWorld;import net.minecraft.world.GameMode;import java.util.*;
public class ServerPlayerEntity extends PlayerEntity {public ServerPlayNetworkHandler networkHandler;public MinecraftServer server;
public UUID uuid=new UUID(10,20);public boolean vehicle,sleeping,teleportation;public int teleportationDone;public Set<String> tags=new HashSet<>();public Abilities abilities=new Abilities();
public Interaction interactionManager=new Interaction();public MinecraftServer getServer(){return server;}public UUID getUuid(){return uuid;}
public boolean hasVehicle(){return vehicle||riding!=null;}public boolean isSleeping(){return sleeping;}public int getId(){return 1;}public float getHealth(){return 20;}
public boolean startRiding(net.minecraft.entity.Entity target,boolean force){throw new AssertionError("Vanilla transplant required");}
public float getStandingEyeHeight(){return 1.62f;}public Set<String> getCommandTags(){return tags;}public void addCommandTag(String t){tags.add(t);}
public void removeCommandTag(String t){tags.remove(t);}public Abilities getAbilities(){return abilities;}
public void changeGameMode(GameMode m){interactionManager.mode=m;spectator=m==GameMode.SPECTATOR;}
public void sendAbilitiesUpdate(){}public void onDeath(net.minecraft.entity.damage.DamageSource source){alive=false;}
public ServerWorld getServerWorld(){return (ServerWorld)world;}
public void updatePositionAndAngles(double x,double y,double z,float yaw,float pitch){pos=new net.minecraft.util.math.Vec3d(x,y,z);}
public boolean isInTeleportationState(){return teleportation;}public void onTeleportationDone(){teleportation=false;teleportationDone++;}
public void teleport(ServerWorld w,double x,double y,double z,float yaw,float pitch){world=w;networkHandler.requestTeleport(x,y,z,yaw,pitch);}
public class Interaction {public GameMode mode=GameMode.SURVIVAL;public GameMode getGameMode(){return mode;}}
public static class Abilities {public boolean flying,allowFlying;}}''',
    'net/minecraft/server/network/ServerPlayNetworkHandler.java': '''import dev.goldcraft.mixin.ServerPlayNetworkHandlerAccessor;
import dev.goldcraft.world.HostPlayerEffects;import net.minecraft.util.math.Vec3d;
public class ServerPlayNetworkHandler implements ServerPlayNetworkHandlerAccessor,net.minecraft.network.listener.PacketListener {public ServerPlayerEntity player;public boolean open=true,channel=true;
public Vec3d requestedTeleportPos;public int requests,requestedTeleportId,ticks,prevTeleportCheckTicks;public double updatedX,updatedY,updatedZ;
public net.minecraft.network.packet.s2c.play.PlayerPositionLookS2CPacket sent;public ServerPlayNetworkHandler getConnection(){return this;}public boolean isOpen(){return open;}
public boolean hasChannel(Object id){return channel;}public Vec3d goldcraft$requestedTeleportPos(){return requestedTeleportPos;}
public void requestTeleport(double x,double y,double z,float yaw,float pitch){throw new AssertionError("Vanilla transplant required");}
public void requestTeleport(double x,double y,double z,float yaw,float pitch,java.util.Set<net.minecraft.network.packet.s2c.play.PositionFlag> flags){throw new AssertionError("Vanilla transplant required");}
public void onTeleportConfirm(net.minecraft.network.packet.c2s.play.TeleportConfirmC2SPacket packet){throw new AssertionError("Vanilla transplant required");}
public void send(net.minecraft.network.packet.Packet packet){sent=(net.minecraft.network.packet.s2c.play.PlayerPositionLookS2CPacket)packet;requests++;}
public void disconnect(net.minecraft.text.Text text){open=false;}
public void syncWithPlayerPosition(){}}''',
    'net/minecraft/network/packet/Packet.java': '''public interface Packet {}''',
    'net/minecraft/network/listener/PacketListener.java': '''public interface PacketListener {}''',
    'net/minecraft/network/NetworkThreadUtils.java': '''public class NetworkThreadUtils {
public static boolean onThread=true;public static int calls;
public static void forceMainThread(net.minecraft.network.packet.Packet p,net.minecraft.network.listener.PacketListener l,net.minecraft.server.world.ServerWorld w){calls++;if(!onThread)throw new IllegalStateException("Reschedule on server");}}''',
    'net/minecraft/text/Text.java': '''public interface Text {static MutableText translatable(String key){return new MutableText();}}''',
    'net/minecraft/text/MutableText.java': '''public class MutableText implements Text {}''',
    'net/minecraft/network/packet/c2s/play/TeleportConfirmC2SPacket.java': '''public record TeleportConfirmC2SPacket(int id) implements net.minecraft.network.packet.Packet {public int getTeleportId(){return id;}}''',
    'net/minecraft/network/packet/s2c/play/PositionFlag.java': '''public enum PositionFlag {X,Y,Z,Y_ROT,X_ROT;}''',
    'net/minecraft/network/packet/s2c/play/PlayerPositionLookS2CPacket.java': '''public record PlayerPositionLookS2CPacket(double x,double y,double z,float yaw,float pitch,java.util.Set<PositionFlag> flags,int id) implements net.minecraft.network.packet.Packet {}''',
    'org/spongepowered/asm/mixin/injection/callback/CallbackInfo.java': '''public class CallbackInfo {public CallbackInfo(String name,boolean cancel){if(cancel)throw new AssertionError("Unexpected cancellable teleport");}}''',
    'net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.java': '''public record PlayerMoveC2SPacket(double x,double y,double z){
public boolean changesPosition(){return true;}public double getX(double d){return x;}public double getY(double d){return y;}public double getZ(double d){return z;}}''',
    'dev/goldcraft/mixin/ServerPlayNetworkHandlerAccessor.java': '''public interface ServerPlayNetworkHandlerAccessor {
net.minecraft.util.math.Vec3d goldcraft$requestedTeleportPos();}''',
    'dev/goldcraft/bridge/LocalQpcClock.java': '''public class LocalQpcClock {
public record Sample(long counter,long frequency,long milliseconds,int marginMillis){}public static long millis=1000;
public static final LocalQpcClock CLOCK=new LocalQpcClock();public static LocalQpcClock system(){return CLOCK;}
public Sample read(){return new Sample(millis*1000,1000000,millis,2);}}''',
    'dev/goldcraft/bridge/HostWorldState.java': '''import dev.goldcraft.world.BspMap;import java.util.*;
public class HostWorldState {public long epoch=7;public String dimension="goldcraft:host";public BspMap map;public Actor actor;
public List<Brush> brushes=new ArrayList<>();public Collision collision=new Collision();
public long epoch(){return epoch;}public String dimension(){return dimension;}public String map(){return "cs_assault";}
public BspMap geometry(){return map;}public List<Brush> brushes(){return brushes;}public Collision collision(){return collision;}
public Actor actor(UUID id){return actor!=null&&actor.minecraftPlayer().equals(id)?actor:null;}
public record Actor(int slot,int serial,int spawn,int life,int flags,int team,UUID minecraftPlayer,boolean minecraftForm,BspMap.Vec minecraftFeet){
public float yaw(){return 0;}public float pitch(){return 0;}public int vitalsAck(){return 0;}}
public record Brush(boolean solid,boolean ladder,int model,BspMap.Vec origin,BspMap.Vec angles,BspMap.Vec min,BspMap.Vec max){}
public class Collision {public boolean solidBrush;public int lastHull;public BspMap.Vec point;
public int contents(Brush b,int hull,BspMap.Vec p){lastHull=hull;point=p;return b!=null&&solidBrush?BspMap.SOLID:map.contents(b==null?0:b.model(),hull,p);}
public BspMap.Trace trace(Brush b,int hull,BspMap.Vec a,BspMap.Vec z){return map.trace(b==null?0:b.model(),hull,a,z);}}}''',
    'dev/goldcraft/world/HostCollision.java': '''import net.minecraft.world.World;import dev.goldcraft.bridge.*;import java.util.*;
public class HostCollision {public static Map<World,HostWorldState> states=new IdentityHashMap<>();
public static HostWorldState state(World w){return states.get(w);}public static void attach(World w,HostWorldState h){states.put(w,h);}
public static BspMap.Vec goldsrc(double x,double y,double z){return new BspMap.Vec((float)(x*32),(float)(-z*32),(float)((y-64)*32));}
public static BspMap.Vec local(BspMap.Vec p,HostWorldState.Brush b){return p.subtract(b.origin());}
public static BspMap.Vec worldDirection(BspMap.Vec p,HostWorldState.Brush b){return p;}}''',
    'dev/goldcraft/world/SharedVitals.java': '''import net.minecraft.server.network.ServerPlayerEntity;import dev.goldcraft.bridge.HostWorldState;import java.util.UUID;
public class SharedVitals {public static void disconnect(UUID u){}public static void active(ServerPlayerEntity p,boolean b){}
public static boolean prepare(ServerPlayerEntity p,HostWorldState h,HostWorldState.Actor a){return false;}
public static boolean deferRemoval(ServerPlayerEntity p){return false;}public static int pending(UUID u){return 0;}}''',
    'dev/goldcraft/world/HostWorldReset.java': '''public class HostWorldReset {public static void activate(net.minecraft.server.world.ServerWorld w,long epoch){}}''',
    'dev/goldcraft/net/HostPayload.java': '''public class HostPayload {public static final Object ID=new Object();}''',
    'dev/goldcraft/GoldCraft.java': '''import dev.goldcraft.bridge.*;import java.util.*;
public class GoldCraft {public static HostWorldState HOST_WORLD;public static boolean negotiated=true;public static long generation=9,frequency=1000000;
public static boolean effectsNegotiated(){return negotiated;}public static long currentAuthorityGeneration(){return generation;}
public static long currentAuthorityClockFrequency(){return frequency;}public static List<byte[]> poses=new ArrayList<>();public static int reopens;
public static boolean sendToHost(int kind,byte[] data){if(kind==Wire.EFFECT_POSE)poses.add(data);else if(kind==Wire.EFFECT_REOPEN)reopens++;return true;}
public static Log LOGGER=new Log();public static class Log {public void info(String s,Object... args){}}}''',
}
adapters['net/minecraft/util/math/Vec3d.java'] = '''public class Vec3d {
public static final Vec3d ZERO=new Vec3d(0,0,0);public final double x,y,z;public Vec3d(double x,double y,double z){this.x=x;this.y=y;this.z=z;}
public Vec3d add(Vec3d b){return new Vec3d(x+b.x,y+b.y,z+b.z);}public Vec3d subtract(Vec3d b){return new Vec3d(x-b.x,y-b.y,z-b.z);}
public Vec3d multiply(double f){return new Vec3d(x*f,y*f,z*f);}public double lengthSquared(){return x*x+y*y+z*z;}
public double horizontalLengthSquared(){return x*x+z*z;}public double squaredDistanceTo(Vec3d b){return subtract(b).lengthSquared();}}'''
for name in ('MinecraftObjects', 'MinecraftFluids', 'NativePlayers'):
    adapters[f'dev/goldcraft/world/{name}.java'] = f'public class {name} {{public static void diagnostics(com.google.gson.JsonObject obj){{}}}}'
for name in ('JsonArray', 'JsonObject'):
    adapters[f'com/google/gson/{name}.java'] = f'''public class {name} {{public void addProperty(String k,Object v){{}}
public void add(Object value){{}}public void add(String k,Object v){{}}}}'''
for name, body in adapters.items():
    path = src/name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('package '+name.rsplit('/', 1)[0].replace('/', '.')+';\n'+body+'\n', encoding='utf-8')
names = ['world/HostPlayerEffects.java', 'world/HostMovement.java', 'world/HostSession.java',
         'world/BspMap.java', 'world/HullSlide.java', 'bridge/HostEffects.java',
         'bridge/HostEffectGrant.java', 'bridge/LocalQpcLease.java', 'bridge/Performance.java', 'bridge/Wire.java',
         'bridge/NativeArmorProtocol.java']
production = [ROOT/'neoforge/src/main/java/dev/goldcraft'/name for name in names]
probe = ROOT/'tests/java/dev/goldcraft/world/HostRelocationProbe.java'
java = ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
subprocess.run([str(java/'javac.exe'), '-encoding', 'UTF-8', '-d', str(classes),
                *map(str, src.rglob('*.java')), *map(str, production), str(probe)], check=True)
runtime = json.loads((ROOT/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
games = list((ROOT/'neoforge/.gradle/loom-cache/minecraftMaven/net/minecraft').glob(
    'neoforge-21.1.256-minecraft-merged-*/1.21.1-loom.mappings.*/neoforge-21.1.256-*-neoforge-21.1.256.jar'))
if len(games) != 1:
    raise ValueError(f'Expected one pinned named game JAR: {games}')
game = games[0]
transform_classes = out/'transform-classes'
transform_classes.mkdir(exist_ok=True)
services = transform_classes/'META-INF/services'
services.mkdir(parents=True, exist_ok=True)
(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
(transform_classes/'relocation.mixins.json').write_text(json.dumps({'required': True,
    'package': 'dev.goldcraft.mixin', 'compatibilityLevel': 'JAVA_21',
    'mixins': ['HostEffectMovementMixin','ServerPlayNetworkHandlerAccessor'], 'injectors': {'defaultRequire': 1}}))
fixtures = [ROOT/'tests/java/RelocationTransform.java', *[ROOT/'tests/nightfall-blade'/name
            for name in ('BladeMixinService.java','BladeProperties.java')]]
mixins = [ROOT/'neoforge/src/main/java/dev/goldcraft/mixin'/name
          for name in ('HostEffectMovementMixin.java','ServerPlayNetworkHandlerAccessor.java')]
cp = os.pathsep.join(map(str,[transform_classes,game,ROOT/'neoforge/build/classes/java/main',*runtime['classpath']]))
def transform_run(name, exe, arguments):
    argfile = out/(name+'.args')
    argfile.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in arguments),encoding='utf-8')
    run = subprocess.run([str(java/exe),'@'+str(argfile)],capture_output=True,cwd=out,timeout=90)
    (out/(name+'.log')).write_bytes(run.stdout+run.stderr)
    if run.returncode:
        raise RuntimeError(f'{name} failed: {out/(name+".log")}')
    print('PASS '+name)
transform_run('compile-transform','javac.exe',['--release','21','-proc:none','-encoding','UTF-8',
              '-cp',cp,'-d',transform_classes,*fixtures,*mixins])
transform_run('sponge-transform','java.exe',['-cp',cp,'probe.RelocationTransform',game,classes,out])
result = subprocess.run([str(java/'java.exe'), '-ea', '-cp', str(classes),
                         'dev.goldcraft.world.HostRelocationProbe', str(a.map.resolve())],
                        capture_output=True, text=True)
(out/'execution.log').write_text(result.stdout+result.stderr, encoding='utf-8')
result.check_returncode()
print(result.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True, output=result.stdout.strip(),
    sourceHashes={x.relative_to(ROOT).as_posix(): hashlib.sha256(x.read_bytes()).hexdigest() for x in [*production, probe, *fixtures, *mixins, Path(__file__)]},
    gameSha256=hashlib.sha256(game.read_bytes()).hexdigest(),
    mapSha256=hashlib.sha256(a.map.read_bytes()).hexdigest(),
    boundary='Real Sponge transformation and intact vanilla request/ACK/ServerPlayer.startRiding bytecode, production session/effects/movement and real BSP. Observing MC/world/QPC/transport APIs, base mounting admission and passenger seat. No FML launch, actual network rescheduling, vanilla pending resend execution, boat interaction/physics, native HandlePose or live gameplay.'), indent=2)+'\n', encoding='utf-8')
