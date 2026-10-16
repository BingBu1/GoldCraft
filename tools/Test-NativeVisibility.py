"""Exercise the production visibility publisher with observing Minecraft API adapters."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,default=ROOT/'build/status-visibility-publisher')
a=p.parse_args();out=a.output.resolve()
if not out.is_relative_to(ROOT/'build'):raise ValueError('Output outside workspace build directory')
for part in (out,*out.parents):
    if part.is_symlink() or part.is_junction():raise ValueError('Redirected output')
    if part==ROOT:break
src=out/'src';classes=out/'classes';classes.mkdir(parents=True,exist_ok=True)
adapters={
    'net/minecraft/entity/effect/StatusEffects.java':'public class StatusEffects {public static final int INVISIBILITY=1;}',
    'net/minecraft/entity/LivingEntity.java':'''public class LivingEntity {public int effects;public boolean alive=true,hidden;
public boolean isAlive(){return alive;}public boolean isInvisible(){return hidden;}
public boolean hasStatusEffect(int effect){return (effects&effect)!=0;}}''',
    'dev/goldcraft/world/NativePlayerHitboxEntity.java':'''public class NativePlayerHitboxEntity extends net.minecraft.entity.LivingEntity {
public boolean tracked;public void potionInvisible(boolean value){tracked=value;}}''',
    'dev/goldcraft/bridge/HostWorldState.java':'''public class HostWorldState {public long world=99;public long epoch(){return world;}
public record Actor(int slot,int serial,int spawn,int life) {}}''',
    'dev/goldcraft/GoldCraft.java':'''import java.util.*;import dev.goldcraft.bridge.HostWorldState;
public class GoldCraft {public static final HostWorldState HOST_WORLD=new HostWorldState();public static boolean available=true;
public static final List<byte[]> packets=new ArrayList<>();public static boolean visibilityAvailable(){return available;}
public static boolean sendVisibility(byte[] payload){if(!available)return false;packets.add(payload);return true;}}''',
}
for name,body in adapters.items():
    path=src/name;path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('package '+name.rsplit('/',1)[0].replace('/','.')+';\n'+body+'\n',encoding='utf-8')
(src/'VisibilityPublisherTest.java').write_text('''import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.*;
import dev.goldcraft.bridge.*;
import net.minecraft.entity.LivingEntity;
public class VisibilityPublisherTest {
static int checks;static void check(boolean ok){checks++;if(!ok)throw new AssertionError(checks);}
public static void main(String[] args){
var actor=new HostWorldState.Actor(2,7,5,9);long previous=0;
for(var owner:new LivingEntity[]{new LivingEntity(),new NativePlayerHitboxEntity()}){
    for(boolean hidden:new boolean[]{false,true})for(int mask:new int[]{0,1,0}){
        owner.hidden=hidden;owner.effects=mask;NativeVisibility.update(actor,owner);
        check(owner.isInvisible()==hidden&&NativeVisibility.invisible(owner)==(mask==1));
        if(owner instanceof NativePlayerHitboxEntity box)check(box.tracked==(mask==1));
        var bytes=GoldCraft.packets.getLast();check(bytes.length==36);var r=new Wire.Reader(bytes);
        check(r.i64()==99);long revision=r.i64();check(revision>previous);previous=revision;
        check(r.i32()==2&&r.i32()==7&&r.i32()==5&&r.i32()==9&&r.i32()==mask);r.finish();
    }
}
var owner=new NativePlayerHitboxEntity();int count=GoldCraft.packets.size();
GoldCraft.available=false;owner.effects=1;NativeVisibility.update(actor,owner);check(owner.tracked&&GoldCraft.packets.size()==count);
owner.effects=0;NativeVisibility.update(actor,owner);check(!owner.tracked&&GoldCraft.packets.size()==count);
GoldCraft.available=true;owner.alive=false;NativeVisibility.update(actor,owner);check(GoldCraft.packets.size()==count);owner.alive=true;
for(var invalid:new HostWorldState.Actor[]{new HostWorldState.Actor(0,7,5,9),new HostWorldState.Actor(65,7,5,9),new HostWorldState.Actor(2,0,5,9),new HostWorldState.Actor(2,7,0,9),new HostWorldState.Actor(2,7,5,0)}){
    NativeVisibility.update(invalid,owner);check(GoldCraft.packets.size()==count);
}
GoldCraft.HOST_WORLD.world=0;NativeVisibility.update(actor,owner);check(GoldCraft.packets.size()==count);
GoldCraft.HOST_WORLD.world=100;NativeVisibility.clear();NativeVisibility.update(new HostWorldState.Actor(3,8,6,10),owner);
var r=new Wire.Reader(GoldCraft.packets.getLast());check(r.i64()==100&&r.i64()==1&&r.i32()==3&&r.i32()==8&&r.i32()==6&&r.i32()==10&&r.i32()==0);r.finish();
System.out.println(checks+" visibility publisher checks: actual effect independent of proxy hiding, tracked removal, legacy, death/identity/reset");
}}
''',encoding='utf-8')
production=[ROOT/name for name in ['neoforge/src/main/java/dev/goldcraft/world/NativeVisibility.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java','neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java']]
java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
subprocess.run([str(java/'javac.exe'),'-encoding','UTF-8','-d',str(classes),*[str(x) for x in src.rglob('*.java')],*[str(x) for x in production]],check=True)
result=subprocess.run([str(java/'java.exe'),'-ea','-cp',str(classes),'VisibilityPublisherTest'],check=True,capture_output=True,text=True)
print(result.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True,output=result.stdout.strip(),sourceHashes={x.relative_to(ROOT).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in production},
    boundary='Production Java publisher and wire; observing API adapters, no live game.'),indent=2)+'\n',encoding='utf-8')
