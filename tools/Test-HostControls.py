"""Check hosted input without launching or sending input to any game.

Production dispatch/focus and real Sponge targets; real TACZ/YSM gate bytecode.
OS, transport and callback receivers are observing adapters, not gameplay proof.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=ROOT/'build/host-controls-tests')
p.add_argument('--mods', type=Path, required=True)
p.add_argument('--production-jar', type=Path, help='Also validate the shipped Mojang-remapped mixins')
a = p.parse_args()
out = a.output.resolve()
if not out.is_relative_to(ROOT/'build'):
    raise ValueError('Output must stay inside workspace build')
for part in (out, *out.parents):
    if part.is_symlink() or part.is_junction():
        raise ValueError('Refusing redirected output')
    if part == ROOT:
        break
src, classes, transform = out/'src', out/'classes', out/'transform'
for directory in (src, classes, transform):
    directory.mkdir(parents=True, exist_ok=True)
adapters = {
'org/jetbrains/annotations/Nullable.java': 'public @interface Nullable {}',
'dev/goldcraft/client/HostInput.java': '''public class HostInput {
public static boolean control=true,active=true;public static int generation=3;
public static boolean controlling(){return control;}public static boolean active(){return active;}
public static boolean hosted(){return control;}public static int life(){return generation;}}''',
'dev/goldcraft/client/HostAudio.java': '''public class HostAudio {public static boolean physical;
public static boolean standaloneFocused(net.minecraft.client.MinecraftClient c){return physical;}}''',
'dev/goldcraft/client/GoldCraftClient.java': '''public class GoldCraftClient {public static final Host HOST=new Host();
public static class Host {public long epoch(){return 11;}}}''',
'dev/goldcraft/client/HudExporter.java': '''public class HudExporter {
public int width(){return 800;}public int height(){return 600;}public long menuId(){return 21;}
public net.minecraft.client.gui.screen.Screen screen(){return net.minecraft.client.MinecraftClient.getInstance().currentScreen;}
public static boolean nativeDeathHud(net.minecraft.client.MinecraftClient c){return false;}}''',
'net/minecraft/client/MinecraftClient.java': '''public class MinecraftClient {
public static final boolean IS_SYSTEM_MAC=false;static final MinecraftClient INSTANCE=new MinecraftClient();
public net.minecraft.client.gui.screen.Screen currentScreen;public net.minecraft.client.gui.screen.Overlay overlay;
public Mouse mouse=new Mouse();public Keyboard keyboard=new Keyboard();public Options options=new Options();
public boolean windowFocused;public static MinecraftClient getInstance(){return INSTANCE;}
public net.minecraft.client.gui.screen.Overlay getOverlay(){return overlay;}
public boolean isWindowFocused(){throw new AssertionError("transplant required");}
public net.minecraft.client.util.Window getWindow(){return new net.minecraft.client.util.Window();}
public static class Options{public net.minecraft.client.option.KeyBinding attackKey=new net.minecraft.client.option.KeyBinding("attack",0,true),
useKey=new net.minecraft.client.option.KeyBinding("use",1,true);public net.minecraft.client.option.KeyBinding[] allKeys={attackKey,useKey};}}''',
'net/minecraft/client/Mouse.java': '''public class Mouse implements dev.goldcraft.client.mixin.MouseAccessor {
public boolean cursorLocked;public double cursorDeltaX,cursorDeltaY,lastTickTime;
public boolean isCursorLocked(){throw new AssertionError("transplant required");}
public void goldcraft$button(long w,int b,int action,int mods){
net.minecraft.client.option.KeyBinding.setKeyPressed(net.minecraft.client.util.InputUtil.Type.MOUSE.createFromCode(b),action==1);
dev.goldcraft.client.ControlsProbe.mouseEvents++;if(dev.goldcraft.client.ActualTacz.gate())dev.goldcraft.client.ControlsProbe.gunEdges++;
}
public void goldcraft$scroll(long w,double h,double v){dev.goldcraft.client.ControlsProbe.scroll+=v;}}''',
'net/minecraft/client/Keyboard.java': '''public class Keyboard {public void onKey(long w,int code,int scan,int action,int mods){
var key=net.minecraft.client.util.InputUtil.fromKeyCode(code,scan);net.minecraft.client.option.KeyBinding.setKeyPressed(key,action!=0);
dev.goldcraft.client.ControlsProbe.keyEvents++;
if(action==1&&dev.goldcraft.client.ActualYsm.gate()){
var event=new net.neoforged.neoforge.client.event.InputEvent.Key(code,scan,action,mods);
if(dev.goldcraft.client.ActualYsm.matches(event,dev.goldcraft.client.ControlsProbe.chord))dev.goldcraft.client.ControlsProbe.chords++;
if(dev.goldcraft.client.ActualYsm.matches(event,dev.goldcraft.client.ControlsProbe.wheel))dev.goldcraft.client.ControlsProbe.wheels++;
}}}''',
'net/minecraft/client/option/KeyBinding.java': '''import java.util.*;import net.minecraft.client.util.InputUtil;
import net.neoforged.neoforge.client.settings.KeyModifier;
public class KeyBinding {static final List<KeyBinding> all=new ArrayList<>();public String name;public int code;public boolean mouse,pressed;
public KeyModifier modifier=KeyModifier.NONE;public KeyBinding(String n,int c,boolean m){name=n;code=c;mouse=m;all.add(this);}
public static void setKeyPressed(InputUtil.Key key,boolean value){for(var k:all)if(k.code==key.code()&&k.mouse==key.mouse())k.pressed=value;}
public static void onKeyPressed(InputUtil.Key key){}public boolean matchesKey(int c,int scan){return !mouse&&c==code;}
public boolean matchesMouse(int c){return mouse&&c==code;}public KeyModifier getKeyModifier(){return modifier;}
public String getTranslationKey(){return name;}public String getBoundKeyTranslationKey(){return name;}
public boolean isPressed(){return pressed&&modifier.isActive(null);}}''',
'net/minecraft/client/util/InputUtil.java': '''public class InputUtil {
public record Key(int code,boolean mouse){public int getCode(){return code;}}
public static Key fromKeyCode(int code,int scan){return new Key(code,false);}
public static boolean isKeyPressed(long window,int code){throw new AssertionError("transplant required");}
public enum Type {KEYSYM,MOUSE;public Key createFromCode(int code){return new Key(code,this==MOUSE);}}}''',
'net/minecraft/client/util/GlfwUtil.java': '''public class GlfwUtil {public static double getTime(){return 23;}}''',
'net/minecraft/client/util/Window.java': '''public class Window {public long getHandle(){return 7;}}''',
'net/minecraft/client/gui/screen/Screen.java': '''public class Screen {
public static boolean hasAltDown(){throw new AssertionError("transplant required");}
public static boolean hasShiftDown(){throw new AssertionError("transplant required");}
public static boolean hasControlDown(){throw new AssertionError("transplant required");}
public boolean charTyped(char c,int mods){return true;}public boolean keyPressed(int k,int s,int m){return true;}
public boolean keyReleased(int k,int s,int m){return true;}public void mouseMoved(double x,double y){}
public boolean mouseDragged(double x,double y,int b,double dx,double dy){return true;}
public boolean mouseClicked(double x,double y,int b){return true;}public boolean mouseReleased(double x,double y,int b){return true;}
public boolean mouseScrolled(double x,double y,double h,double v){return true;}}''',
'net/minecraft/client/gui/screen/Overlay.java': 'public class Overlay {}',
'net/minecraft/text/Text.java': '''public class Text {public static Text translatable(String s,Object... a){return new Text();}}''',
'net/neoforged/neoforge/client/settings/IKeyConflictContext.java': '''public interface IKeyConflictContext {boolean conflicts(IKeyConflictContext other);}''',
'net/neoforged/neoforge/client/settings/KeyConflictContext.java': '''public enum KeyConflictContext implements IKeyConflictContext {IN_GAME;public boolean conflicts(IKeyConflictContext other){return this==other;}}''',
'net/neoforged/neoforge/client/event/InputEvent.java': '''public class InputEvent {public record Key(int key,int scan,int action,int mods){public int getKey(){return key;}public int getScanCode(){return scan;}}}''',
'net/neoforged/neoforge/client/ClientHooks.java': '''public class ClientHooks {
public static boolean onScreenCharTypedPre(net.minecraft.client.gui.screen.Screen s,char c,int mods){return false;}
public static void onScreenCharTypedPost(net.minecraft.client.gui.screen.Screen s,char c,int mods){}}''',
'org/lwjgl/glfw/GLFW.java': '''public class GLFW {public static final int GLFW_RELEASE=0,GLFW_PRESS=1,GLFW_REPEAT=2,
GLFW_KEY_LEFT_SHIFT=340,GLFW_KEY_RIGHT_SHIFT=344,GLFW_KEY_LEFT_CONTROL=341,GLFW_KEY_RIGHT_CONTROL=345,
GLFW_KEY_LEFT_ALT=342,GLFW_KEY_RIGHT_ALT=346,GLFW_KEY_LEFT_SUPER=343,GLFW_KEY_RIGHT_SUPER=347;
public static int glfwGetKeyScancode(int code){return code;}public static int glfwGetKey(long w,int code){return 0;}}''',
'dev/goldcraft/client/ActualTacz.java': '''public class ActualTacz {public static boolean gate(){throw new AssertionError("transplant required");}}''',
'dev/goldcraft/client/ActualYsm.java': '''public class ActualYsm {public static boolean gate(){throw new AssertionError("transplant required");}
public static boolean matches(net.neoforged.neoforge.client.event.InputEvent.Key e,net.minecraft.client.option.KeyBinding k){throw new AssertionError("transplant required");}}''',
}
for name, body in adapters.items():
    f=src/name;f.parent.mkdir(parents=True,exist_ok=True)
    f.write_text('package '+name.rsplit('/',1)[0].replace('/','.')+';\n'+body+'\n',encoding='utf-8')
runtime=json.loads((ROOT/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
games=list((ROOT/'neoforge/.gradle/loom-cache/minecraftMaven/net/minecraft').glob(
    'neoforge-21.1.256-minecraft-merged-*/1.21.1-loom.mappings.*/neoforge-21.1.256-*-neoforge-21.1.256.jar'))
assert len(games)==1
game=games[0]
import zipfile
# The pinned remapped NeoForge source supplies the actual modifier selection.
with zipfile.ZipFile(game.with_name(game.stem+'-sources.jar')) as z:
    modifier=src/'net/neoforged/neoforge/client/settings/KeyModifier.java'
    modifier.write_bytes(z.read('net/neoforged/neoforge/client/settings/KeyModifier.java'))
java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
runs=[]
def run(name,exe,args,timeout=90):
    f=out/(name+'.args');f.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),encoding='utf-8')
    result=subprocess.run([str(java/exe),'-J-Duser.language=en' if exe=='javac.exe' else '-Duser.language=en','@'+str(f)],cwd=out,capture_output=True,timeout=timeout)
    (out/(name+'.log')).write_bytes(result.stdout+result.stderr)
    runs.append(dict(name=name,exitCode=result.returncode))
    print(name,result.returncode,(result.stdout+result.stderr).decode(errors='replace')[-1800:])
    if result.returncode:raise RuntimeError(name+' failed')
cp=os.pathsep.join(map(str,[classes,*runtime['classpath']]))
production=[ROOT/'neoforge/src/client/java/dev/goldcraft/client'/n for n in ('HostFocus.java','HostKeys.java','HostUi.java','mixin/MouseAccessor.java')]
production += [ROOT/'neoforge/src/main/java/dev/goldcraft/bridge'/n for n in ('Wire.java','NativeArmorProtocol.java')]
probe=ROOT/'tests/host-controls/ControlsProbe.java'
run('compile','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,*src.rglob('*.java'),*production,probe])
services=transform/'META-INF/services';services.mkdir(parents=True,exist_ok=True)
(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
(transform/'controls.mixins.json').write_text(json.dumps(dict(required=True,package='dev.goldcraft.client.mixin',compatibilityLevel='JAVA_21',
    client=['HostFocusMixin','HostMouseMixin','HostKeyStateMixin'],injectors=dict(defaultRequire=1))))
mixins=[ROOT/'neoforge/src/client/java/dev/goldcraft/client/mixin'/n for n in ('HostFocusMixin.java','HostMouseMixin.java','HostKeyStateMixin.java')]
fixtures=[ROOT/'tests/host-controls/ControlsTransform.java',*[ROOT/'tests/nightfall-blade'/n for n in ('BladeMixinService.java','BladeProperties.java')]]
transform_cp=os.pathsep.join(map(str,[transform,game,ROOT/'neoforge/build/classes/java/main',*runtime['classpath']]))
run('compile-transform','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',transform_cp,'-d',transform,*fixtures,*mixins])
tacz=next(a.mods.resolve().glob('tacz-*.jar'));ysm=next(a.mods.resolve().glob('ysm-*.jar'))
run('transform','java.exe',['-cp',transform_cp,'probe.ControlsTransform',game,classes,out,tacz,ysm])
run('regression','java.exe',['-ea','-cp',cp,'dev.goldcraft.client.ControlsProbe'])
if a.production_jar:
    shipped=a.production_jar.resolve()
    libraries=list((ROOT/'.tools/neoforge-runtime/21.1.256/libraries').rglob('*.jar'))
    production_game=next(f for f in libraries if f.name=='client-1.21.1-20240808.144430-srg.jar')
    production_transform=out/'production-transform';production_transform.mkdir(exist_ok=True)
    import shutil
    shutil.copytree(services,production_transform/'META-INF/services',dirs_exist_ok=True)
    shutil.copyfile(transform/'controls.mixins.json',production_transform/'controls.mixins.json')
    production_cp=os.pathsep.join(map(str,[production_transform,shipped,production_game,*libraries]))
    prod_fixture=ROOT/'tests/host-controls/ProductionTransform.java'
    run('compile-production-transform','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',production_cp,'-d',production_transform,prod_fixture,*fixtures[1:]])
    run('production-transform','java.exe',['-cp',production_cp,'probe.ProductionTransform',production_game,out])
    production.extend([shipped,prod_fixture])
report=dict(passed=True,runs=runs,inputs={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [game,tacz,ysm,*production,*mixins,*fixtures,probe]},
    boundary='Production focus/key/UI dispatch; actual Sponge transforms and pinned TACZ/YSM gate/chord bytecode plus NeoForge modifier. OS/transport/input callback receivers adapted; no running game touched; no live gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
