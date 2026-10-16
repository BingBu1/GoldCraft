"""Exercise production vision publication through observing Minecraft API adapters."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT/'build/status-vision-publisher')
out = parser.parse_args().output.resolve()
if not out.is_relative_to(ROOT/'build'):
    raise ValueError('Output outside workspace build directory')
for part in (out, *out.parents):
    if part.is_symlink() or part.is_junction():
        raise ValueError('Redirected output')
    if part == ROOT:
        break
src, classes = out/'src', out/'classes'
classes.mkdir(parents=True, exist_ok=True)
adapters = {
    'net/minecraft/entity/effect/StatusEffects.java':
        'public class StatusEffects {public static final int NIGHT_VISION=0,BLINDNESS=1,DARKNESS=2;}',
    'net/minecraft/entity/effect/StatusEffectInstance.java': '''public class StatusEffectInstance {
public int duration;public float fade;public StatusEffectInstance(int ticks,float fade){duration=ticks;this.fade=fade;}
public boolean isInfinite(){return duration==-1;}public int getDuration(){return duration;}
public boolean isDurationBelow(int ticks){return !isInfinite()&&duration<ticks;}
public float getFadeFactor(net.minecraft.entity.LivingEntity owner,float delta){if(delta!=1)throw new AssertionError();return fade;}}''',
    'net/minecraft/entity/LivingEntity.java': '''import net.minecraft.entity.effect.StatusEffectInstance;
public class LivingEntity {public int age;public boolean alive=true;
public StatusEffectInstance[] effects=new StatusEffectInstance[3];public boolean isAlive(){return alive;}
public StatusEffectInstance getStatusEffect(int effect){return effects[effect];}}''',
    'net/minecraft/util/math/MathHelper.java': '''public class MathHelper {
public static float sin(float value){return (float)Math.sin(value);}
public static float cos(float value){return (float)Math.cos(value);}}''',
    'dev/goldcraft/bridge/HostWorldState.java': '''public class HostWorldState {
public long world=99;public long epoch(){return world;}public record Actor(int slot,int serial,int spawn,int life){}}''',
    'dev/goldcraft/GoldCraft.java': '''import java.util.*;import dev.goldcraft.bridge.HostWorldState;
public class GoldCraft {public static final HostWorldState HOST_WORLD=new HostWorldState();public static boolean available=true;
public static final List<byte[]> packets=new ArrayList<>();public static boolean visionAvailable(){return available;}
public static boolean sendVision(byte[] payload){if(!available)return false;packets.add(payload);return true;}}''',
}
for name, body in adapters.items():
    path = src/name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('package '+name.rsplit('/', 1)[0].replace('/', '.')+';\n'+body+'\n', encoding='utf-8')
(src/'VisionPublisherTest.java').write_text('''import dev.goldcraft.GoldCraft;
import dev.goldcraft.world.NativeVision;import dev.goldcraft.bridge.*;
import net.minecraft.entity.LivingEntity;import net.minecraft.entity.effect.StatusEffectInstance;
public class VisionPublisherTest {
static int checks;static long revision;static HostWorldState.Actor actor=new HostWorldState.Actor(2,7,5,9);
static void check(boolean ok){checks++;if(!ok)throw new AssertionError(checks);}
static float[] publish(LivingEntity owner){NativeVision.update(actor,owner);byte[] bytes=GoldCraft.packets.getLast();
 check(bytes.length==48);var r=new Wire.Reader(bytes);check(r.i64()==99);long next=r.i64();check(next>revision);revision=next;
 check(r.i32()==2&&r.i32()==7&&r.i32()==5&&r.i32()==9);float[] v={r.f32(),r.f32(),r.f32(),r.f32()};r.finish();
 for(float x:v)check(Float.isFinite(x)&&x>=0&&x<=1);check(v[3]<=v[2]*.45f+.00001f);return v;}
static void near(float a,float b){check(Math.abs(a-b)<.00002);}
public static void main(String[] args){
 var owner=new LivingEntity();for(float x:publish(owner))near(x,0);
 for(int ticks:new int[]{-1,1200,200,199,198,195,100,5,1}){
  owner.effects[0]=new StatusEffectInstance(ticks,0);float[] v=publish(owner);
  near(v[0],ticks<0||ticks>=200?1:.7f+(float)Math.sin(ticks*(float)Math.PI*.2f)*.3f);near(v[1],0);near(v[2],0);
 }
 owner.effects[0]=null;
 for(int ticks:new int[]{-1,100,20,10,1,0}){owner.effects[1]=new StatusEffectInstance(ticks,0);
  float[] v=publish(owner);near(v[0],0);near(v[1],ticks<0?1:Math.min(ticks/20f,1));}
 owner.effects[1]=null;
 for(float fade:new float[]{0,.25f,.5f,1})for(int age:new int[]{0,10,20,40,80}){
  owner.age=age;owner.effects[2]=new StatusEffectInstance(100,fade);float[] v=publish(owner);near(v[2],fade);
  near(v[3],Math.max(0,(float)Math.cos(age*(float)Math.PI*.025f)*.45f*fade));}
 owner.age=0;owner.effects[0]=new StatusEffectInstance(-1,0);owner.effects[1]=new StatusEffectInstance(-1,0);
 float[] combined=publish(owner);near(combined[0],1);near(combined[1],1);near(combined[2],1);near(combined[3],.45f);
 owner.effects=new StatusEffectInstance[3];for(float x:publish(owner))near(x,0);
 int count=GoldCraft.packets.size();GoldCraft.available=false;NativeVision.update(actor,owner);check(GoldCraft.packets.size()==count);
 GoldCraft.available=true;owner.alive=false;NativeVision.update(actor,owner);check(GoldCraft.packets.size()==count);owner.alive=true;
 for(var bad:new HostWorldState.Actor[]{new HostWorldState.Actor(0,7,5,9),new HostWorldState.Actor(65,7,5,9),
  new HostWorldState.Actor(2,0,5,9),new HostWorldState.Actor(2,7,0,9),new HostWorldState.Actor(2,7,5,0)}){
  NativeVision.update(bad,owner);check(GoldCraft.packets.size()==count);}
 GoldCraft.HOST_WORLD.world=0;NativeVision.update(actor,owner);check(GoldCraft.packets.size()==count);
 GoldCraft.HOST_WORLD.world=100;NativeVision.clear();NativeVision.update(new HostWorldState.Actor(3,8,6,10),owner);
 var r=new Wire.Reader(GoldCraft.packets.getLast());check(r.i64()==100&&r.i64()==1&&r.i32()==3&&r.i32()==8&&r.i32()==6&&r.i32()==10);
 for(int i=0;i<4;i++)near(r.f32(),0);r.finish();
 System.out.println(checks+" vision publisher checks: duration/fade/pulse, coexistence/removal, capability/death/identity/world reset");
}}
''', encoding='utf-8')
production = [ROOT/name for name in [
    'neoforge/src/main/java/dev/goldcraft/world/NativeVision.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java']]
java = ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
subprocess.run([str(java/'javac.exe'), '-encoding', 'UTF-8', '-d', str(classes),
                *map(str, src.rglob('*.java')), *map(str, production)], check=True)
result = subprocess.run([str(java/'java.exe'), '-ea', '-cp', str(classes), 'VisionPublisherTest'],
                        check=True, capture_output=True, text=True)
print(result.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True, output=result.stdout.strip(),
    sourceHashes={x.relative_to(ROOT).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in production},
    boundary='Production publisher and wire; observing Minecraft effect API, no live gameplay.'), indent=2)+'\n', encoding='utf-8')
