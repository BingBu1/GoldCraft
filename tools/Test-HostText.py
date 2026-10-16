"""Compile production HostUi/Wire with observing screen adapters, without a JVM game."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=ROOT / 'analysis/host-text-check')
p.add_argument('--java', type=Path)
a = p.parse_args()
out = a.output.resolve()
if not out.is_relative_to(ROOT / 'analysis'):
    raise ValueError('Output must be inside workspace analysis')
for part in (out, *out.parents):
    if part.is_symlink() or part.is_junction():
        raise ValueError('Refusing redirected output')
    if part == ROOT:
        break
out.mkdir(parents=True, exist_ok=True)
src, classes = out / 'src', out / 'classes'
classes.mkdir(exist_ok=True)
adapters = {
    'com/google/gson/JsonObject.java': '''package com.google.gson;
public class JsonObject { public void addProperty(String name,Number value){} }''',
    'net/minecraft/client/MinecraftClient.java': '''package net.minecraft.client;
import dev.goldcraft.client.HostUi;
public class MinecraftClient {
    static final MinecraftClient INSTANCE=new MinecraftClient();
    public Screen currentScreen=new Screen();
    public static MinecraftClient getInstance(){return INSTANCE;}
    public static class Screen {
        public final StringBuilder text=new StringBuilder();public int key,mods;public boolean sawControl;
        public boolean charTyped(char c,int m){assert HostUi.dispatching();text.append(c);mods=m;return c!='?';}
        public void keyPressed(int k,int scan,int m){key=k;mods=m;sawControl=HostUi.modifier(2);}
        public void keyReleased(int k,int scan,int m){}
        public void mouseMoved(double x,double y){}
        public void mouseDragged(double x,double y,int b,double dx,double dy){}
        public void mouseClicked(double x,double y,int b){}
        public void mouseReleased(double x,double y,int b){}
        public void mouseScrolled(double x,double y,double h,double v){}
    }
}''',
    'net/neoforged/neoforge/client/ClientHooks.java': '''package net.neoforged.neoforge.client;
import net.minecraft.client.MinecraftClient.Screen;
public class ClientHooks {
    public static int pre,post;
    public static boolean onScreenCharTypedPre(Screen s,char c,int m){pre++;return c=='!';}
    public static void onScreenCharTypedPost(Screen s,char c,int m){post++;}
}''',
    'dev/goldcraft/client/HudExporter.java': '''package dev.goldcraft.client;
import net.minecraft.client.MinecraftClient;
public class HudExporter {
    final MinecraftClient.Screen displayed=MinecraftClient.getInstance().currentScreen;
    public int width(){return 800;}public int height(){return 600;}public long menuId(){return 21;}
    public MinecraftClient.Screen screen(){return displayed;}
    public static boolean nativeDeathHud(MinecraftClient c){return false;}
}''',
    'dev/goldcraft/client/HostInput.java': '''package dev.goldcraft.client;
public class HostInput {static boolean enabled=true;public static boolean hosted(){return enabled;}}''',
    'dev/goldcraft/client/GoldCraftClient.java': '''package dev.goldcraft.client;
public class GoldCraftClient {public static final Host HOST=new Host();public static class Host {public long epoch(){return 11;}}}''',
}
for name, text in adapters.items():
    path = src / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
production = [ROOT / 'neoforge/src/client/java/dev/goldcraft/client/HostUi.java',
              ROOT / 'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java',
              ROOT / 'neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java',
              ROOT / 'tests/host-ui/HostUiProbe.java']
java = a.java or next((ROOT / '.tools/java').glob('jdk-21*/bin'))
args = ['--release', '21', '-encoding', 'UTF-8', '-proc:none', '-d', str(classes),
        *map(str, production), *map(str, src.rglob('*.java'))]
argfile = out / 'javac.args'
argfile.write_text('\n'.join('"' + s.replace('\\', '/') + '"' for s in args), encoding='utf-8')
runs = []
for name, command in [('compile', [str(java / 'javac.exe'), '-J-Duser.language=en', '@' + str(argfile)]),
                      ('test', [str(java / 'java.exe'), '-ea', '-cp', str(classes), 'dev.goldcraft.client.HostUiProbe'])]:
    result = subprocess.run(command, capture_output=True, timeout=60)
    (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
    runs.append(dict(name=name, exitCode=result.returncode))
    print(name, result.returncode, (result.stdout + result.stderr).decode('utf-8', errors='replace').encode('ascii', errors='backslashreplace').decode())
    if result.returncode:
        break
report = dict(passed=len(runs) == 2 and all(r['exitCode'] == 0 for r in runs), runs=runs,
              sourceHashes={str(f.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest() for f in production},
              boundary='Production HostUi and Wire; observing Minecraft screen/ownership adapters. No real game or input method acceptance.')
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
raise SystemExit(0 if report['passed'] else 1)
