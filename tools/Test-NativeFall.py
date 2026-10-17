"""Compile the production fall publisher with observing Minecraft API adapters."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,default=ROOT/'build/status-fall-publisher')
out=p.parse_args().output.resolve()
if not out.is_relative_to(ROOT/'build'):raise ValueError('Output outside workspace build')
for part in (out,*out.parents):
    if part.is_symlink() or part.is_junction():raise ValueError('Redirected output')
    if part==ROOT:break
src=out/'src';classes=out/'classes';classes.mkdir(parents=True,exist_ok=True)
adapters={
    'net/minecraft/entity/attribute/EntityAttributes.java':'''public class EntityAttributes {
public static final int GENERIC_SAFE_FALL_DISTANCE=1,GENERIC_FALL_DAMAGE_MULTIPLIER=2;}''',
    'net/minecraft/entity/LivingEntity.java':'''public class LivingEntity {
public boolean alive=true;public double safe=3,multiplier=1;public boolean isAlive(){return alive;}
public double getAttributeValue(int type){return switch(type){case 1->safe;case 2->multiplier;default->throw new AssertionError(type);};}}''',
    'dev/goldcraft/bridge/HostWorldState.java':'''public class HostWorldState {public long world=99;public long epoch(){return world;}
public record Actor(int slot,int serial,int spawn,int life){}}''',
    'dev/goldcraft/GoldCraft.java':'''import java.util.*;import dev.goldcraft.bridge.HostWorldState;
public class GoldCraft {public static final HostWorldState HOST_WORLD=new HostWorldState();public static boolean available=true;
public static final List<byte[]> packets=new ArrayList<>();public static boolean fallAvailable(){return available;}
public static boolean sendFall(byte[] payload){if(!available)return false;packets.add(payload);return true;}}''',
}
for name,body in adapters.items():
    path=src/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('package '+name.rsplit('/',1)[0].replace('/','.')+';\n'+body+'\n',encoding='utf-8')
(src/'FallPublisherTest.java').write_text('''import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativeFall;
import dev.goldcraft.bridge.*;
import net.minecraft.entity.LivingEntity;
public class FallPublisherTest {
static int checks;static void check(boolean ok){checks++;if(!ok)throw new AssertionError(checks);}
public static void main(String[] args){
var owner=new LivingEntity();var actor=new HostWorldState.Actor(2,7,5,9);long previous=0;
for(double safe:new double[]{-2048,-1024,-1,0,3,4,5,259,1024,2048})for(double multiplier:new double[]{-1,0,.5,1,2,100,101}){
 owner.safe=safe;owner.multiplier=multiplier;NativeFall.update(actor,owner);var bytes=GoldCraft.packets.getLast();check(bytes.length==40);
 var r=new Wire.Reader(bytes);check(r.i64()==99);long revision=r.i64();check(revision>previous);previous=revision;
 check(r.i32()==2&&r.i32()==7&&r.i32()==5&&r.i32()==9);
 check(r.f32()==(float)Math.clamp(safe,-1024,1024));check(r.f32()==(float)Math.clamp(multiplier,0,100));r.finish();
}
int count=GoldCraft.packets.size();
for(double bad:new double[]{Double.NaN,Double.POSITIVE_INFINITY,Double.NEGATIVE_INFINITY}){
 owner.safe=bad;owner.multiplier=1;NativeFall.update(actor,owner);check(GoldCraft.packets.size()==count);
 owner.safe=3;owner.multiplier=bad;NativeFall.update(actor,owner);check(GoldCraft.packets.size()==count);
}
owner.safe=3;owner.multiplier=1;GoldCraft.available=false;NativeFall.update(actor,owner);check(GoldCraft.packets.size()==count);
GoldCraft.available=true;owner.alive=false;NativeFall.update(actor,owner);check(GoldCraft.packets.size()==count);owner.alive=true;
for(var invalid:new HostWorldState.Actor[]{new HostWorldState.Actor(0,7,5,9),new HostWorldState.Actor(65,7,5,9),new HostWorldState.Actor(2,0,5,9),new HostWorldState.Actor(2,7,0,9),new HostWorldState.Actor(2,7,5,0)}){
 NativeFall.update(invalid,owner);check(GoldCraft.packets.size()==count);
}
GoldCraft.HOST_WORLD.world=0;NativeFall.update(actor,owner);check(GoldCraft.packets.size()==count);
GoldCraft.HOST_WORLD.world=100;NativeFall.clear();NativeFall.update(actor,owner);var r=new Wire.Reader(GoldCraft.packets.getLast());
check(r.i64()==100&&r.i64()==1&&r.i32()==2&&r.i32()==7&&r.i32()==5&&r.i32()==9&&r.f32()==3&&r.f32()==1);r.finish();
System.out.println(checks+" fall publisher checks: actual attributes, defaults, bounds, finite values and lifetime");
}}
''',encoding='utf-8')
production=[ROOT/name for name in ['neoforge/src/main/java/dev/goldcraft/world/NativeFall.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java','neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java']]
java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
subprocess.run([str(java/'javac.exe'),'-encoding','UTF-8','-d',str(classes),*[str(x) for x in src.rglob('*.java')],*[str(x) for x in production]],check=True)
result=subprocess.run([str(java/'java.exe'),'-ea','-cp',str(classes),'FallPublisherTest'],check=True,capture_output=True,text=True)
print(result.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True,output=result.stdout.strip(),sourceHashes={x.relative_to(ROOT).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in production},
    boundary='Production publisher/wire with observing MC API adapters; not a live Minecraft test.'),indent=2)+'\n',encoding='utf-8')
