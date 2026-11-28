"""Build the Siren Head ground sounds from recorded impacts, gravel and thunder."""

import numpy as np

from sound import CACHE, MIX_RATE, add_take, game_pcm, load_recording, write_wave


LEFT_STEP = "siren_head/step_left.wav"
RIGHT_STEP = "siren_head/step_right.wav"
STOMP_LOAD = "siren_head/stomp_load.wav"
STOMP_HIT = "siren_head/stomp_hit.wav"


def walking_step(bag, gravel, thunder, *, bag_at, gravel_at, earth_at, speed, peak):
    mix = np.zeros(round(.84 * MIX_RATE))
    # Lead with the heel. The loose stones move under the sole after it takes weight.
    add_take(mix, bag, 0, bag_at, speed, 1.12,
             [(0, 0), (.005, 1), (.115, 1), (.26, .78), (.47, .16), (.62, 0)])
    add_take(mix, gravel, .018, gravel_at, .86, .33,
             [(0, 0), (.012, .95), (.11, 1), (.22, .63), (.36, 0)])
    add_take(mix, thunder, .029, earth_at, .82, .68,
             [(0, 0), (.074, 1), (.18, .81), (.34, .50), (.57, .19), (.80, 0)])
    return game_pcm(mix, peak)


def render_footfalls(cache=CACHE):
    # Start at zero so every edit position below refers to its original recording.
    bag = load_recording("bag", cache, 0, 28, "highpass=f=55,equalizer=f=205:t=q:w=.9:g=3,lowpass=f=3200")
    gravel = load_recording("gravel", cache, 0, 10.9, "highpass=f=115,lowpass=f=5200")
    stone = load_recording("stone", cache, 0, 25.9, "highpass=f=75,lowpass=f=4800")
    thunder = load_recording("thunder", cache, 0, 7.8, "highpass=f=44,lowpass=f=245")
    hinge = load_recording("metal", cache, 0, 5.6, "highpass=f=145,lowpass=f=2500")

    left = walking_step(bag, gravel, thunder, bag_at=12.839, gravel_at=6.485,
                        earth_at=3.26, speed=.68, peak=.55)
    right = walking_step(bag, gravel, thunder, bag_at=17.934, gravel_at=7.018,
                         earth_at=4.82, speed=.73, peak=.53)

    # The planted leg grinds into the ground; the frame creaks as the other foot lifts.
    load = np.zeros(round(.72 * MIX_RATE))
    add_take(load, gravel, 0, 2.46, .61, .42,
             [(0, 0), (.06, .50), (.22, 1), (.44, .75), (.62, 0)])
    add_take(load, hinge, .068, 3.29, .60, .75,
             [(0, 0), (.08, .33), (.28, .88), (.41, 1), (.54, .58), (.65, 0)])

    hit = np.zeros(round(2.12 * MIX_RATE))
    # The high edge locates the strike even on small speakers. A second, slower
    # impact carries the pelvis settling onto the planted foot just afterwards.
    add_take(hit, stone, 0, 13.157, .86, .91,
             [(0, 0), (.003, 1), (.10, .87), (.23, .43), (.39, 0)])
    add_take(hit, bag, .003, 26.62, .55, 1.38,
             [(0, 0), (.009, 1), (.13, 1), (.34, .73), (.57, .27), (.72, 0)])
    add_take(hit, bag, .105, 21.246, .59, .62,
             [(0, 0), (.018, .89), (.09, 1), (.23, .51), (.46, 0)])
    # A recording of rolling thunder supplies the irregular ground response.
    add_take(hit, thunder, .018, 3.55, .84, 1.28,
             [(0, 0), (.062, .71), (.14, 1), (.37, .86), (.61, .73),
              (.89, .54), (1.17, .38), (1.44, .23), (1.78, .09), (2.08, 0)])
    add_take(hit, gravel, .048, 8.35, .79, .64,
             [(0, 0), (.026, 1), (.19, .88), (.35, .49), (.53, 0)])
    # Separate real stone drops for the scattered fragments. They land off the beat.
    add_take(hit, stone, .32, 5.18, .96, .52,
             [(0, 0), (.025, .93), (.16, 1), (.29, .46), (.45, 0)])
    add_take(hit, stone, .68, 10.22, 1.0, .40,
             [(0, 0), (.023, .92), (.11, 1), (.25, .43), (.41, 0)])
    add_take(hit, stone, 1.16, 1.07, 1.0, .36,
             [(0, 0), (.021, 1), (.10, .93), (.21, .51), (.34, 0)])
    return {LEFT_STEP: left, RIGHT_STEP: right, STOMP_LOAD: game_pcm(load, .19),
            STOMP_HIT: game_pcm(hit, .86)}


def write_footfalls(sound_dir):
    for name, samples in render_footfalls().items():
        write_wave(sound_dir / name, samples)
