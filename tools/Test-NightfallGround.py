"""Run actual transformed NightFall ground loops with bounded CPU world adapters."""
import argparse,hashlib,json,os,subprocess,zipfile
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();o=(R/a.output).resolve()
if not o.is_relative_to(R/'analysis'):raise ValueError('Output outside analysis')
for x in [o,*o.parents]:
 if x.is_symlink() or x.is_junction():raise ValueError('Redirected output')
 if x==R:break
o.mkdir(parents=True,exist_ok=False);c=o/'classes';c.mkdir();s=o/'src'
jar=R/'sandbox/neoforge-server/mods/NightFall-1.21.1-Neoforge-3.4.0.jar';core=R/'neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar'
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
if sha(jar)!='ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c':raise ValueError('Producer changed')
files={
'net/minecraft/core/BlockPos.java':'package net.minecraft.core;public class BlockPos{public int x,y,z;public BlockPos(double x,double y,double z){this.x=(int)Math.floor(x);this.y=(int)Math.floor(y);this.z=(int)Math.floor(z);}public static class MutableBlockPos extends BlockPos{public MutableBlockPos(double x,double y,double z){super(x,y,z);}}}',
'net/minecraft/world/level/BlockGetter.java':'package net.minecraft.world.level;public interface BlockGetter{}',
'net/minecraft/world/level/block/Block.java':'package net.minecraft.world.level.block;public class Block{public net.minecraft.world.level.block.state.BlockState defaultBlockState(){return new net.minecraft.world.level.block.state.BlockState(this);}}',
'net/minecraft/world/level/block/BushBlock.java':'package net.minecraft.world.level.block;public class BushBlock extends Block{}',
'net/minecraft/world/level/block/Blocks.java':'package net.minecraft.world.level.block;public class Blocks{public static final Block AIR=new Block(),STONE=new Block(),BEDROCK=new Block(),FLOWER=new BushBlock();}',
'net/minecraft/world/level/block/state/BlockState.java':'package net.minecraft.world.level.block.state;import net.minecraft.world.level.block.*;public record BlockState(Block block){public Block getBlock(){return block;}public boolean isAir(){return block==Blocks.AIR;}public boolean is(Block other){return block==other;}}',
'net/minecraft/world/level/Level.java':'''package net.minecraft.world.level;import net.minecraft.core.*;import net.minecraft.world.level.block.*;import net.minecraft.world.level.block.state.*;public class Level implements BlockGetter{public int reads,hostY=Integer.MIN_VALUE,realY=Integer.MIN_VALUE;public boolean plant;public boolean isOutsideBuildHeight(BlockPos p){return p.y< -64||p.y>=320;}public BlockState getBlockState(BlockPos p){if(++reads>1024)throw new IllegalStateException("unbounded producer loop");return (p.y==realY?Blocks.STONE:plant?Blocks.FLOWER:Blocks.AIR).defaultBlockState();}}''',
'net/minecraft/world/entity/LivingEntity.java':'package net.minecraft.world.entity;public record LivingEntity(net.minecraft.world.level.Level value){public net.minecraft.world.level.Level level(){return value;}}',
'net/minecraft/world/phys/shapes/VoxelShape.java':'package net.minecraft.world.phys.shapes;public record VoxelShape(boolean empty){public boolean isEmpty(){return empty;}}',
'dev/goldcraft/world/HostCollision.java':'package dev.goldcraft.world;public class HostCollision{public static net.minecraft.world.phys.shapes.VoxelShape shape(net.minecraft.world.level.BlockGetter view,net.minecraft.core.BlockPos p){return new net.minecraft.world.phys.shapes.VoxelShape(((net.minecraft.world.level.Level)view).hostY!=p.y);}}',
'probe/GroundBehavior.java':'''package probe;import net.minecraft.world.level.*;import net.minecraft.world.entity.*;public class GroundBehavior{
 static float run(String name,Level world,float y)throws Exception{return (Float)Class.forName("probe.GroundLoops").getMethod(name,LivingEntity.class,float.class,float.class,float.class).invoke(null,new LivingEntity(world),0f,y,0f);}
 public static void main(String[] args)throws Exception{
  var empty=new Level();try{run("original",empty,65);throw new AssertionError("negative control terminated");}catch(java.lang.reflect.InvocationTargetException e){if(!(e.getCause() instanceof IllegalStateException)||empty.reads!=1025)throw e;}
  empty=new Level();if(run("patched",empty,65)!=-65||empty.reads>130)throw new AssertionError("void search not bounded");
  var host=new Level();host.hostY=63;if(run("patched",host,65)!=63||host.reads!=3)throw new AssertionError("host floor missed");
  host=new Level();host.hostY=63;host.plant=true;if(run("patched",host,65)!=63)throw new AssertionError("plant hides host floor");
  var real=new Level();real.realY=60;var control=new Level();control.realY=60;if(run("patched",real,65)!=run("original",control,65)||real.reads!=control.reads)throw new AssertionError("ordinary floor changed");
  var above=new Level();if(run("patched",above,16777216)!=16777216||above.reads!=0)throw new AssertionError("above-world float search");
  System.out.println("PASS actual producer loops: negative void bound, fixed void, host floor, plant/host, unchanged real floor, outside-height float; no block mutations available");
 }
}'''}
for name,text in files.items():
 path=s/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
with zipfile.ZipFile(core) as z:
 for n in ['dev/goldcraft/mixin/NightfallYamatoGroundMixin.class','dev/goldcraft/world/NightfallGroundProbe.class']:
  path=c/n;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(z.read(n))
services=c/'META-INF/services';services.mkdir(parents=True)
(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n');(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
(c/'ground-only.mixins.json').write_text(json.dumps({'required':True,'package':'dev.goldcraft.mixin','compatibilityLevel':'JAVA_21','mixins':['NightfallYamatoGroundMixin'],'injectors':{'defaultRequire':1}}))
runtime=json.loads((R/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'));cp=os.pathsep.join([str(c),str(jar),*runtime['classpath']]);java=R/'.tools/java/jdk-21.0.12.1+1/bin'
fixtures=[R/'tests/nightfall-config/GroundTransform.java',*[R/'tests/nightfall-blade'/n for n in ['BladeMixinService.java','BladeProperties.java']]];runs=[]
def run(name,exe,args):
 f=o/(name+'.args');f.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args))
 p=subprocess.run([str(java/exe),'@'+str(f)],cwd=o,capture_output=True,timeout=45);(o/(name+'.log')).write_bytes(p.stdout+p.stderr);runs.append({'name':name,'exitCode':p.returncode})
 if p.returncode:raise RuntimeError(f'{name} failed; see {o/(name+".log")}')
 print('PASS '+name)
try:
 run('compile','javac.exe',['--release','21','-proc:none','-cp',cp,'-d',c,*s.rglob('*.java'),*fixtures])
 run('transform','java.exe',['-cp',cp,'probe.GroundTransform',jar,c])
 run('behavior','java.exe',['-cp',str(c),'probe.GroundBehavior'])
finally:
 (o/'result.json').write_text(json.dumps({'runs':runs,'inputs':{str(p):sha(p) for p in [jar,core,Path(__file__),*fixtures]},'scope':'Real producer loop and remapped helper bytecode; CPU world/host-shape adapters; no gameplay acceptance'},indent=2))
