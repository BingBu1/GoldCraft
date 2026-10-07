"""Measure real sandbox audio sources/output, focus isolation and spatial listener updates."""
import argparse
import importlib.util
import json
import math
import re
import subprocess
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location('audio_renderer',Path(__file__).with_name('Exercise-Renderer.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase',choices=['focus','spatial','distance','movement','ladder','reload','reconnect'])
    parser.add_argument('--foreground',choices=['a','b'],default='b')
    args=parser.parse_args()
    clients={i:r.Sandbox('cs-client-'+i) for i in ['a','b']}
    active=clients[args.foreground];other='a' if args.foreground=='b' else 'b'
    prefix=r.ROOT/'analysis/goldcraft-tests'/f'audio-{args.phase}-{args.foreground}-{int(time.time())}'
    pids={}
    for key,c in clients.items():
        for role in ['CsClient','MinecraftClient']:
            pids[key+'-'+role]=r.read_json(c.logs.parent/f'process-{role}.json')['pid']
    report={'scope':__doc__,'phase':args.phase,'foreground':args.foreground,'checks':{},'samples':[],
            'meters':{},'pids':pids,'commands':{i:c.commands for i,c in clients.items()},'hostCommands':[]}
    original={}

    def save():prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    def sample():
        state={'time':time.monotonic(),**{i:{'native':c.status(),'minecraft':r.read_json(c.logs/'minecraft-client-status.json')} for i,c in clients.items()}}
        report['samples'].append(state);return state
    def check(name,value):
        report['checks'][name]=bool(value);save()
        if not value:raise AssertionError(name)
    def wait(predicate,seconds=12):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            s=sample()
            if predicate(s):return s
            time.sleep(.05)
        raise TimeoutError('Audio runtime condition did not complete')
    def focused(s):
        return (s[args.foreground]['native'].get('windowFocused') and s[args.foreground]['minecraft'].get('audioEffectiveGain',0)>0
            and s[other]['minecraft'].get('audioEffectiveGain',-1)==0)
    def host(command,client=active):
        report['hostCommands'].append({'client':client.instance,'command':command});client.host(command)
    def meter(name,action,seconds=2):
        process=subprocess.Popen([str(r.ROOT/'build/native-x86/Release/goldcraft_audio_probe.exe'),str(seconds),*map(str,pids.values())],stdout=subprocess.PIPE,text=True)
        try:
            time.sleep(.15);action()
            output=process.communicate(timeout=seconds+5)[0]
            if process.returncode:raise RuntimeError('Audio session probe failed')
            result=json.loads(output);report['meters'][name]=result;save();return result
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=3)
    def output(result,key,role='MinecraftClient'):return result['processes'][str(pids[key+'-'+role])]
    def played(s,event,key=args.foreground):return s[key]['minecraft'].get('audioEvents',{}).get(event,0)
    def sound(event,target=active.player):
        active.minecraft(f'execute at {target} run playsound {event} master {target} ~ ~1 ~ 1 1')
    def stable():
        a=sample();time.sleep(.7);b=sample()
        if not r.same_view(a[args.foreground]['native'],b[args.foreground]['native']):raise RuntimeError('Input/view is changing; fixed fixture was not started')
        return b

    try:
        initial=wait(focused,20);check('foreground_has_MC_audio_and_background_is_muted',focused(initial))
        check('user_master_settings_preserved',all(initial[i]['minecraft']['audioMasterVolume']==.6 for i in clients))
        check('native_sound_volume_enabled',all(initial[i]['native']['hostVolume']>0 for i in clients))
        if args.phase=='focus':
            before=sample()
            result=meter('both-pairs',lambda:[(host('sound_test',c),sound('minecraft:block.note_block.harp',c.player)) for c in clients.values()])
            after=wait(lambda s:all(played(s,'minecraft:block.note_block.harp',i)>played(before,'minecraft:block.note_block.harp',i) for i in clients))
            check('both_MC_instances_allocate_the_sound',all(played(after,'minecraft:block.note_block.harp',i)>played(before,'minecraft:block.note_block.harp',i) for i in clients))
            check('foreground_MC_has_real_output',output(result,args.foreground)['nonzeroSamples']>0)
            check('background_MC_is_silent',output(result,other)['nonzeroSamples']==0)
            check('foreground_CS_has_real_output',output(result,args.foreground,'CsClient')['nonzeroSamples']>0)
            check('background_CS_is_silent',output(result,other,'CsClient')['nonzeroSamples']==0)
            before=sample()
            result=meter('targeted-events',lambda:[sound(e) for e in ['minecraft:block.stone.break','minecraft:block.wood.place','minecraft:item.flintandsteel.use','minecraft:entity.pig.ambient','minecraft:entity.generic.explode']])
            after=wait(lambda s:played(s,'minecraft:entity.generic.explode')>played(before,'minecraft:entity.generic.explode'))
            check('targeted_sound_does_not_reach_other_instance',played(after,'minecraft:entity.generic.explode',other)==played(before,'minecraft:entity.generic.explode',other))
            check('gameplay_events_reach_audio_sources',all(played(after,e)>played(before,e) for e in ['minecraft:block.stone.break','minecraft:block.wood.place','minecraft:item.flintandsteel.use','minecraft:entity.pig.ambient','minecraft:entity.generic.explode']))
            check('gameplay_events_have_real_output',output(result,args.foreground)['nonzeroSamples']>0)
        elif args.phase in ['spatial','distance','movement','ladder']:
            start=stable();state=start[args.foreground];original={'position':state['minecraft']['position'],'view':state['native']['viewAngles'],'hud':state['native']['hudVisible']}
            dim='goldcraft:cs_assault_f6725c06'
            # This fixture only relocates the consenting test client and never edits terrain/inventory.
            active.minecraft(f'execute in {dim} run tp {active.player} 12.5 64.0009765625 -5.5 -90 0');host('view 0 0')
            wait(lambda s:math.dist(s[args.foreground]['minecraft']['position'],[12.5,64,-5.5])<.04)
            if args.phase=='ladder':
                active.minecraft(f'execute in {dim} run tp {active.player} 21.0 64.0009765625 -13.0 -90 0');host('view 0 0')
                wait(lambda s:math.dist(s[args.foreground]['minecraft']['position'],[21,64,-13])<.04)
                before=sample();result=meter('host-ladder-climb',lambda:host('forward 2'),2.1);after=sample()
                check('CS_input_climbed_real_host_ladder',after[args.foreground]['minecraft']['position'][1]>before[args.foreground]['minecraft']['position'][1]+2.5)
                check('host_ladder_allocated_step_audio',played(after,'minecraft:block.ladder.step')>played(before,'minecraft:block.ladder.step'))
                check('host_ladder_has_real_output',output(result,args.foreground)['nonzeroSamples']>0)
            elif args.phase=='distance':
                energies={}
                for label,x in [('near',14.5),('far',24.5)]:
                    active.minecraft(f'stopsound {active.player}')
                    result=meter(label,lambda x=x:active.minecraft(f'execute in {dim} run playsound minecraft:block.note_block.harp master {active.player} {x} 65.62 -5.5 1 1'),1.2)
                    energies[label]=sum(output(result,args.foreground)['channelEnergy'])
                check('distance_attenuation_is_measured_at_output',energies['near']>energies['far']*3 and energies['far']>0.00001)
            elif args.phase=='movement':
                before=sample();result=meter('host-floor-walk',lambda:host('forward 1.5'),2.1);after=sample()
                check('CS_walk_moved_on_host_floor',math.dist(before[args.foreground]['minecraft']['position'],after[args.foreground]['minecraft']['position'])>2)
                check('host_floor_steps_played',after[args.foreground]['minecraft']['hostFootsteps']>before[args.foreground]['minecraft']['hostFootsteps'])
                check('footstep_has_real_output',output(result,args.foreground)['nonzeroSamples']>0)
                before=sample();time.sleep(1);after=sample()
                check('stationary_does_not_emit_host_steps',after[args.foreground]['minecraft']['hostFootsteps']==before[args.foreground]['minecraft']['hostFootsteps'])
                before=sample();host('forward_duck .8');time.sleep(1.2);after=sample()
                check('crouch_does_not_emit_host_steps',after[args.foreground]['minecraft']['hostFootsteps']==before[args.foreground]['minecraft']['hostFootsteps'])
            else:
                for yaw,pitch in [(0,0),(90,0),(180,25)]:
                    host(f'view {yaw} {pitch}');time.sleep(.45);s=sample()[args.foreground]['minecraft']
                    expected=[math.cos(math.radians(yaw))*math.cos(math.radians(pitch)),-math.sin(math.radians(pitch)),-math.sin(math.radians(yaw))*math.cos(math.radians(pitch))]
                    check('listener_direction_'+str(yaw),math.dist(s['audioForward'],expected)<.04)
                    check('listener_position_'+str(yaw),math.dist(s['audioListener'],[s['position'][0],s['position'][1]+1.62,s['position'][2]])<.05)
                host('view 0 0');time.sleep(.3)
                for label,z in [('left',-9.5),('right',-1.5)]:
                    active.minecraft(f'stopsound {active.player}')
                    result=meter(label,lambda z=z:active.minecraft(f'execute in {dim} run playsound minecraft:block.note_block.harp master {active.player} 12.5 65.62 {z} 1 1'),1.2)
                    levels=output(result,args.foreground)['channelEnergy']
                    # Ambient random sounds can obscure panning. Keep raw channel evidence;
                    # acceptance requires the intended channel to dominate this short fixture.
                    check('spatial_'+label+'_channel_dominates',len(levels)>=2 and levels[0 if label=='left' else 1]>levels[1 if label=='left' else 0]*1.3)
                host('hud 0');host('view 45 15');host('forward .8');time.sleep(1.2)
                s=sample()[args.foreground]['minecraft'];expected=[math.cos(math.radians(45))*math.cos(math.radians(15)),-math.sin(math.radians(15)),-math.sin(math.radians(45))*math.cos(math.radians(15))]
                check('HUD_disabled_listener_still_moves_and_rotates',math.dist(s['audioForward'],expected)<.04 and math.dist(s['audioListener'],[s['position'][0],s['position'][1]+1.62,s['position'][2]])<.12)
        elif args.phase=='reload':
            before=sample();host('resource_reload')
            wait(lambda s:focused(s) and s[args.foreground]['minecraft']['overlay']=='' and s[args.foreground]['native']['atlasGeneration']>before[args.foreground]['native']['atlasGeneration'],40)
            result=meter('after-reload',lambda:sound('minecraft:block.note_block.harp'))
            after=sample();check('resource_reload_restores_real_audio',output(result,args.foreground)['nonzeroSamples']>0 and played(after,'minecraft:block.note_block.harp')>played(before,'minecraft:block.note_block.harp'))
        else:
            host('disconnect')
            state=wait(lambda s:s[args.foreground]['minecraft']['audioEffectiveGain']==0)
            check('native_disconnect_expires_audio_focus',state[args.foreground]['minecraft']['audioEffectiveGain']==0)
            host('reconnect');wait(lambda s:focused(s) and s[args.foreground]['minecraft']['controlling'],40)
            result=meter('after-reconnect',lambda:sound('minecraft:block.note_block.harp'))
            check('reconnect_restores_real_audio',output(result,args.foreground)['nonzeroSamples']>0)
        final=sample()
        check('hands_particles_and_GL_remain_healthy',all(final[i]['native']['glError']==0 and final[i]['minecraft']['hudError']=='' and final[i]['minecraft']['particleError']=='' for i in clients))
        report['outcome']='passed'
    except Exception as error:
        report['error']=repr(error);report['outcome']='incomplete';raise
    finally:
        if original:
            try:
                if original['hud']:host('hud 1')
                p=original['position'];a=original['view'];active.minecraft(f'execute in goldcraft:cs_assault_f6725c06 run tp {active.player} {p[0]} {p[1]} {p[2]} {-a[1]-90} {a[0]}');host(f'view {a[1]} {a[0]}')
            except Exception as error:report['cleanupError']=repr(error)
        for client in clients.values():client.connection.close()
        save()
        print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report.get('outcome'),'checks':report['checks'],'error':report.get('error'),'cleanupError':report.get('cleanupError')},indent=2))


if __name__=='__main__':main()
