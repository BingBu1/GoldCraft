"""Verify the real Minecraft HUD and inventory through the CS bridge on a ReHLDS dedicated server."""
import importlib.util
import json
import re
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location('renderer_test',Path(__file__).with_name('Exercise-Renderer.py'))
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
ROOT=renderer.ROOT


def main():
    a=renderer.Sandbox('cs-client-a');b=renderer.Sandbox('cs-client-b')
    prefix=ROOT/'analysis/goldcraft-tests'/f'hud-{int(time.time())}'
    report={'commands':b.commands,'checks':{},'captures':{},'scope':'Real vanilla HUD/menu pixels and native UI protocol; actual mouse hardware is a separate check.'}
    original={};cursor_slot=None

    def snapshot():
        return {'A':a.status(),'B':b.status(),'minecraftB':renderer.read_json(b.logs/'minecraft-client-status.json'),
                'server':renderer.read_json(renderer.SERVER_STATUS)}
    def wait_for(predicate,timeout=10):
        deadline=time.monotonic()+timeout;last=None
        while time.monotonic()<deadline:
            last=snapshot()
            if predicate(last):return last
            time.sleep(.08)
        report['timeoutState']=last;raise TimeoutError('HUD transition did not complete')
    def capture(name):
        report['captures'][name]=b.capture(prefix.with_name(prefix.name+'-'+name+'.bmp'))
    def player_value(name):
        response=b.minecraft(f'data get entity {b.player} {name}')
        value=response.split('following entity data: ',1)[-1]
        return float(value.strip().rstrip('fbds'))
    def click(slot):
        data=snapshot()['B'];b.host(f"ui_move {slot['x']/data['hudWidth']:.7f} {slot['y']/data['hudHeight']:.7f}")
        b.host('ui_click 0')
    def item(state,slot_id):return next(x for x in state['minecraftB'].get('menuSlots',[]) if x['id']==slot_id)

    try:
        before=wait_for(lambda s:s['A']['hudVisible'] and s['B']['hudVisible'] and s['B']['hudDraws']>5,20)
        report['before']=before
        report['checks']['dedicated_server_owns_both_pairs']=before['server']['dedicated'] and sum(p['controlled'] for p in before['server']['actors'])==2
        report['checks']['independent_HUD_in_both_CS_clients']=before['A']['hudFrames']>0 and before['B']['hudFrames']>0
        original['slot']=before['B']['hudSlot'];capture('initial')
        chosen=(original['slot']+1)%9;began=time.monotonic();b.host(f'slot {chosen+1}')
        selected=wait_for(lambda s:s['B']['hudSlot']==chosen and s['minecraftB']['selectedSlot']==chosen)
        report['slotChange']={'seconds':time.monotonic()-began,'state':selected};capture('selected')
        report['checks']['CS_hotbar_selection_reaches_vanilla_HUD']=selected['A']['hudSlot']==before['A']['hudSlot']
        b.host(f"slot {original['slot']+1}")
        b.host('hud 0');disabled=wait_for(lambda s:not s['B']['hudVisible']);capture('disabled')
        b.host('hud 1');enabled=wait_for(lambda s:s['B']['hudVisible'] and s['B']['hudFrames']>disabled['B']['hudFrames'])
        report['checks']['HUD_disable_and_restore']=enabled['B']['glError']==0
        previous_viewport=enabled['B']['viewportId']
        b.host('hud_scale 3');resized=wait_for(lambda s:s['B']['hudVisible'] and s['B']['viewportId']>previous_viewport
                                             and s['B']['hudWidth']==427 and s['B']['hudHeight']==240);capture('scale3')
        b.host('hud_scale 0');wait_for(lambda s:s['B']['hudVisible'] and s['B']['viewportId']>resized['B']['viewportId'])
        report['scaleChange']=resized
        report['checks']['GUI_scale_preserves_physical_resolution']=resized['B']['hudTextureWidth']==1280 and resized['B']['hudTextureHeight']==720
        report['checks']['HUD_matches_CS_physical_viewport']=all(before[p]['hudTextureWidth']==before[p]['viewportWidth']
            and before[p]['hudTextureHeight']==before[p]['viewportHeight'] for p in ('A','B'))
        # Flowing water moves an idle player even with the menu open. Use a dry host floor.
        if before['server']['map']=='cs_militia':
            original['position']=b.status()['minecraftFeet']
            original['view']=b.status()['viewAngles']
            b.minecraft(f'execute in {renderer.DIMENSION} run tp {b.player} -11.625 59.001 70.125')
            time.sleep(.7)
        original['mode']=int(player_value('playerGameType'))
        original['levels']=int(re.search(r'(\d+) experience levels',b.minecraft(f'experience query {b.player} levels')).group(1))
        original['points']=int(re.search(r'(\d+) experience points',b.minecraft(f'experience query {b.player} points')).group(1))
        b.minecraft(f'gamemode survival {b.player}')
        b.minecraft(f'experience set {b.player} 7 levels');b.minecraft(f'experience set {b.player} 4 points')
        survival=wait_for(lambda s:s['B']['hudLevel']==7 and s['B']['hudExperience']>0)
        report['survival']=survival;capture('survival')
        report['checks']['health_food_armor_XP_from_live_MC_player']=survival['B']['hudHealth']==player_value('Health') and survival['B']['hudFood']==int(player_value('foodLevel'))
        saved_inventory=b.minecraft(f'data get entity {b.player} Inventory')
        b.host('inventory')
        menu=wait_for(lambda s:s['B']['uiActive'] and s['B']['hudMenuId']>0 and 'InventoryScreen' in s['minecraftB']['screen'] and 'menuSlots' in s['minecraftB'])
        capture('inventory');report['openedMenu']=menu
        source=next((x for x in menu['minecraftB']['menuSlots'] if 9<=x['id']<=44 and x['count']>0),None)
        target=next((x for x in menu['minecraftB']['menuSlots'] if 9<=x['id']<=35 and x['count']==0),None)
        if source is None or target is None:raise RuntimeError('A nonempty inventory slot and an empty destination are needed for the reversible item test')
        cursor_slot=source;click(source)
        picked=wait_for(lambda s:s['minecraftB'].get('cursorCount')==source['count'] and item(s,source['id'])['count']==0)
        click(target);cursor_slot=None
        placed=wait_for(lambda s:s['minecraftB'].get('cursorCount')==0 and item(s,target['id'])['count']==source['count'])
        report['inventoryMove']={'source':source,'destination':target,'picked':picked,'placed':placed,
                                 'serverInventoryAfterMove':b.minecraft(f'data get entity {b.player} Inventory')}
        capture('moved-item')
        cursor_slot=target;click(target);wait_for(lambda s:s['minecraftB'].get('cursorCount')==source['count'])
        cursor_slot=source;click(source);wait_for(lambda s:s['minecraftB'].get('cursorCount')==0 and item(s,source['id'])['count']==source['count']);cursor_slot=None
        report['checks']['CS_inventory_pick_place_and_restore']=saved_inventory==b.minecraft(f'data get entity {b.player} Inventory')
        fixed=b.status();b.host('forward 0.25');time.sleep(.6);after=b.status()
        report['menuMovement']={'before':fixed,'after':after,'minecraft':renderer.read_json(b.logs/'minecraft-client-status.json')}
        report['checks']['inventory_blocks_world_movement']=all(abs(x-y)<.003 for x,y in zip(fixed['minecraftFeet'],after['minecraftFeet']))
        b.host('ui_key 256 0');closed=wait_for(lambda s:not s['B']['minecraftMenu'] and not s['B']['uiActive'])
        report['checks']['CS_GUI_escape_closes_inventory']=closed['minecraftB']['screen']==''
        b.minecraft(f"experience set {b.player} {original['levels']} levels");b.minecraft(f"experience set {b.player} {original['points']} points")
        b.minecraft(f"gamemode {['survival','creative','adventure','spectator'][original['mode']]} {b.player}")
        original.pop('mode');original.pop('levels');original.pop('points')
        pre=snapshot();b.host('resource_reload')
        reload=wait_for(lambda s:s['B']['hudVisible'] and s['B']['hudRevision']>pre['B']['hudRevision']+8
                        and s['B']['atlasGeneration']>pre['B']['atlasGeneration'] and s['minecraftB']['overlay']=='',30)
        report['reload']=reload;capture('resource-reload')
        report['checks']['HUD_recovers_after_real_resource_reload']=reload['B']['glError']==0 and reload['minecraftB']['hudGlError']==0 and reload['minecraftB']['hudError']==''
        report['after']=snapshot()
        report['checks']['A_UI_not_affected_by_B_menu']=all(s['A']['minecraftMenu']==before['A']['minecraftMenu']
            and s['A']['hudMenuId']==before['A']['hudMenuId'] for s in (menu,report['after'])) and report['after']['A']['hudVisible']
        report['outcome']='passed' if all(report['checks'].values()) else 'failed'
    except Exception as error:
        report['error']=str(error);report['outcome']='incomplete'
    finally:
        try:
            if cursor_slot is not None and b.status()['minecraftMenu']:click(cursor_slot)
            b.host('menu_close');b.host('hud 1');b.host('hud_scale 0')
            if 'slot' in original:b.host(f"slot {original['slot']+1}")
            if 'levels' in original:b.minecraft(f"experience set {b.player} {original['levels']} levels")
            if 'points' in original:b.minecraft(f"experience set {b.player} {original['points']} points")
            if 'mode' in original:b.minecraft(f"gamemode {['survival','creative','adventure','spectator'][original['mode']]} {b.player}")
            if 'position' in original:
                b.minecraft(f"execute in {renderer.DIMENSION} run tp {b.player} {' '.join(map(str,original['position']))}")
                b.host(f"view {original['view'][1]} {original['view'][0]}")
        except Exception as error:report['cleanupError']=str(error)
        a.connection.close();b.connection.close()
        prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report['outcome'],'checks':report['checks'],'error':report.get('error')},indent=2))


if __name__=='__main__':main()
