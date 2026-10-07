"""Exercise player clip and host ladders using real CS input on the cs_assault sandbox."""
import argparse
import importlib.util
import json
import re
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location('renderer_test',Path(__file__).with_name('Exercise-Renderer.py'))
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
DIMENSION='goldcraft:cs_assault_f6725c06'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--instance',choices=('cs-client-a','cs-client-b'),default='cs-client-b')
    parser.add_argument('--phase',choices=('all','clip','ladder','ladder-top','walk','steps'),default='all')
    args=parser.parse_args()
    game=renderer.Sandbox(args.instance)
    prefix=renderer.ROOT/'analysis/goldcraft-tests'/f'host-movement-{int(time.time())}'
    report={'instance':args.instance,'map':'cs_assault','checks':{},'phases':{},'commands':game.commands,
            'source':'CL_CreateMove from sandbox CS commands, authoritative MC RCON and native/Java diagnostics'}
    original={}

    def sample():
        raw=game.minecraft(f'data get entity {game.player} Pos')
        match=re.search(r'\[([^\]]+)\]',raw)
        if not match:raise RuntimeError(raw)
        return {'time':time.monotonic(),'position':[float(v.strip().removesuffix('d')) for v in match[1].split(',')],
                'native':game.status(),'java':renderer.read_json(game.logs/'minecraft-client-status.json')}

    def observe(seconds):
        values=[];until=time.monotonic()+seconds
        while time.monotonic()<until:
            values.append(sample());time.sleep(.07)
        return values

    def run(name,action,seconds):
        phase={'before':sample()}
        if action:game.host(action)
        phase['samples']=observe(seconds);phase['after']=sample()
        phase['delta']=[b-a for a,b in zip(phase['before']['position'],phase['after']['position'])]
        report['phases'][name]=phase
        return phase

    def place(feet,yaw):
        game.host('menu_close');game.host(f'view {yaw} 0')
        reply=game.minecraft(f"execute in {DIMENSION} run tp {game.player} {' '.join(str(float(v)) for v in feet)} {-yaw-90} 0")
        if 'Teleported' not in reply:raise RuntimeError(reply)
        time.sleep(.65)
        state=sample()
        if not state['java']['controlling'] or state['java']['screen']:raise RuntimeError('Client is not ready for CS-driven movement')
        if max(abs(a-b) for a,b in zip(feet,state['position']))>.08:raise RuntimeError('Fixture position moved before test: '+str(state['position']))

    def capture(name):
        report.setdefault('captures',{})[name]=game.capture(prefix.with_name(prefix.name+'-'+name+'.bmp'))

    try:
        native_server=renderer.read_json(renderer.SERVER_STATUS)
        if native_server['map']!='cs_assault' or not native_server['dedicated']:raise RuntimeError('Requires dedicated cs_assault host')
        deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            try:state=sample()
            except RuntimeError as error:
                if 'No entity was found' not in str(error):raise
                time.sleep(.3);continue
            if state['java']['controlling'] and state['java'].get('hostLadderCount')==8:break
            time.sleep(.3)
        else:raise TimeoutError('New movement/ladder client not ready')
        report['before']=state;original['position']=state['position'];original['view']=state['native']['viewAngles']
        mode=game.minecraft(f'data get entity {game.player} playerGameType')
        original['mode']=int(mode.split('following entity data: ')[-1])
        game.minecraft(f'gamemode survival {game.player}')
        report['checks']['all_8_host_ladders_arrive']=state['java']['hostLadderCount']==8
        if args.phase in ('all','clip'):
            place((-51,64.0009765625,11),-90)
            wall=run('air_wall','forward 1.5',2.0)
            report['checks']['standing_stops_at_engine_clip_plane']=abs(wall['after']['position'][2]-13.4990234375)<.01
            report['checks']['no_clip_wall_tunneling']=all(p['position'][2]<=13.5001 for p in wall['samples'])
            report['checks']['no_height_drift_at_wall']=all(abs(p['position'][1]-64.0009765625)<.01 for p in wall['samples'])
            capture('air-wall')
            game.host('view -135 0')
            slide=run('wall_slide','forward 0.6',1.0)
            report['checks']['slides_along_clip_wall']=slide['delta'][0]<-.7 and abs(slide['delta'][2])<.01
            duck=run('crouch','duck 0.6',1.0)
            report['checks']['crouch_uses_hull3']=any(p['java']['hostHull']==3 for p in duck['samples']) and duck['after']['java']['hostHull']==1
            game.host('view -90 0');away=run('leave_wall','back 0.4',.9)
            report['checks']['can_leave_wall_without_sticking']=away['delta'][2]<-.6
            place((-51,64.0009765625,12.4),-90)
            duck_walk=run('crouched_air_wall','forward_duck 1.5',1.9)
            report['checks']['crouched_walk_stops_at_engine_clip_plane']=abs(duck_walk['after']['position'][2]-13.4990234375)<.01 and any(p['java']['hostHull']==3 for p in duck_walk['samples'])
        if args.phase in ('all','walk'):
            place((10,64.0009765625,-5),0);game.host('profile 8')
            walk=run('flat_walking','forward 0.5',1.2)
            report['checks']['walking_distance']=1.5<walk['delta'][0]<3 and abs(walk['delta'][1])<.01 and abs(walk['delta'][2])<.01
            jump=run('jump','jump 0.12',1.3)
            rise=max(p['position'][1] for p in jump['samples'])-jump['before']['position'][1]
            report['checks']['jump_and_land']=1.1<rise<1.35 and abs(jump['delta'][1])<.01 and jump['after']['java']['onGround']
        if args.phase in ('all','ladder'):
            place((21,64.0009765625,-13),0);capture('ladder-base')
            ascent=run('ladder_ascent','forward 2',2.1)
            report['checks']['CS_forward_climbs_host_ladder']=ascent['delta'][1]>2.5 and any(p['java']['climbing'] and p['java']['hostLadderModel']==38 for p in ascent['samples'])
            capture('ladder-mid')
            observe(.3)
            hold=run('ladder_hold','duck 1',.8)
            settled=[p['position'][1] for p in hold['samples'][3:]]
            report['checks']['crouch_holds_ladder']=bool(settled) and max(settled)-min(settled)<.02
            observe(.5)
            descent=run('ladder_descent',None,.75)
            report['checks']['controlled_ladder_descent']=descent['delta'][1]<-.8 and any(p['java']['climbing'] for p in descent['samples'])
            exit_ladder=run('ladder_exit','back 0.65',1.1)
            report['checks']['can_leave_ladder']=exit_ladder['delta'][0]<-.5 and not exit_ladder['after']['java']['climbing']
        if args.phase=='ladder-top':
            place((21,64.0009765625,-13),0)
            top={'before':sample(),'samples':[]};report['phases']['ladder_top']=top
            for index in range(6):
                game.host('forward 2');top['samples'].extend(observe(1.8))
                if top['samples'][-1]['position'][1]>81:break
            # This ladder ends beside the elevated walkway. The front remains a wall;
            # exit left (GoldSrc +Y), onto the 512 GS-unit platform.
            game.host('moveleft 0.65');top['samples'].extend(observe(1.3));top['after']=sample()
            top['delta']=[b-a for a,b in zip(top['before']['position'],top['after']['position'])]
            report['checks']['sustained_climb_reaches_top']=max(p['position'][1] for p in top['samples'])>81
            report['checks']['exits_top_onto_elevated_walkway']=abs(top['after']['position'][1]-80)<.02 and top['after']['position'][2]<-14.4 and not top['after']['java']['climbing'] and top['after']['java']['onGround']
            capture('ladder-top')
        if args.phase=='steps':
            place((-24.75,64.0009765625,-3.5),-90)
            up=run('host_step_up','forward 0.5',1.1)
            report['checks']['climbs_real_16_GS_step']=.48<up['delta'][1]<.57 and up['delta'][2]>1.5
            down=run('host_step_down','back 0.5',1.2)
            report['checks']['descends_real_step']=abs(down['after']['position'][1]-64)<.01 and down['delta'][2]<-1.5
            capture('host-steps')
        final=sample();report['after']=final
        report['checks']['no_startsolid_or_recovery_during_routes']=final['java']['hostStuckMoves']==report['before']['java']['hostStuckMoves'] and final['java']['hostRecoveryMoves']==report['before']['java']['hostRecoveryMoves']
        report['checks']['authoritative_pose_matches_CS']=max(abs(a-b) for a,b in zip(final['position'],final['native']['minecraftFeet']))<.01
        report['outcome']='PASSED' if all(report['checks'].values()) else 'FAILED'
    except Exception as error:
        report['outcome']='INCOMPLETE';report['error']=repr(error)
    finally:
        if original:
            try:
                restored_mode=('survival','creative','adventure','spectator')[original['mode']]
                game.minecraft(f"gamemode {restored_mode} {game.player}")
                feet=original['position'];game.minecraft(f"execute in {DIMENSION} run tp {game.player} {' '.join(str(v) for v in feet)}")
                view=original['view'];game.host(f'view {view[1]} {view[0]}')
            except Exception as error:report['restoreError']=repr(error)
        prefix.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps({'report':str(prefix.with_suffix('.json')),'outcome':report['outcome'],'checks':report['checks'],
                          'error':report.get('error'),'deltas':{k:v.get('delta') for k,v in report['phases'].items()}},indent=2))
    if report['outcome']!='PASSED':raise SystemExit(1)


if __name__=='__main__':main()
