"""Exercise vanilla hands/items through sandbox B's real CS input and final framebuffer."""
import importlib.util
import json
import math
import re
import time
from pathlib import Path


def module(name, file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


r=module('hands_renderer','Exercise-Renderer.py')
cs=module('hands_cs','GoldSrc-Command.py')
DIM='goldcraft:cs_assault_f6725c06'


def main():
    b=r.Sandbox('cs-client-b')
    prefix=r.ROOT/'analysis/goldcraft-tests'/f'first-person-{int(time.time())}'
    report={'scope':__doc__,'checks':{},'captures':{},'states':{},'samples':[],
            'commands':b.commands,'nativeCommands':[],'visualReviewRequired':True}
    original={};backup=False;forcerespawn=None
    tag='goldcraft_hand_backup_'+str(int(time.time()))
    stand=f'@e[type=minecraft:armor_stand,tag={tag},limit=1]'

    def save():prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    def sample():
        state={'time':time.monotonic(),'native':b.status(),'minecraft':r.read_json(b.logs/'minecraft-client-status.json')}
        report['samples'].append(state);return state
    def wait(predicate,seconds=12):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            state=sample()
            if predicate(state):return state
            time.sleep(.04)
        raise TimeoutError('First-person runtime condition did not complete')
    def ready(s):return s['native'].get('minecraftHands') and s['native']['viewModelSuppressed'] and s['minecraft']['hudError']==''
    def check(name,condition):
        report['checks'][name]=bool(condition);save()
        if not condition:raise AssertionError(name)
    def mc(command):return b.minecraft(f'execute in {DIM} run {command}')
    def native(command):
        reply=cs.command(command);report['nativeCommands'].append({'command':command,'response':reply});return reply
    def item(name):
        mc(f'item replace entity {b.player} hotbar.{original["slot"]} with minecraft:{name}')
        state=wait(lambda s:ready(s) and s['minecraft']['selectedItem']=='minecraft:'+name)
        wait(lambda s:s['native']['hudFrames']>state['native']['hudFrames']+15)
        # Vanilla lowers a newly selected tool until its attack/equip cooldown recovers.
        # Counters alone can acknowledge the item before it has risen into view.
        time.sleep(.85)
    def capture(name):
        result=b.capture(prefix.with_name(prefix.name+'-'+name+'.bmp'))
        report['captures'][name]=result;save();return result

    try:
        initial=wait(ready,40);report['states']['initial']=initial
        original={'slot':initial['native']['hudSlot'],'position':initial['minecraft']['position'],
                  'view':initial['native']['viewAngles'],'mainEmpty':initial['minecraft']['selectedItem']=='minecraft:air',
                  'offEmpty':initial['minecraft']['offhandItem']=='minecraft:air'}
        reply=b.minecraft(f'data get entity {b.player} playerGameType')
        original['mode']=int(re.search(r'following entity data: (\d+)',reply)[1])
        xyz=' '.join(map(str,original['position']))
        reply=mc(f'summon minecraft:armor_stand {xyz} {{Tags:["{tag}"],Invisible:1b,Marker:1b,NoGravity:1b,Invulnerable:1b}}')
        if 'Summoned' not in reply:raise RuntimeError(reply)
        backup=True
        if not original['mainEmpty']:mc(f'item replace entity {stand} weapon.mainhand from entity {b.player} hotbar.{original["slot"]}')
        if not original['offEmpty']:mc(f'item replace entity {stand} weapon.offhand from entity {b.player} weapon.offhand')
        b.host('menu_close');b.host('hideconsole')
        b.minecraft(f'gamemode creative {b.player}')
        mc(f'tp {b.player} 12.5 64.0009765625 -5.5 -90 0');b.host('view 0 0')
        wait(lambda s:ready(s) and math.dist(s['minecraft']['position'],[12.5,64,-5.5])<.04)
        mc(f'item replace entity {b.player} weapon.offhand with minecraft:air')
        item('air');capture('empty')
        check('fresh_MC_first_person_suppresses_CS_weapon',b.status()['hostViewModel']==0)
        for name in ('stone','diamond_pickaxe'):
            item(name);capture(name)
            check('native_frame_with_'+name,sample()['minecraft']['handVisible'])
        selected=(original['slot']+1)%9;b.host(f'slot {selected+1}')
        wait(lambda s:s['native']['hudSlot']==selected and s['minecraft']['selectedSlot']==selected)
        b.host(f'slot {original["slot"]+1}');wait(lambda s:s['minecraft']['selectedSlot']==original['slot'])
        check('CS_hotbar_switch_updates_equipped_hand',True)

        before=sample();b.host('attack 0.65')
        swung=wait(lambda s:s['minecraft']['swinging'] or s['minecraft']['swingProgress']>0,4)
        report['states']['swing']=swung;capture('swing')
        wait(lambda s:not s['minecraft']['swinging'] and s['native']['inputButtons']==0)
        capture('swing-ended');check('CS_attack_drives_vanilla_swing',swung['native']['hudFrames']>before['native']['hudFrames'])
        item('diamond_sword');mc(f'item replace entity {b.player} weapon.offhand with minecraft:shield')
        wait(lambda s:s['minecraft']['offhandItem']=='minecraft:shield');time.sleep(.45);capture('offhand-shield')
        b.host('attack2 1.4');used=wait(lambda s:s['minecraft']['usingItem'],3)
        report['states']['shieldUse']=used;capture('shield-blocking')
        wait(lambda s:not s['minecraft']['usingItem'] and s['native']['inputButtons']==0)
        check('CS_right_click_drives_offhand_use',used['minecraft']['itemUseTime']>0)
        mc(f'item replace entity {b.player} weapon.offhand with minecraft:air')
        item('golden_apple');b.host('attack2 1.0')
        eaten=wait(lambda s:s['minecraft']['usingItem'],3);report['states']['eating']=eaten;capture('eating')
        time.sleep(.2);capture('eating-later')
        wait(lambda s:not s['minecraft']['usingItem'] and s['native']['inputButtons']==0)
        check('vanilla_eating_action_reaches_CS_frames',eaten['native']['hostViewModel']==0)

        b.host('inventory');menu=wait(lambda s:s['native']['uiActive'] and 'InventoryScreen' in s['minecraft']['screen'])
        report['states']['inventory']=menu;capture('inventory')
        b.host('menu_close');wait(lambda s:not s['native']['minecraftMenu'] and not s['native']['uiActive'])
        check('menu_composites_over_the_MC_view_without_CS_gun',menu['native']['hostViewModel']==0)
        b.host('hud 0');fallback=wait(lambda s:not s['native']['viewModelSuppressed'])
        check('disabled_MC_compositor_restores_CS_viewmodel',fallback['native']['hostViewModel']==1)
        b.host('hud 1');wait(ready)
        check('reenabled_MC_compositor_reacquires_viewmodel',b.status()['hostViewModel']==0)
        pre=sample();b.host('resource_reload')
        reloaded=wait(lambda s:ready(s) and s['native']['atlasGeneration']>pre['native']['atlasGeneration']
                      and s['minecraft']['overlay']=='' and s['native']['hudFrames']>pre['native']['hudFrames']+10,35)
        report['states']['reload']=reloaded;capture('reloaded')
        check('hand_HUD_and_particles_survive_resource_reload',reloaded['native']['glError']==0 and reloaded['minecraft']['hudGlError']==0
              and reloaded['minecraft'].get('particleError','')=='')

        reply=native('mp_forcerespawn');forcerespawn=re.search(r'is "([^"]+)"',reply)[1];native('mp_forcerespawn 1')
        before=sample();b.host('kill')
        dead=wait(lambda s:not s['native']['minecraftControl'],8);report['states']['dead']=dead
        spawned=wait(lambda s:ready(s) and s['native']['playerLife']>before['native']['playerLife'],15)
        report['states']['respawn']=spawned
        check('death_releases_first_person_override',not dead['native']['viewModelSuppressed'])
        check('respawn_requires_current_life_frame',spawned['native']['minecraftHands'] and spawned['minecraft']['life']==spawned['native']['playerLife'])
        before=sample();b.host('disconnect');wait(lambda s:not s['native']['minecraftControl'],8)
        b.host('reconnect');joined=wait(lambda s:ready(s) and s['native']['playerSerial']>before['native']['playerSerial'],35)
        report['states']['reconnect']=joined;check('reconnect_restores_first_person_without_stale_CS_gun',joined['native']['hostViewModel']==0)

        start=sample();time.sleep(3);end=sample();duration=end['time']-start['time']
        report['delivery']={key+'PerSecond':(end['native'][key]-start['native'][key])/duration for key in ('hudFrames','hudDraws','viewModelSuppressedFrames')}
        check('presentation_updates_above_simulation_tick_rate',report['delivery']['hudFramesPerSecond']>25)
        check('no_GL_errors_in_live_first_person',all(s['native']['glError']==0 and s['minecraft']['hudGlError']==0 for s in report['samples']))
        report['outcome']='passed'
    except Exception as error:
        report['outcome']='incomplete';report['error']=str(error)
        try:report['states']['failure']=sample()
        except Exception:pass
    finally:
        try:
            b.host('menu_close');b.host('hud 1')
            if backup:
                main='with minecraft:air' if original['mainEmpty'] else f'from entity {stand} weapon.mainhand'
                off='with minecraft:air' if original['offEmpty'] else f'from entity {stand} weapon.offhand'
                mc(f'item replace entity {b.player} hotbar.{original["slot"]} {main}')
                mc(f'item replace entity {b.player} weapon.offhand {off}')
                mc(f'item replace entity {stand} weapon.mainhand with minecraft:air');mc(f'item replace entity {stand} weapon.offhand with minecraft:air')
                mc(f'kill {stand}')
            if original:
                b.host(f'slot {original["slot"]+1}')
                mc(f'tp {b.player} '+ ' '.join(map(str,original['position'])))
                b.host(f'view {original["view"][1]} {original["view"][0]}')
                if 'mode' in original:b.minecraft(f'gamemode {["survival","creative","adventure","spectator"][original["mode"]]} {b.player}')
            if forcerespawn is not None:native('mp_forcerespawn '+forcerespawn)
        except Exception as error:report['cleanupError']=str(error)
        b.connection.close();save()
    print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report['outcome'],'checks':report['checks'],
                      'error':report.get('error'),'delivery':report.get('delivery')},indent=2))
    return report['outcome']=='passed'


if __name__=='__main__':raise SystemExit(0 if main() else 1)
