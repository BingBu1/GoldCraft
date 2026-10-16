"""Exercise the production environment publisher against observing MC API adapters."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,default=ROOT/'analysis/status-environment-publisher')
a=p.parse_args();out=a.output.resolve()
if not out.is_relative_to(ROOT/'analysis'):raise ValueError('Output outside workspace analysis')
for part in (out,*out.parents):
    if part.is_symlink() or part.is_junction():raise ValueError('Redirected output')
    if part==ROOT:break
src=out/'src';classes=out/'classes';classes.mkdir(parents=True,exist_ok=True)
adapters={
    'net/minecraft/entity/effect/StatusEffects.java':'''public class StatusEffects {
public static final int FIRE_RESISTANCE=1,WATER_BREATHING=2,CONDUIT_POWER=4,SLOW_FALLING=8,LEVITATION=16;}''',
    'net/minecraft/entity/LivingEntity.java':'''public class LivingEntity {public int effects;public boolean alive=true;
public boolean isAlive(){return alive;}public boolean hasStatusEffect(int effect){return (effects&effect)!=0;}}''',
    'dev/goldcraft/bridge/HostWorldState.java':'''public class HostWorldState {public long world=99;public long epoch(){return world;}
public record Actor(int slot,int serial,int spawn,int life) {}}''',
    'dev/goldcraft/GoldCraft.java':'''import java.util.*;import dev.goldcraft.bridge.HostWorldState;
public class GoldCraft {public static final HostWorldState HOST_WORLD=new HostWorldState();public static boolean available=true;
public static final List<byte[]> packets=new ArrayList<>();public static boolean environmentAvailable(){return available;}
public static boolean sendEnvironment(byte[] payload){if(!available)return false;packets.add(payload);return true;}}''',
}
for name,body in adapters.items():
    path=src/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('package '+name.rsplit('/',1)[0].replace('/','.')+';\n'+body+'\n',encoding='utf-8')
test=src/'EnvironmentPublisherTest.java'
test.write_text('''import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativeEnvironment;
import dev.goldcraft.bridge.*;
import net.minecraft.entity.LivingEntity;
public class EnvironmentPublisherTest {
static int checks;static void check(boolean ok){checks++;if(!ok)throw new AssertionError(checks);}
public static void main(String[] args){
var owner=new LivingEntity();var actor=new HostWorldState.Actor(2,7,5,9);long previous=0;
for(int mask=0;mask<32;mask++){
    owner.effects=mask;NativeEnvironment.update(actor,owner);var bytes=GoldCraft.packets.getLast();check(bytes.length==36);
    var r=new Wire.Reader(bytes);check(r.i64()==99);long revision=r.i64();check(revision>previous);previous=revision;
    check(r.i32()==2&&r.i32()==7&&r.i32()==5&&r.i32()==9);
    int expected=((mask&1)!=0?1:0)|((mask&6)!=0?2:0)|((mask&24)!=0?4:0);check(r.i32()==expected);r.finish();
}
int count=GoldCraft.packets.size();GoldCraft.available=false;NativeEnvironment.update(actor,owner);check(GoldCraft.packets.size()==count);
GoldCraft.available=true;owner.alive=false;NativeEnvironment.update(actor,owner);check(GoldCraft.packets.size()==count);
owner.alive=true;
for(var invalid:new HostWorldState.Actor[]{new HostWorldState.Actor(0,7,5,9),new HostWorldState.Actor(65,7,5,9),new HostWorldState.Actor(2,0,5,9),new HostWorldState.Actor(2,7,0,9),new HostWorldState.Actor(2,7,5,0)}){
    NativeEnvironment.update(invalid,owner);check(GoldCraft.packets.size()==count);
}
GoldCraft.HOST_WORLD.world=0;NativeEnvironment.update(actor,owner);check(GoldCraft.packets.size()==count);
owner.alive=true;owner.effects=0;GoldCraft.HOST_WORLD.world=100;NativeEnvironment.clear();
NativeEnvironment.update(new HostWorldState.Actor(3,8,6,10),owner);var r=new Wire.Reader(GoldCraft.packets.getLast());
check(r.i64()==100&&r.i64()==1&&r.i32()==3&&r.i32()==8&&r.i32()==6&&r.i32()==10&&r.i32()==0);r.finish();
System.out.println(checks+" environment publisher checks: all effect combinations, identity, absence/death and world reset");
}}
''',encoding='utf-8')
production=[ROOT/name for name in ['neoforge/src/main/java/dev/goldcraft/world/NativeEnvironment.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java','neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java']]
java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
subprocess.run([str(java/'javac.exe'),'-encoding','UTF-8','-d',str(classes),*[str(x) for x in src.rglob('*.java')],*[str(x) for x in production]],check=True)
result=subprocess.run([str(java/'java.exe'),'-ea','-cp',str(classes),'EnvironmentPublisherTest'],check=True,capture_output=True,text=True)
print(result.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True,output=result.stdout.strip(),sourceHashes={x.relative_to(ROOT).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in production},
    boundary='Production Java publisher and wire; observing API adapters. No live game.'),indent=2)+'\n',encoding='utf-8')
