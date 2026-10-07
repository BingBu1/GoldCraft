"""Exercise vanilla particle export, host rendering and server delivery on the two isolated cs_assault pairs."""
import importlib.util
import json
import math
import re
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location('particle_renderer',Path(__file__).with_name('Exercise-Renderer.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
DIM='goldcraft:cs_assault_f6725c06'

def main():
    b=r.Sandbox('cs-client-b');a=r.Sandbox('cs-client-a')
    prefix=r.ROOT/'analysis/goldcraft-tests'/f'particles-{int(time.time())}'
    tag='goldcraft_particle_'+str(int(time.time()))
    report={'scope':__doc__,'checks':{},'captures':{},'states':{},'samples':[],
            'commands':b.commands,'visualReviewRequired':True}
    original={};owns_block=False
    log_path=b.logs/'goldcraft-client.log';log_start=log_path.stat().st_size
    native_keys=('world','playerSlot','playerSerial','playerLife','minecraftControl','glError','sceneReady',
                 'atlasGeneration','entityTextures','entityBatches','particleCount','particleVertices','particleFrames',
                 'particleDraws','particleGeneration','particleRevision','particleAgeMs','particleLife','particlesEnabled',
                 'hudFrames','hudDraws','minecraftHands','viewModelSuppressed','minecraftFeet','viewAngles','inputButtons')
    java_keys=('world','life','controlling','position','screen','overlay','particleFrames','particleCount','particleVertices',
               'particleKinds','particleKindFrames','particleUnsupported','particleCulled','particleError','particleGeneration',
               'particleIntervalMs','particleTextureUploads','hudError','hudGlError','presentationOnly','minecraftFps')

    def save():prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    def sample():
        state={'time':time.monotonic()}
        for key,game in [('b',b),('a',a)]:
            native=game.status();java=r.read_json(game.logs/'minecraft-client-status.json')
            state[key]={'native':{k:native.get(k) for k in native_keys},'minecraft':{k:java.get(k) for k in java_keys}}
        report['samples'].append(state);return state
    def ready(s):
        n=s['b']['native'];j=s['b']['minecraft']
        return n['minecraftControl'] and n['sceneReady'] and j['controlling'] and j['particleError']=='' and n['particleLife']==n['playerLife']
    def wait(predicate,seconds=12):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            state=sample()
            if predicate(state):return state
            time.sleep(.04)
        raise TimeoutError('Particle runtime condition timed out')
    def check(name,value):
        report['checks'][name]=bool(value);save()
        if not value:raise AssertionError(name)
    def mc(command):
        response=b.minecraft(f'execute in {DIM} run {command}')
        if any(text in response for text in ('Incorrect argument','Unknown or incomplete','Expected ')):raise RuntimeError(response)
        return response
    def capture(name):
        report['captures'][name]=b.capture(prefix.with_name(prefix.name+'-'+name+'.bmp'));save()
    def seen(state,kind,instance='b'):return (state[instance]['minecraft']['particleKindFrames'] or {}).get(kind,0)
    def present(state,kind,instance='b'):return (state[instance]['minecraft']['particleKinds'] or {}).get(kind,0)
    def particle(effect,kind,where='16.5 65.5 -5.5',count=45,spread='.2 .2 .2',speed='.005',viewers='GoldCraft_B'):
        before=sample()
        response=mc(f'particle {effect} {where} {spread} {speed} {count} force {viewers}')
        if 'particle' not in response.lower():raise RuntimeError(response)
        after=wait(lambda s:seen(s,kind)>seen(before,kind) and s['b']['native']['particleFrames']>before['b']['native']['particleFrames'])
        report['states'][kind]={'before':before,'after':after,'effect':effect};return before,after

    try:
        initial=wait(ready,40);report['states']['initial']=initial
        original={'position':initial['b']['minecraft']['position'],'view':initial['b']['native']['viewAngles']}
        mode=b.minecraft(f'data get entity {b.player} playerGameType')
        original['mode']=int(re.search(r'following entity data: (\d+)',mode)[1])
        b.host('menu_close');b.host('hideconsole');b.host('particles 1')
        b.minecraft(f'gamemode creative {b.player}')
        mc(f'tp {b.player} 12.5 64.0009765625 -5.5 -90 0');b.host('view 0 0')
        wait(lambda s:ready(s) and math.dist(s['b']['minecraft']['position'],[12.5,64,-5.5])<.04)
        capture('before')

        for effect,kind,label in [('large_smoke','LargeFireSmokeParticle','smoke'),('flame','FlameParticle','flame'),
                                  ('crit','DamageParticle','crit'),('explosion','ExplosionLargeParticle','explosion')]:
            before,after=particle('minecraft:'+effect,kind,count=8 if effect=='explosion' else 60)
            capture(label+'-early');time.sleep(.2);capture(label+'-later')
            check('vanilla_'+label+'_exports_and_draws',after['b']['native']['glError']==0 and after['b']['minecraft']['particleUnsupported']==0)
            if effect=='large_smoke':
                start=sample();time.sleep(.55);end=sample();elapsed=end['time']-start['time']
                report['particleAnimationRate']=(end['b']['native']['particleFrames']-start['b']['native']['particleFrames'])/elapsed
                check('particle_geometry_updates_above_20Hz',report['particleAnimationRate']>25)
        wait(lambda s:present(s,'ExplosionLargeParticle')==0,8)
        check('expired_explosion_geometry_is_removed',True)

        empty=mc('execute if block 15 65 -6 minecraft:air run data get entity GoldCraft_B Pos')
        if 'entity data' not in empty:raise RuntimeError('Block-debris fixture is not empty')
        response=mc('setblock 15 65 -6 minecraft:lime_concrete keep')
        owns_block='Changed the block' in response
        if not owns_block:raise RuntimeError(response)
        b.host('view 0 2.3');time.sleep(.8);capture('block-before')
        before=sample();b.host('attack 0.35')
        broken=wait(lambda s:seen(s,'BlockDustParticle')>seen(before,'BlockDustParticle'))
        capture('block-debris')
        reply=mc('execute if block 15 65 -6 minecraft:air run data get entity GoldCraft_B Pos')
        check('actual_CS_break_creates_terrain_atlas_debris','entity data' in reply and broken['b']['minecraft']['particleUnsupported']==0)
        b.host('view 0 0');wait(lambda s:present(s,'BlockDustParticle')==0,8)

        before=sample()
        # Stagger genuine pickup packets so the 3-tick custom geometry can also be photographed.
        for index in range(6):
            mc(f'summon minecraft:item 12.9 64.4 -5.5 {{Tags:["{tag}"],NoGravity:1b,PickupDelay:{10+index*3}s,Item:{{id:"minecraft:amethyst_shard",count:1,components:{{"minecraft:custom_data":{{goldcraft_particle_fixture:"{tag}"}}}}}}}}')
        picked=wait(lambda s:seen(s,'ItemPickupParticle')>seen(before,'ItemPickupParticle'))
        capture('item-pickup')
        check('real_item_pickup_custom_renderer_exports',picked['b']['minecraft']['particleUnsupported']==0)
        particle('minecraft:elder_guardian','ElderGuardianAppearanceParticle',where='14.0 65.0 -5.5',count=1,spread='0 0 0',speed='0')
        capture('elder-guardian-early');time.sleep(.25);capture('elder-guardian-later')
        check('guardian_custom_renderer_exports',sample()['b']['minecraft']['particleUnsupported']==0)
        wait(lambda s:present(s,'ElderGuardianAppearanceParticle')==0,6)

        dust='minecraft:dust{color:[1.0,0.0,1.0],scale:2.0}'
        particle(dust,'RedDustParticle',count=60,spread='.03 .03 .03',speed='0');capture('depth-in-front')
        wait(lambda s:present(s,'RedDustParticle')==0,12)
        # BSP hull0 at this ray becomes solid at MC x=26.0 (GS x=832).
        particle(dust,'RedDustParticle',where='28.0 65.5 -5.5',count=60,spread='.03 .03 .03',speed='0')
        capture('depth-behind-host-wall')
        report['depthFixture']={'eye':[12.5,65.62,-5.5],'solidBeginsAtX':26.0,'frontX':16.5,'hiddenX':28.0,
                                'source':'cs_assault.bsp model0 render nodes; visible-depth result requires inspecting captures'}
        wait(lambda s:present(s,'RedDustParticle')==0,12);capture('depth-expired')
        check('individual_particle_lifetime_clears_without_clearing_ambient_particles',sample()['b']['native']['particleCount']>0)

        before,after=particle('minecraft:end_rod','EndRodParticle',count=25)
        time.sleep(.4);after=sample()
        check('B_only_server_event_does_not_leak_to_A',seen(after,'EndRodParticle','a')==seen(before,'EndRodParticle','a'))
        before=sample()
        if not before['a']['native']['minecraftControl']:raise RuntimeError('A is not paired for the broadcast test')
        ap=before['a']['minecraft']['position'];bp=before['b']['minecraft']['position']
        midpoint=[(x+y)/2 for x,y in zip(ap,bp)];midpoint[1]+=1
        if math.dist(ap,bp)>120:raise RuntimeError('Clients are beyond the particle export radius for a shared event')
        particle('minecraft:sonic_boom','SonicBoomParticle',where=' '.join(map(str,midpoint)),count=1,spread='0 0 0',speed='0',viewers='@a')
        shared=wait(lambda s:seen(s,'SonicBoomParticle','a')>seen(before,'SonicBoomParticle','a'))
        report['states']['sharedServerEvent']={'before':before,'after':shared,'position':midpoint}
        check('server_broadcast_event_reaches_both_isolated_renderers',seen(shared,'SonicBoomParticle')>seen(before,'SonicBoomParticle'))

        b.host('particles 0');disabled=wait(lambda s:not s['b']['native']['particlesEnabled']);time.sleep(.3)
        start=sample();time.sleep(.4);end=sample();capture('particles-disabled')
        check('particle_toggle_stops_native_draws_while_stream_continues',end['b']['native']['particleDraws']==start['b']['native']['particleDraws']
              and end['b']['native']['particleFrames']>start['b']['native']['particleFrames'])
        b.host('particles 1');wait(lambda s:s['b']['native']['particlesEnabled']);capture('particles-enabled')
        before=sample();b.host('resource_reload')
        reloaded=wait(lambda s:ready(s) and s['b']['minecraft']['overlay']=='' and s['b']['native']['atlasGeneration']>before['b']['native']['atlasGeneration']
                      and s['b']['native']['particleGeneration']>before['b']['native']['particleGeneration'],40)
        particle('minecraft:flame','FlameParticle');capture('after-resource-reload')
        check('particle_textures_and_hands_recover_after_resource_reload',reloaded['b']['native']['minecraftHands'])
        before=sample();b.host('scene_resync')
        wait(lambda s:ready(s) and s['b']['native']['atlasGeneration']>before['b']['native']['atlasGeneration'])
        check('native_scene_replay_recovers_particles',True)
        check('zero_GL_and_particle_export_errors',all(s['b']['native']['glError']==0 and s['b']['minecraft']['particleError']==''
              and s['b']['minecraft']['hudError']=='' for s in report['samples']))
        with log_path.open('rb') as log:log.seek(log_start);tail=log.read().decode(errors='replace')
        report['textureErrorLines']=[line for line in tail.splitlines() if 'Missing ' in line and 'texture' in line]
        check('no_missing_texture_errors_during_replay',not report['textureErrorLines'])
        report['outcome']='passed'
    except Exception as error:
        report['outcome']='incomplete';report['error']=str(error)
        try:report['states']['failure']=sample()
        except Exception:pass
    finally:
        try:
            b.host('particles 1');b.host('menu_close')
            if owns_block:mc('execute if block 15 65 -6 minecraft:lime_concrete run setblock 15 65 -6 minecraft:air')
            mc(f'kill @e[type=minecraft:item,tag={tag}]')
            b.minecraft(f'clear GoldCraft_B minecraft:amethyst_shard[minecraft:custom_data~{{goldcraft_particle_fixture:"{tag}"}}]')
            if original:
                mc(f'tp {b.player} '+' '.join(str(float(x)) for x in original['position']))
                b.host(f'view {original["view"][1]} {original["view"][0]}')
                if 'mode' in original:b.minecraft(f'gamemode {["survival","creative","adventure","spectator"][original["mode"]]} {b.player}')
        except Exception as error:report['cleanupError']=str(error)
        b.connection.close();a.connection.close();save()
    print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report['outcome'],'checks':report['checks'],
                      'error':report.get('error'),'cleanupError':report.get('cleanupError'),'animationRate':report.get('particleAnimationRate')},indent=2))
    return report['outcome']=='passed'

if __name__=='__main__':raise SystemExit(0 if main() else 1)
