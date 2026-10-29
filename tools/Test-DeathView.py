"""Exercise production death viewing and vitals against explicit observing MC APIs."""
from pathlib import Path
import argparse, hashlib, json, subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=ROOT/'build/death-view')
out = p.parse_args().output.resolve()
if not out.is_relative_to(ROOT/'build'):
    raise ValueError('Output must stay in build')
for parent in (out, *out.parents):
    if parent.is_symlink() or parent.is_junction():
        raise ValueError('Redirected output')
    if parent == ROOT:
        break
src, classes = out/'src', out/'classes'
classes.mkdir(parents=True, exist_ok=True)
adapters = {
 'net/minecraft/entity/Entity.java': '''public class Entity {public enum RemovalReason {KILLED}}''',
 'net/minecraft/entity/damage/DamageSource.java': '''public class DamageSource {public Object getAttacker(){return null;}public boolean isIn(Object o){return false;}}''',
 'net/minecraft/registry/tag/DamageTypeTags.java': '''public class DamageTypeTags {public static final Object IS_EXPLOSION=new Object(),IS_PROJECTILE=new Object();}''',
 'net/minecraft/world/GameMode.java': '''public enum GameMode {SURVIVAL,SPECTATOR}''',
 'net/minecraft/server/world/ServerWorld.java': '''public class ServerWorld {public int trackingUpdates;public ServerWorld getChunkManager(){return this;}
public void updatePosition(net.minecraft.server.network.ServerPlayerEntity p){if(!p.isAlive()||p.isRemoved())throw new AssertionError("dead tracker");trackingUpdates++;}}''',
 'dev/goldcraft/mixin/ServerPlayNetworkHandlerAccessor.java': '''public interface ServerPlayNetworkHandlerAccessor {Object goldcraft$requestedTeleportPos();}''',
 'net/minecraft/server/network/ServerPlayerEntity.java': '''import java.util.*;import net.minecraft.entity.*;import net.minecraft.server.world.*;
public class ServerPlayerEntity extends Entity {
public UUID uuid=UUID.randomUUID();public float health=20,fallDistance;public boolean removed,spectator;public double x,y,z;public int inventory=4,drops,deaths;
public ServerWorld world;public Handler networkHandler=new Handler(this);public UUID getUuid(){return uuid;}public ServerWorld getWorld(){return world;}
public float getHealth(){return health;}public float getMaxHealth(){return 20;}public void setHealth(float h){health=h;}public boolean isAlive(){return health>0;}public boolean isRemoved(){return removed;}
public net.minecraft.entity.damage.DamageSource getRecentDamageSource(){return null;}public Sources getDamageSources(){return new Sources();}
public static class Sources{public net.minecraft.entity.damage.DamageSource genericKill(){return new net.minecraft.entity.damage.DamageSource();}}
public double squaredDistanceTo(double a,double b,double c){return (x-a)*(x-a)+(y-b)*(y-b)+(z-c)*(z-c);}
public void teleport(ServerWorld w,double a,double b,double c,float yaw,float pitch){world=w;x=a;y=b;z=c;networkHandler.pending=new Object();networkHandler.teleports++;}
public void setVelocity(double a,double b,double c){}
public static class Handler implements dev.goldcraft.mixin.ServerPlayNetworkHandlerAccessor {
public ServerPlayerEntity player;public Object pending;public int teleports,syncs;public Connection connection=new Connection();
public Handler(ServerPlayerEntity p){player=p;}public Connection getConnection(){return connection;}public Object goldcraft$requestedTeleportPos(){return pending;}public void syncWithPlayerPosition(){syncs++;}}
public static class Connection {public boolean open=true;public boolean isOpen(){return open;}}
}''',
 'net/minecraft/server/MinecraftServer.java': '''import java.util.*;import net.minecraft.server.network.*;import net.minecraft.entity.*;
public class MinecraftServer {public Map<UUID,ServerPlayerEntity> players=new HashMap<>();public int respawns;
public MinecraftServer getPlayerManager(){return this;}public ServerPlayerEntity getPlayer(UUID u){return players.get(u);}
public ServerPlayerEntity respawnPlayer(ServerPlayerEntity old,boolean alive,Entity.RemovalReason reason){
if(alive||reason!=Entity.RemovalReason.KILLED)throw new AssertionError("not vanilla death copy");respawns++;
ServerPlayerEntity p=new ServerPlayerEntity();p.uuid=old.uuid;p.world=old.world;p.networkHandler=old.networkHandler;p.inventory=old.inventory;
p.drops=old.drops;p.deaths=old.deaths;p.spectator=old.spectator;old.removed=true;players.put(p.uuid,p);return p;}}''',
 'dev/goldcraft/bridge/HostWorldState.java': '''import java.util.*;
public class HostWorldState {public long epoch=7;public Actor actor;public long epoch(){return epoch;}public Actor actor(UUID id){return actor!=null&&actor.minecraftPlayer.equals(id)?actor:null;}
public record Point(float x,float y,float z){}
public record Actor(int slot,int serial,int spawn,int life,int flags,int team,float health,UUID minecraftPlayer,Point minecraftFeet){
public long vitalsAck(){return 0;}public float yaw(){return 0;}public float pitch(){return 0;}}}''',
 'dev/goldcraft/world/HostSession.java': '''import net.minecraft.server.network.*;
public class HostSession {public static void hold(ServerPlayerEntity p){p.spectator=true;}
public static void confirmedDeath(ServerPlayerEntity p,net.minecraft.entity.damage.DamageSource source){p.deaths++;p.drops+=p.inventory;p.inventory=0;p.health=0;}}''',
 'dev/goldcraft/world/MinecraftObjects.java': '''public class MinecraftObjects {public static final float CS_HEALTH_PER_MC=5;public static long key(Object o){return 0;}}''',
 'dev/goldcraft/world/NativePlayerEntity.java': '''public class NativePlayerEntity {public dev.goldcraft.bridge.HostWorldState.Actor actor(){return null;}}''',
 # Removal effects have separate real-FML coverage in Test-NativeStatusDeath.py.
 # This observer keeps the death-view fixture focused on its lifecycle contract.
 'dev/goldcraft/world/NativeStatusDeath.java': '''public class NativeStatusDeath {
public static void prepare(net.minecraft.server.network.ServerPlayerEntity p,dev.goldcraft.bridge.HostWorldState h,dev.goldcraft.bridge.HostWorldState.Actor a){}
}''',
 'dev/goldcraft/GoldCraft.java': '''public class GoldCraft {public static dev.goldcraft.bridge.HostWorldState HOST_WORLD;
public static boolean hostConnected(){return true;}public static int messages;public static boolean sendToHost(int type,byte[] data){messages++;return true;}}''',
}
for name, body in adapters.items():
    target = src/name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('package '+name.rsplit('/', 1)[0].replace('/', '.')+';\n'+body+'\n', encoding='utf-8')
