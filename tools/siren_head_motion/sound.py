"""Synthesize the mono shockwave emitted by the two Siren Head horns."""

from pathlib import Path
from tempfile import NamedTemporaryFile
import wave

import numpy as np

SOUND = "siren_head/shockwave.wav"
SAMPLE_RATE = 22050
MIX_RATE = 2 * SAMPLE_RATE
TAU = 2 * np.pi


def contour(t, *points):
    """Interpolate timed control points with rounded transitions."""
    times, values = np.array(points, dtype=float).T
    i = np.searchsorted(times, t, side="right").clip(1, len(times) - 1)
    a = ((t - times[i - 1]) / (times[i] - times[i - 1])).clip(0, 1)
    return values[i - 1] + (values[i] - values[i - 1]) * a * a * (3 - 2 * a)


def resonator(signal, frequency, bandwidth):
    """Causal, damped resonance with approximately unit gain at its centre."""
    decay = np.pi * bandwidth
    time = np.arange(int(np.ceil(7 * MIX_RATE / decay))) / MIX_RATE
    impulse = (2 * decay / MIX_RATE) * np.exp(-decay * time) * np.cos(TAU * frequency * time)
    return np.convolve(signal, impulse)[: len(signal)]


def rotor(frequency, ports, opening, initial_angle):
    # A port admits air for part of each turn. One worn port is weaker each revolution.
    phase = initial_angle + (TAU / MIX_RATE) * np.cumsum(frequency)
    pulses = np.zeros_like(phase)
    for n in range(1, 19):
        strength = (2 / (np.pi * n)) * np.sin(np.pi * n * opening)
        pulses += strength * np.cos(n * phase)
    weak_port = ((1 + np.cos(phase / ports)) * 0.5) ** 5
    return pulses * (1 - 0.32 * weak_port)


def broken_voice(t, noise):
    # A glottal source with moving vowel resonances gives two scraps of PA speech.
    # Each scrap has its own consonant and pitch break. There is no recorded voice.
    pitch = contour(t, (0, 104), (.51, 121), (.59, 99), (.74, 87), (.94, 138), (1.14, 91), (1.32, 79))
    pitch *= 1 + .017 * np.sin(TAU * 6.4 * t) + .009 * np.sin(TAU * 18.1 * t)
    phase = TAU / MIX_RATE * np.cumsum(pitch)
    f1 = contour(t, (0, 390), (.50, 390), (.59, 760), (.74, 480), (.91, 380), (1.02, 650), (1.19, 320))
    f2 = contour(t, (0, 940), (.5, 940), (.60, 1290), (.74, 940), (.92, 2040), (1.04, 1620), (1.20, 820))
    voice = np.zeros_like(t)
    for n in range(2, 33):
        frequency = n * pitch
        gain = (
            np.exp(-.5 * ((frequency - f1) / 105) ** 2)
            + .84 * np.exp(-.5 * ((frequency - f2) / 160) ** 2)
            + .38 * np.exp(-.5 * ((frequency - 2460) / 195) ** 2)
        )
        voice += (4.5 / n) * gain * np.sin(n * phase - .55 * n)
    voice *= .76 + .24 * np.sin(.5 * phase)
    syllables = contour(t, (.46, 0), (.52, .66), (.60, 1), (.67, .89), (.75, 0),
                        (.87, 0), (.93, .56), (1.01, .92), (1.10, .68), (1.23, .42), (1.27, 0))
    hiss = contour(t, (.43, 0), (.47, .32), (.53, 0), (.86, 0), (.9, .44), (.95, 0))
    return syllables * np.tanh(1.6 * voice) + hiss * resonator(noise, 2640, 1130)


