"""Render the mono GoldSrc sample that starts on the shockwave's attack frame."""

from pathlib import Path
import wave

import numpy as np

SOUND = "siren_head/shockwave.wav"


def write_wave(path: Path):
    rate = 22050
    t = np.arange(round(1.24 * rate)) / rate
    # Two differently sized brass horns drift apart under load and sag together as the air dies.
    low = 105 + 84 * np.exp(-t / 0.185) + 28 * np.sin(2 * np.pi * 3.2 * t) * np.exp(-t / 0.23)
    high = 153 + 139 * np.exp(-t / 0.16) + 5.2 * np.sin(2 * np.pi * 4.3 * t + 0.4)
    phases = [2 * np.pi * np.cumsum(freq) / rate for freq in (low, high)]
    brass = np.zeros_like(t)
    for phase, weight in zip(phases, (1.0, 0.67)):
        for harmonic, gain in (
            (1, 0.66),
            (2, 0.40),
            (3, 0.32),
            (4, 0.22),
            (6, 0.13),
            (9, 0.068),
            (13, 0.029),
        ):
            brightness = np.exp(-t * (1.9 + 0.52 * harmonic))
            brass += weight * gain * brightness * np.sin(harmonic * phase + 0.12 * harmonic)
    attack = (1 - np.exp(-t / 0.009)) ** 2
    gate = np.minimum(1.0, (1.24 - t) / 0.18) ** 2
    horn = np.tanh(brass * 1.37) * attack * gate * np.exp(-t / 1.08)
    # Seeded band-limited air and a short second pressure pulse keep the transient rough but smooth.
    noise = np.random.default_rng(4107).normal(size=len(t))
    band = np.convolve(noise, np.ones(8) / 8, mode="same") - np.convolve(noise, np.ones(38) / 38, mode="same")
    air = band * 0.7 * attack * np.exp(-t / 0.09)
    thump = (
        np.sin(2 * np.pi * np.cumsum(42 + 21 * np.exp(-t / 0.055)) / rate)
        * (1 - np.exp(-t / 0.004))
        * np.exp(-t / 0.12)
    )
    wave_data = horn + air + 0.63 * thump
    echo = int(0.086 * rate)
    wave_data[echo:] += 0.13 * horn[:-echo]
    wave_data *= np.minimum(1.0, t / 0.004) * np.minimum(1.0, (1.24 - t) / 0.03)
    pcm = np.round(wave_data * (0.80 * 32767 / np.max(np.abs(wave_data)))).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(pcm.tobytes())
