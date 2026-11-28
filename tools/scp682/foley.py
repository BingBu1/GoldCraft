"""Build short, frame-synchronized SCP-682 Foley from animal and physical recordings."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

from views import ROOT,WORK
sys.path.append(str(ROOT/'tools/siren_head_motion'))
from sound import MIX_RATE,add_take,game_pcm,write_wave

# Studio 1004 and 1008 go through the monster body and voice channels respectively.
EVENTS = (
    ((1,1004,'run_near'),(7,1004,'run_far')),
    ((5,1008,'breath'),),
    ((3,1008,'attack_r'),(14,1004,'bite_r')),
    ((3,1008,'attack_l'),(14,1004,'bite_l')),
    ((1,1008,'death'),(8,1004,'fall'),(18,1004,'settle')),
)

SOURCES={
    'bellows':('CaveboyTup','T-Rex Calls','https://freesound.org/people/CaveboyTup/sounds/529462/'),
    'hiss':('JesterWhoo','Angry snake hissing. OWI.wav','https://freesound.org/people/JesterWhoo/sounds/706977/'),
    'jaw':('FableVision_Studios','Chattering Teeth (foley)','https://freesound.org/people/FableVision_Studios/sounds/182867/'),
    'bag':('burbujafilms','HEAVY BAG DROP DIRT.wav','https://freesound.org/people/burbujafilms/sounds/392008/'),
    'gravel':('Caitlin_100','Footsteps on stones & pebbles.mp3','https://freesound.org/people/Caitlin_100/sounds/365498/'),
    'stone':('northern87','Stone dropping to ground','https://freesound.org/people/northern87/sounds/369981/'),
}


def sound_bank():
    tracks={}
    info={}
    for key,(who,title,url) in SOURCES.items():
        directory=WORK/'recordings' if key in ('bellows','hiss','jaw') else ROOT/'build/siren-head-horror/field-recordings'
        path=directory/f'{key}.mp3'
        pipe=subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-ac','1','-ar',str(MIX_RATE),
                                     '-af','highpass=f=42,lowpass=f=8400','-f','f32le','-'])
        track=np.frombuffer(pipe,'<f4')
        tracks[key]=track/max(.0001,np.max(np.abs(track)))
        info[key]=dict(author=who,title=title,url=url,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                       license='https://creativecommons.org/publicdomain/zero/1.0/')
    (WORK/'audio-sources.json').write_text(json.dumps(info,indent=2))
    return tracks


def produce():
    tracks=sound_bank()
    # Each time envelope is local to the selected take and ends before the next cue on its channel.
    def mix(name,seconds,peak,takes):
        bus=np.zeros(round(seconds*MIX_RATE))
        for key,at,start,rate,gain,envelope in takes:
            add_take(bus,tracks[key],at,start,rate,gain,envelope)
        write_wave(WORK/f'compile/sound/scp682/{name}.wav',game_pcm(bus,peak))
    for name,dirt,grit,pitch in [('run_near',12.839,6.485,.87),('run_far',17.934,7.018,.96)]:
        mix(name,.235,.49,[
            ('bag',0,dirt,pitch,.82,[(0,0),(.006,1),(.05,.91),(.14,.39),(.22,0)]),
            ('gravel',.014,grit,1.14,.52,[(0,0),(.009,1),(.08,.42),(.195,0)])])
    mix('breath',1.03,.27,[
        ('hiss',0,4.25,.74,.83,[(0,0),(.08,.37),(.28,1),(.62,.67),(1.0,0)]),
        ('bellows',.11,2.45,.68,.24,[(0,0),(.15,.43),(.34,.56),(.74,0)])])
    for side,start,air,rate in [('r',.62,4.67,.83),('l',5.85,3.96,.76)]:
        mix('attack_'+side,.91,.84,[
            ('bellows',0,start,rate,1,[(0,0),(.03,.31),(.14,.79),(.33,1),(.55,.85),(.90,0)]),
            ('hiss',.035,air,.91,.28,[(0,0),(.09,1),(.28,.69),(.58,0)])])
        mix('bite_'+side,.34,.69,[
            ('jaw',0,4.98 if side=='r' else 7.36,.76,.94,[(0,0),(.007,1),(.07,.87),(.19,.28),(.31,0)]),
            ('bag',0,12.845 if side=='r' else 17.936,1.08,.46,[(0,0),(.009,.92),(.10,.45),(.22,0)])])
    mix('death',1.39,.83,[
        ('bellows',0,11.64,.83,1.0,[(0,0),(.025,.84),(.17,1),(.48,.83),(.85,.41),(1.32,0)]),
        ('hiss',.32,4.2,.60,.42,[(0,0),(.12,.58),(.48,.93),(.87,.38),(1.05,0)])])
    mix('fall',.405,.86,[
        ('bag',0,26.62,.67,1.1,[(0,0),(.008,1),(.10,.83),(.30,.31),(.40,0)]),
        ('stone',.014,13.16,.69,.48,[(0,0),(.009,1),(.09,.46),(.30,0)])])
    mix('settle',.67,.40,[
        ('gravel',0,2.47,.66,.78,[(0,0),(.03,.72),(.12,1),(.42,.33),(.65,0)]),
        ('bag',.06,21.24,.80,.45,[(0,0),(.01,1),(.12,.52),(.34,0)])])


if __name__=='__main__':
    produce()
