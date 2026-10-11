"""Run production light snapshot tests and unchanged WorldExporter method extraction.

Minecraft block/render inputs and the transport endpoint are explicit fixtures.
This does not launch a game or establish live Renderer lighting acceptance.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[2]
output=root/'build/world-lights-tests'
output.mkdir(parents=True,exist_ok=True)
source=root/'neoforge/src/client/java/dev/goldcraft/client/WorldExporter.java'
text=source.read_text(encoding='utf-8-sig')


def method(signature):
    start=text.index(signature)
    level=0
    for at in range(text.index('{',start),len(text)):
        level+=text[at]=='{'
        level-=text[at]=='}'
        if not level:
            return text[start:at+1]
    raise ValueError(f'Unclosed production method: {signature}')


header=text[text.index('public final class WorldExporter {'):text.index('    public void presentationFrame()')]
signatures=('public void bind(', 'public void resendScene(', 'public void reset(',
            'public static void atlasUploaded(', 'private void renderFrameInternal(',
            'private void refresh(', 'private void exportSection(', 'private void sendLights(')
parts=[method(signature) for signature in signatures]
support=Path(__file__).with_name('WorldExporterFixture.inc')
wrapper=output/'src/dev/goldcraft/client/WorldExporter.java'
wrapper.parent.mkdir(parents=True,exist_ok=True)
wrapper.write_text('package dev.goldcraft.client;\nimport java.util.*;\n'
                   'import dev.goldcraft.bridge.Wire;\n'
                   'import dev.goldcraft.client.SectionLightSnapshot.Lamp;\n'
                   +header+'\n'+'\n'.join(parts)+'\n'+support.read_text(encoding='utf-8')+'\n}\n',encoding='utf-8')
registry=output/'src/net/minecraft/registry/Registries.java'
registry.parent.mkdir(parents=True,exist_ok=True)
registry.write_text('''package net.minecraft.registry;
public final class Registries {
    public static final Registries BLOCK=new Registries();
    public Id getId(Object block){return new Id(block.toString());}
    public record Id(String path){public String getPath(){return path;}}
}
''',encoding='utf-8')
runner=output/'src/RunLightTests.java'
runner.write_text('''import org.junit.platform.launcher.core.*;
import org.junit.platform.launcher.listeners.*;
import org.junit.platform.engine.discovery.DiscoverySelectors;
public class RunLightTests {
    public static void main(String[] args) {
        var summary=new SummaryGeneratingListener();var launcher=LauncherFactory.create();
        launcher.registerTestExecutionListeners(summary);
        launcher.execute(LauncherDiscoveryRequestBuilder.request().selectors(
            DiscoverySelectors.selectClass("dev.goldcraft.client.SectionLightSnapshotTest"),
            DiscoverySelectors.selectClass("dev.goldcraft.client.WorldExporterLightsTest")).build());
        summary.getSummary().printTo(new java.io.PrintWriter(System.out));
        summary.getSummary().printFailuresTo(new java.io.PrintWriter(System.out));
        if(summary.getSummary().getTestsSucceededCount()!=21||summary.getSummary().getTestsFailedCount()!=0)System.exit(1);
    }
}
''',encoding='utf-8')
cache=root/'.tools/gradle-cache/caches/modules-2/files-2.1'
dependencies=[]
for group in ('org.junit.jupiter','org.junit.platform','org.opentest4j','org.apiguardian'):
    dependencies.extend((cache/group).rglob('*.jar'))
if not dependencies:
    raise RuntimeError('Pinned workspace JUnit dependencies are unavailable; run the normal Java preparation first.')
classpath=os.pathsep.join(str(path) for path in dependencies)
classes=output/'classes'
classes.mkdir(exist_ok=True)
jdk=root/'.tools/java/jdk-21.0.12.1+1/bin'
sources=[wrapper,registry,runner,
         root/'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java',
         root/'neoforge/src/client/java/dev/goldcraft/client/SectionLightSnapshot.java',
         root/'neoforge/src/test/java/dev/goldcraft/client/SectionLightSnapshotTest.java',
         Path(__file__).with_name('WorldExporterLightsTest.java')]
tracked=[source,support,Path(__file__),*sources[3:]]
report={'boundary':__doc__,'sources':{str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked},
        'extractedMethods':{signature:hashlib.sha256(part.encode()).hexdigest() for signature,part in zip(signatures,parts)},
        'gameplayValidated':False,'runs':[]}
for name,command in [('compile',[str(jdk/'javac.exe'),'--release','21','-encoding','UTF-8','-cp',classpath,'-d',str(classes),*map(str,sources)]),
                     ('test',[str(jdk/'java.exe'),'-cp',str(classes)+os.pathsep+classpath,'RunLightTests'])]:
    result=subprocess.run(command,cwd=output,capture_output=True,text=True,encoding='utf-8')
    (output/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    report['runs'].append({'name':name,'exitCode':result.returncode})
    print(result.stdout+result.stderr)
    if result.returncode:
        break
(output/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
raise SystemExit(result.returncode)