def render():
    t = np.arange(round(2.10 * MIX_RATE)) / MIX_RATE
    rng = np.random.default_rng(4107)
    noise = rng.standard_normal(len(t))
    # The large horn spins up immediately. The small horn catches later, keeps climbing
    # as the first sags, and stalls first. Both project into the same positional channel.
    low_hz = contour(t, (0, 168), (.065, 252), (.25, 331), (.44, 353), (.72, 288),
                     (.96, 337), (1.12, 306), (1.28, 227), (1.54, 155), (1.85, 91))
    high_hz = contour(t, (0, 217), (.065, 247), (.18, 385), (.37, 484), (.52, 501),
                      (.69, 402), (.86, 458), (1.05, 492), (1.17, 427), (1.36, 250), (1.65, 121))
    wobble_times = np.arange(0, 2.2, .023)
    flutter = np.interp(t, wobble_times, rng.uniform(-1, 1, len(wobble_times)))
    low_hz *= 1 + .004 * flutter + .005 * np.sin(TAU * 7.1 * t)
    high_hz *= 1 - .006 * flutter + .008 * np.sin(TAU * 5.3 * t + 1.2)
    low = rotor(low_hz, 5, .33, .8)
    high = rotor(high_hz, 6, .26, 2.3)
    low = .46 * low + resonator(low, 660, 320) + .58 * resonator(low, 1460, 530)
    high = .29 * high + 1.12 * resonator(high, 975, 460) + .76 * resonator(high, 2380, 570)
    low_level = contour(t, (0, 0), (.012, .64), (.10, .91), (.27, 1), (.48, .90),
                        (.57, .40), (.73, .86), (.90, .62), (1.06, .88), (1.22, .46),
                        (1.37, .10), (1.58, .027), (1.78, 0))
    high_level = contour(t, (.025, 0), (.065, .16), (.17, .76), (.32, 1), (.49, .95),
                         (.56, .19), (.67, .72), (.82, .97), (.95, .26), (1.075, .76),
                         (1.18, .52), (1.27, .09), (1.40, .02), (1.58, 0))

    # The two interruptions expose the almost intelligible voice inside the horns.
    dropout = contour(t, (.506, 1), (.518, .24), (.555, .34), (.572, .86),
                      (.881, 1), (.898, .22), (.947, .43), (.968, 1))
    throat = .83 * low_level * low + .74 * high_level * high
    cone = np.tanh(2.65 * throat) + .13 * np.tanh(4 * throat ** 2)
    contact = sum(np.exp(-((t - start) / width) ** 2) for start, width in
                  ((.009, .005), (.061, .009), (.518, .005), (.897, .005), (1.248, .012)))
    static = resonator(noise, 2290, 1350)
    speaker = dropout * cone + .43 * broken_voice(t, noise) + (.32 * contact + .12 * high_level) * static

    # Local pressure at the start and a dragging motor beneath the dying note.
    motor_phase = TAU / MIX_RATE * np.cumsum(.49 * low_hz)
    motor = .24 * low_level * np.sin(motor_phase + .23 * np.sin(3 * motor_phase))
    blow = .22 * np.sin(TAU * (49 * t + 1.9 * (1 - np.exp(-t / .052)))) * np.exp(-t / .095)
    # A close diaphragm strike takes the peak. The sustained alarm can sit behind
    # it without flattening the changes in level that expose the failing broadcast.
    strike = np.exp(-t / .023) * (1 - np.exp(-t / .0018))
    shell = np.sin(TAU * 184 * t) + .42 * np.sin(TAU * 637 * t + .6) + .28 * static
    direct = speaker + motor + blow + 2.65 * strike * shell
    reflection = .44 * resonator(speaker, 720, 690) + .27 * resonator(speaker, 1440, 770)
    mix = direct.copy()
    for seconds, level in ((.047, .17), (.113, .13), (.187, .10), (.294, .07), (.431, .035)):
        delay = round(seconds * MIX_RATE)
        mix[delay:] += level * reflection[:-delay]
    mix *= contour(t, (0, 0), (.004, 1), (1.91, 1), (2.09, 0))

    # Remove the cone's DC component, then anti-alias the nonlinear mix before decimation.
    mix -= np.convolve(mix, np.ones(883) / 883, mode="same")
    taps = np.arange(-48, 49)
    kernel = .45 * np.sinc(.45 * taps) * np.kaiser(len(taps), 8)
    samples = np.convolve(mix, kernel / kernel.sum(), mode="same")[::2]
    samples[:88] *= np.linspace(0, 1, 88)
    samples[-220:] *= np.linspace(1, 0, 220)
    return np.rint(samples * (.8 * 32767 / np.abs(samples).max())).astype("<i2")


def write_wave(path: Path):
    pcm = render()
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    try:
        with NamedTemporaryFile(dir=path.parent, delete=False) as f:
            temp = Path(f.name)
            with wave.open(f, "wb") as wav:
                wav.setparams((1, 2, SAMPLE_RATE, 0, "NONE", "not compressed"))
                wav.writeframes(pcm.tobytes())
        temp.replace(path)
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)