production = [ROOT/name for name in (
 'neoforge/src/main/java/dev/goldcraft/world/HostDeathView.java',
 'neoforge/src/main/java/dev/goldcraft/world/SharedVitals.java',
 'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java',
 'neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java',
 'neoforge/src/main/java/dev/goldcraft/bridge/VitalsLedger.java',
 'neoforge/src/main/java/dev/goldcraft/bridge/HealthPrediction.java',
 'tests/death-view/DeathViewProbe.java')]
java = next((ROOT/'.tools/java').glob('jdk-21*/bin'))
args = ['--release', '21', '-encoding', 'UTF-8', '-proc:none', '-d', str(classes), *map(str, production), *map(str, src.rglob('*.java'))]
argfile = out/'javac.args'
argfile.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args), encoding='utf-8')
runs=[]
for name, command in [('compile',[str(java/'javac.exe'),'@'+str(argfile)]),
                      ('test',[str(java/'java.exe'),'-ea','-cp',str(classes),'dev.goldcraft.world.DeathViewProbe'])]:
    result=subprocess.run(command,capture_output=True,timeout=60)
    (out/(name+'.log')).write_bytes(result.stdout+result.stderr)
    print(name,result.returncode,(result.stdout+result.stderr).decode('utf-8',errors='replace'))
    runs.append(dict(name=name,exitCode=result.returncode))
    if result.returncode:break
report=dict(passed=len(runs)==2 and all(r['exitCode']==0 for r in runs),runs=runs,
            sourceHashes={f.relative_to(ROOT).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in production},
            boundary='Production death viewer and health reconciliation; respawn, chunk tracking, death callbacks, removal effects and network APIs are observers. Real removal effects are covered separately by Test-NativeStatusDeath.py. No live gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
raise SystemExit(0 if report['passed'] else 1)
