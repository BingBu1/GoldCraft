"""Exercise map-session cleanup on the real ReHLDS/Fabric pair, including unloaded chunks."""
import argparse
import importlib.util
import json
import time
from pathlib import Path


def module(name,file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


r=module('renderer_reset','Exercise-Renderer.py')
cs=module('cs_rcon_reset','GoldSrc-Command.py')
DIM='goldcraft:cs_assault_f6725c06'
FAR=(400,120,400)
RESTART_STATE=r.ROOT/'analysis/goldcraft-tests/world-reset-restart-pending.json'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase',choices=('switch','prepare-restart','verify-restart'),default='switch')
    args=parser.parse_args()
    a=r.Sandbox('cs-client-a');b=r.Sandbox('cs-client-b')
    prefix=r.ROOT/'analysis/goldcraft-tests'/f'world-reset-{args.phase}-{int(time.time())}'
    report={'phase':args.phase,'checks':{},'commands':b.commands,'states':{}}

    def snapshot():return {'server':r.read_json(r.SERVER_STATUS),'A':a.status(),'B':b.status()}
    def wait(predicate,seconds=30):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            result=predicate()
            if result:return result
            time.sleep(.15)
        raise TimeoutError('World lifecycle condition did not complete')
    def ready(map_name,old_epoch=None):
        def check():
            s=snapshot();epoch=s['server']['world']
            if s['server']['map']!=map_name or epoch==old_epoch:return None
            if not s['server']['dedicated']:return None
            for name in ('A','B'):
                if s[name]['world']!=epoch or not s[name]['minecraftControl'] or not s[name]['hudVisible'] or s[name]['minecraftFeet'][1]<0:return None
            return s
        return wait(check,40)
    def mc(command):return b.minecraft(f'execute in {DIM} run {command}')
    def is_block(position,block):return 'Test passed' in mc(f"execute if block {' '.join(map(str,position))} minecraft:{block}")
    def loaded():return 'Test passed' in mc(f"execute if loaded {' '.join(map(str,FAR))}")
    def far_load():
        mc('forceload add 400 400');wait(loaded,10)
    def far_unload():
        mc('forceload remove 400 400');wait(lambda:not loaded(),40)
    def fixtures():
        far_load()
        mc('setblock 400 120 400 minecraft:diamond_block')
        mc('setblock 401 120 400 minecraft:chest')
        mc('item replace block 401 120 400 container.0 with minecraft:emerald 19')
        mc('fill 403 120 399 405 122 401 minecraft:glass')
        mc('setblock 404 121 400 minecraft:water')
        # A near block is also loaded by both gameplay clients.
        mc('forceload add 0 0');mc('setblock 0 120 0 minecraft:gold_block')
    def cleared():
        return {'near_block':is_block((0,120,0),'air'),'far_block':is_block(FAR,'air'),
                'chest':is_block((401,120,400),'air'),'fluid':is_block((404,121,400),'air'),
                'container_dropped_no_items':'Test failed' in mc('execute if entity @e[type=minecraft:item,x=400,y=120,z=400,distance=..8]')}
    try:
        initial=ready('cs_assault');report['states']['before']=initial
        if args.phase=='verify-restart':
            saved=r.read_json(RESTART_STATE)
            report['prepared']=saved
            report['checks']['new_host_epoch']=initial['server']['world']!=saved['world']
            far_load();report['checks'].update(cleared())
        else:
            fixtures();report['checks']['fixtures_created']=is_block(FAR,'diamond_block') and is_block((401,120,400),'chest') and is_block((404,121,400),'water')
            if args.phase=='prepare-restart':
                far_unload()
                RESTART_STATE.write_text(json.dumps({'world':initial['server']['world'],'report':str(prefix.with_suffix('.json')),'preparedAt':time.time()},indent=2))
            else:
                far_unload();far_load()
                report['checks']['same_session_chunk_reload_keeps_blocks']=is_block(FAR,'diamond_block') and is_block((401,120,400),'chest')
                old_serial=b.status()['playerSerial'];b.host('disconnect')
                wait(lambda:not b.status()['minecraftControl'],5);b.host('reconnect')
                connected=ready('cs_assault')
                report['states']['clientReconnect']=connected
                report['checks']['client_reconnect_keeps_blocks']=connected['B']['playerSerial']!=old_serial and is_block(FAR,'diamond_block')
                far_unload()
                report['checks']['far_chunk_unloaded_before_map_change']=not loaded()
                print('Unloaded chunk and client reconnect checks completed',flush=True)
                cs.command('changelevel cs_militia')
                other=ready('cs_militia',initial['server']['world']);report['states']['otherMap']=other
                cs.command('changelevel cs_assault')
                returned=ready('cs_assault',other['server']['world']);report['states']['returned']=returned
                report['checks']['map_changes_publish_new_epochs']=returned['server']['world']!=initial['server']['world']
                far_load();report['checks'].update(cleared())
                time.sleep(1.5);report['checks']['fluid_does_not_return_from_saved_ticks']=is_block((404,121,400),'air')
                report['capture']=b.capture(prefix.with_suffix('.bmp'))
                mc('setblock 0 120 0 minecraft:gold_block')
                cs.command('changelevel cs_assault')
                same=ready('cs_assault',returned['server']['world']);report['states']['sameMapRestart']=same
                report['checks']['same_map_reload_clears_blocks']=is_block((0,120,0),'air')
        report['states']['after']=snapshot()
        report['outcome']='passed' if all(report['checks'].values()) else 'failed'
    except Exception as error:
        report['error']=str(error);report['outcome']='incomplete';report['states']['failure']=snapshot()
    finally:
        try:
            if args.phase!='prepare-restart':mc('forceload remove 400 400');mc('forceload remove 0 0')
        except Exception as error:report['cleanupError']=str(error)
        a.connection.close();b.connection.close()
        prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report['outcome'],'checks':report['checks'],'error':report.get('error')},indent=2))
    return report['outcome']=='passed'


if __name__=='__main__':raise SystemExit(0 if main() else 1)
