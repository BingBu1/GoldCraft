"""Render the flinch and the staggered death sounds for the Siren Head model."""

import numpy as np

from sound import CACHE, MIX_RATE, add_take, game_pcm, load_recording, write_wave


FLINCH = "siren_head/flinch.wav"
DEATH_VOICE = "siren_head/death_voice.wav"
DEATH_RUNDOWN = "siren_head/death_rundown.wav"
DEATH_BRACE = "siren_head/death_brace.wav"
DEATH_FALL = "siren_head/death_fall.wav"
DEATH_HORN = "siren_head/death_horn.wav"


def flinch_alarm(cache):
    horn = load_recording("siren_outdoors", cache, 36.72, 37.46, "highpass=f=235,lowpass=f=3900")
    pulse = np.zeros(round(.30 * MIX_RATE))
    # Two steady pitches from the same outdoor siren, joined during the head recoil.
    add_take(pulse, horn, 0, .10, 1.0, 1.0,
             [(0, 0), (.006, 1), (.13, 1), (.14, 0)])
    add_take(pulse, horn, .13, .30, 4 / 3, .94,
             [(0, 0), (.010, 1), (.148, 1), (.17, 0)])
    return game_pcm(pulse, .36)


def winding_down(siren, duct):
    # Slow a field recording through its original harmonics as the head loses power.
    time = np.arange(round(1.12 * MIX_RATE)) / MIX_RATE
    speed = np.interp(time, (0, .16, .41, .75, 1.12), (.97, .95, .81, .51, .28))
    cursor = np.cumsum(speed) - speed[0]
    frame = cursor.astype(np.intp)
    rotor = siren[frame] + (cursor - frame) * (siren[frame + 1] - siren[frame])
    envelope = np.interp(time, (0, .015, .19, .44, .68, .91, 1.12),
                         (0, .82, 1, .77, .61, .25, 0))
    mix = rotor * envelope
    # The housing first bucks, then goes quiet while the voice is still audible.
    add_take(mix, duct, .012, 2.043, .83, .35,
             [(0, 0), (.007, 1), (.09, .82), (.23, .17), (.38, 0)])
    return game_pcm(mix, .34)


def render_reactions(cache=CACHE):
    rasp = load_recording("dying_rasp", cache, 0, 8.1, "highpass=f=85,lowpass=f=5200")
    duct = load_recording("duct", cache, 0, 22.6, "highpass=f=90,lowpass=f=4900")
    siren = load_recording("siren_outdoors", cache, 64.65, 66.35, "highpass=f=165,lowpass=f=3500")
    bag = load_recording("bag", cache, 34.70, 42, "highpass=f=54,lowpass=f=2450")
    gravel = load_recording("gravel", cache, 0, 10.8, "highpass=f=135,lowpass=f=4750")
    thunder = load_recording("thunder", cache, 5.45, 7.65, "highpass=f=49,lowpass=f=220")

    # An uneven exhale breaks, catches again, then loses its last bit of air.
    voice = np.zeros(round(2.82 * MIX_RATE))
    add_take(voice, rasp, 0, .46, .89, 1.0,
             [(0, 0), (.023, .81), (.25, 1), (.69, .88), (.96, .63),
              (1.27, .41), (1.48, .13), (1.67, 0)])
    add_take(voice, rasp, 1.36, 5.26, 1.05, .95,
             [(0, 0), (.07, .47), (.18, .79), (.39, .87), (.72, .65),
              (1.07, .40), (1.43, 0)])

    brace = np.zeros(round(.67 * MIX_RATE))
    add_take(brace, gravel, 0, 5.49, .72, .77,
             [(0, 0), (.009, .81), (.11, 1), (.28, .66), (.49, .17), (.63, 0)])
    add_take(brace, bag, .012, 5.874, .92, .53,
             [(0, 0), (.008, 1), (.1, .66), (.22, .20), (.37, 0)])

    # Spread the body weight across two dull impacts, with a low ground response.
    fall = np.zeros(round(1.03 * MIX_RATE))
    add_take(fall, bag, 0, .269, .59, 1.42,
             [(0, 0), (.009, 1), (.13, .95), (.33, .65), (.59, .17), (.78, 0)])
    add_take(fall, bag, .089, 5.875, .72, .67,
             [(0, 0), (.015, .92), (.075, 1), (.23, .54), (.43, 0)])
    add_take(fall, thunder, .025, .26, .78, .90,
             [(0, 0), (.1, 1), (.31, .85), (.55, .51), (.75, .20), (.99, 0)])
    add_take(fall, gravel, .058, .132, .82, .50,
             [(0, 0), (.017, .88), (.10, .85), (.27, .42), (.47, 0)])

    # The trumpet reaches the floor after the torso. Leave air around the shell's rebound.
    horn = np.zeros(round(1.08 * MIX_RATE))
    add_take(horn, duct, 0, 4.077, .74, .99,
             [(0, 0), (.006, 1), (.13, .98), (.32, .77), (.56, .45), (.84, .15), (1.03, 0)])
    add_take(horn, duct, .176, 2.043, .88, .37,
             [(0, 0), (.009, .93), (.13, .78), (.28, .31), (.51, 0)])
    add_take(horn, gravel, .051, 9.454, .98, .16,
             [(0, 0), (.012, 1), (.10, .68), (.26, .17), (.42, 0)])
    return {FLINCH: flinch_alarm(cache), DEATH_VOICE: game_pcm(voice, .48),
            DEATH_RUNDOWN: winding_down(siren, duct), DEATH_BRACE: game_pcm(brace, .34),
            DEATH_FALL: game_pcm(fall, .75), DEATH_HORN: game_pcm(horn, .44)}


def write_reactions(sound_dir):
    for name, samples in render_reactions().items():
        write_wave(sound_dir / name, samples)
