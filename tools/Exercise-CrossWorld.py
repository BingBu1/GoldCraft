"""Actual CS-form input/weapon tests against authoritative Minecraft objects in sandbox B."""
import importlib.util
import json
import math
import re
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/file)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
runtime=module('cross_runtime','Exercise-Renderer.py')
native=module('cross_native','GoldSrc-Command.py')
DIM='goldcraft:cs_assault_f6725c06'
UUID='747e1ae23a833ddb8cf25e0cadb6c7a0'
TAG='goldcraft_native14_test'

def main():
    game=runtime.Sandbox('cs-client-b')
    prefix=ROOT/'analysis/goldcraft-tests'/f'cross-world-{int(time.time())}'
    report={'source':'Actual native +forward/+attack/+reload, ReAPI fixtures and independent Minecraft server state','checks':{},'phases':{},'commands':game.commands,'nativeCommands':[]}
    created=set()
    def cs(cmd):
        response=native.command(cmd);report['nativeCommands'].append({'command':cmd,'response':response});return response
    def server():return runtime.read_json(ROOT/'sandbox/cs-server/logs/goldcraft-server-status.json')
    def mcserver():return runtime.read_json(ROOT/'sandbox/cs-server/logs/minecraft-server-status.json')
    def actor():return next(a for a in server()['actors'] if a['uuid']==UUID)
    def wait(predicate,label,seconds=8):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=predicate()
            if value:return value
            time.sleep(.05)
        raise TimeoutError(label)
    def check(label,result):
        report['checks'][label]=bool(result)
        if not result:raise AssertionError(label)
    def form(value):
        game.host(f'form {value}')
        wait(lambda:actor()['minecraftForm']==bool(value) and game.status()['minecraftForm']==bool(value),'form owner')
        if value:wait(lambda:actor()['controlled'] and game.status()['minecraftControl'],'MC ownership')
        else:wait(lambda:not actor()['controlled'] and not game.status()['minecraftControl'],'native ownership')
        time.sleep(.5)
    def place(x=320,y=176,z=36.03125):
        cs(f'gc_test_position #{actor()["userid"]} {x} {y} {z}')
        wait(lambda:abs(actor()['origin'][0]-x)<.25 and abs(actor()['origin'][1]-y)<.25,'native fixture relocation')
        time.sleep(.25)
    def view(pitch=0):game.host(f'view 0 {pitch}')
    def state():
        d=game.status();return {'native':{k:d[k] for k in ['origin','minecraftFeet','viewAngles','minecraftForm','minecraftControl','inputActive','windowFocused','hudVisible','minecraftHands','hostViewModel','viewModelSuppressed','glError','particleFrames']},'actor':actor(),'objects':mcserver().get('minecraftObjects')}
    def run(name,cmd,seconds):
        phase={'before':state(),'samples':[]};report['phases'][name]=phase
        game.host(cmd);end=time.monotonic()+seconds
        while time.monotonic()<end:phase['samples'].append(state());time.sleep(.04)
        phase['after']=state();return phase
    def capture(name):
        report['phases'][name]={'capture':game.capture(prefix.with_name(prefix.name+'-'+name+'.bmp'))}
    def block(pos,name):
        if pos not in created:
            result=game.minecraft(f'execute in {DIM} if block {pos} minecraft:air')
            if 'passed' not in result.lower():raise RuntimeError(f'Fixture space is already occupied: {pos}')
        game.minecraft(f'execute in {DIM} run setblock {pos} minecraft:{name}')
        created.add(pos)
    def remove(pos):
        if pos in created:game.minecraft(f'execute in {DIM} run setblock {pos} minecraft:air');created.remove(pos)
    def health():
        response=game.minecraft(f'execute in {DIM} run data get entity @e[tag={TAG},limit=1] Health')
        m=re.search(r':\s*(-?[0-9.]+)f\s*$',response)
        return float(m[1]) if m else 0.0
    def pig():
        game.minecraft(f'execute in {DIM} run kill @e[tag={TAG}]')
        game.minecraft(f'execute in {DIM} run summon minecraft:pig 14 64.001 -5.5 {{Tags:["{TAG}"],NoAI:1b,PersistenceRequired:1b}}')
        wait(lambda:health()>0,'live test pig')
        time.sleep(.3)
    def equip(weapon):
        cs(f'gc_test_equip #{actor()["userid"]} {weapon}');time.sleep(1.2)
    try:
        wait(lambda:game.status()['inputActive'] and game.status()['windowFocused'],'B foreground')
        report['before']=state();report['epoch']=server()['world']
        form(0);check('CS form restores native movement HUD and weapon',not game.status()['viewModelSuppressed'] and game.status()['hostViewModel']==1 and not game.status()['hudVisible'] and actor()['moveType']==3)
        place();view()
        for y in range(64,67):block(f'14 {y} -6','bedrock')
        wait(lambda:server()['minecraftObjects']>=3 and game.status().get('nativeColliders',0)>=3,'native and predicted proxy geometry')
        phase=run('blocked_walk','forward 1.3',1.65)
        final=phase['after']['actor']['origin']
        check('native player stops at MC wall with 16-unit hull',431.5<final[0]<432.1 and abs(final[1]-176)<.2)
        check('native prediction respects MC wall',max(p['native']['origin'][0] for p in phase['samples'])<432.2)
        capture('blocked-native')
        feet=game.status()['minecraftFeet'];oldlife=actor()['life'];form(1)
        check('CS to MC transition preserves feet and advances life',actor()['life']>oldlife and max(abs(x-y) for x,y in zip(feet,game.status()['minecraftFeet']))<.12)
        check('MC form restores hand HUD and native weapon suppression',wait(lambda:game.status()['viewModelSuppressed'] and game.status()['hudVisible'],'MC hand/HUD'))
        form(0)
        for y in range(64,67):remove(f'14 {y} -6')
        time.sleep(.4);place();view();phase=run('clear_walk','forward .65',1.1)
        check('removing MC wall immediately restores native movement',phase['after']['actor']['origin'][0]>455)
        place();equip('weapon_usp');pig();view(math.degrees(math.atan2(62.03-14.4,128)))
        before=health();capture('pig-before');run('usp_shot','attack .08',.7);after=health()
        report['pigHealth']={'before':before,'after':after};check('real CS pistol damages MC pig exactly once',0<after<before)
        capture('pig-hurt')
        clip_before=cs('gc_amxx_status');run('reload','reload .1',3.2);clip_after=cs('gc_amxx_status')
        report['reload']={'before':clip_before,'after':clip_after}
        check('native reload binding remains usable','clip=12' in next(line for line in clip_after.splitlines() if f'userid={actor()["userid"]} ' in line))
        for y in range(64,67):block(f'12 {y} -6','bedrock')
        time.sleep(.3);before=health();run('occluded_shot','attack .08',.6);after=health()
        check('MC solid blocks native bullets before the pig',before==after and before>0)
        for y in range(64,67):remove(f'12 {y} -6')
        game.minecraft(f'execute in {DIM} run kill @e[tag={TAG}]');time.sleep(.3)
        block('12 65 -6','white_wool');view(math.degrees(math.atan2(62.03-48,80)))
        time.sleep(.3);run('wool_shot_1','attack .08',.4);run('wool_shot_2','attack .08',.6)
        air=game.minecraft(f'execute in {DIM} if block 12 65 -6 minecraft:air')
        check('CS bullets authoritatively break MC block','passed' in air.lower())
        check('MC block break emits particles in CS form',game.status()['particleFrames']>report['before']['native']['particleFrames'])
        capture('block-broken')
        pig();place(416);equip('weapon_knife');view(math.degrees(math.atan2(62.03-14.4,32)))
        before=health();run('knife','attack .15',.8);after=health()
        report['knifeHealth']={'before':before,'after':after};check('real CS knife damages MC entity',after<before)
        check('server object bridge has no overflow',mcserver()['minecraftObjects']['overflow']==0)
        check('renderer remains valid',game.status()['glError']==0)
        report['after']=state();report['outcome']='PASSED'
    except Exception as error:
        report['outcome']='INCOMPLETE';report['error']=repr(error)
    finally:
        try:
            for pos in list(created):remove(pos)
            game.minecraft(f'execute in {DIM} run kill @e[tag={TAG}]')
        except Exception as error:report['cleanupError']=repr(error)
        game.connection.close();prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report['outcome'],'checks':report['checks'],'error':report.get('error')},indent=2))
    return 0 if report['outcome']=='PASSED' else 1

if __name__=='__main__':raise SystemExit(main())
