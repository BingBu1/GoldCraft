"""Mix the Siren Head creature sounds from CC0 recordings. FFmpeg and curl are build tools."""

import hashlib
from pathlib import Path
import subprocess
from tempfile import NamedTemporaryFile
import wave

import numpy as np


WINDUP_SOUND = "siren_head/inhale.wav"
SOUND = "siren_head/shockwave.wav"
SAMPLE_RATE = 22050
MIX_RATE = 44100
CACHE = Path(__file__).resolve().parents[2] / "build/siren-head-horror/field-recordings"

# Courtesy of nsstudios (351512), pushkin and performer Annalisa Loeffler (241592),
# B.Harkins (683653), fattirewhitey (381784), and leonelmail (429130).
# Preserve the original recordings outside the runtime package and pin the exact takes.
# Ground recordings by burbujafilms, northern87, Caitlin_100 and TRP.
# Reaction recordings by unfa, metrostock99 and j1987.
RECORDINGS = {
    "pain_grunts": ("610/610991_1038806-hq.mp3", "fc63160db51a6812e80b54be1b517f5f4b944fc59acac5b626fed54f8eeb395f"),
    "dying_rasp": ("147/147078_1256155-hq.mp3", "605217b6c17d003c075f9bbd514d568eeba2c22e0cc6bc99755ecfe5531edfcf"),
    "duct": ("178/178835_367313-hq.mp3", "5f468e7de31d28f8c3c22b0611361ce302559e654d99b56553e388a250ed6828"),
    "bag": ("392/392008_917043-hq.mp3", "9bf9e491d6cd8447116ef7127c741ec01bf26b7bfbca32292aa006b014ccf3d9"),
    "stone": ("369/369981_1196518-hq.mp3", "9b14695665c8e2098086e3aa918a3a082aa7856e32deb4405e0d85da7339d763"),
    "gravel": ("365/365498_5407590-hq.mp3", "925f8b88ea7ccd9bef80072c7637f0eed610b91eacd5a6aa91e1ecfea4e750f3"),
    "thunder": ("574/574387_97550-hq.mp3", "912b13da2abb9292ad5e5c4545303d28f468cc917d5c6bc1626d57f54f7dfc6b"),
    "siren_outdoors": ("351/351512_2776777-hq.mp3", "20bacc798784827ebd1941e8a53ae8420924af57f79988624d24fd25330a1155"),
    "voice": ("241/241592_336074-hq.mp3", "e9d8ee9d669578f867c6a869e144602f2a4907df7d1273c0b6c23e42b1a2c426"),
    "breath": ("683/683653_14665352-hq.mp3", "fa4c360c0220768767eb0df6f45c504fbb0d6c8d063889d2cc154071b22031d3"),
    "growl": ("381/381784_4838185-hq.mp3", "518671e78a3fd1c871a4726793468625b7362c63795eb88f151513cebfef124e"),
    "metal": ("429/429130_4437257-hq.mp3", "9ec2523d78061c2609e7b4bddf546ea6a5be93959f7507657c3c5407b2afd6ec"),
}


def load_recording(key, cache, start, end, filters):
    relative, digest = RECORDINGS[key]
    path = cache / (key + ".mp3")
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        cache.mkdir(parents=True, exist_ok=True)
        with NamedTemporaryFile(dir=cache, delete=False) as tmp:
            partial = Path(tmp.name)
        try:
            subprocess.run([
                "curl", "--fail", "--silent", "--show-error", "--location", "--proto", "=https",
                "--retry", "2", "--max-time", "240", "--output", str(partial),
                "https://cdn.freesound.org/previews/" + relative,
            ], check=True)
            if hashlib.sha256(partial.read_bytes()).hexdigest() != digest:
                raise ValueError(f"The downloaded recording has changed: {key}")
            partial.replace(path)
        finally:
            partial.unlink(missing_ok=True)

    raw = subprocess.check_output([
        "ffmpeg", "-nostdin", "-v", "error", "-i", str(path), "-ss", str(start), "-t", str(end - start),
        "-af", filters, "-ac", "1", "-ar", str(MIX_RATE), "-f", "f32le", "pipe:1",
    ])
    return np.frombuffer(raw, dtype="<f4")


def add_take(bus, recording, at, start, speed, level, gain_points):
    """Place a recording at tape speed with a hand-drawn amplitude envelope."""
    duration = gain_points[-1][0]
    time = np.arange(round(duration * MIX_RATE)) / MIX_RATE
    positions = (start + time * speed) * MIX_RATE
    if positions[0] < 0 or positions[-1] >= len(recording) - 1:
        raise ValueError("Edit extends outside its source recording")
    frame = positions.astype(np.intp)
    samples = recording[frame] + (positions - frame) * (recording[frame + 1] - recording[frame])
    envelope_t, envelope_level = zip(*gain_points)
    samples *= level * np.interp(time, envelope_t, envelope_level)
    first = round(at * MIX_RATE)
    bus[first:first + len(samples)] += samples


def render(cache=CACHE):
    # Start times below are relative to these decoded windows. Only the middle of
    # the field recording is needed: several horns overlap there, then wind down.
    siren = load_recording("siren_outdoors", cache, 32.0, 68.5, "highpass=f=115,lowpass=f=5200")
    voice = load_recording("voice", cache, 0.0, 3.8, "highpass=f=105,lowpass=f=6800")
    growl = load_recording("growl", cache, 0.0, 3.6, "highpass=f=58,lowpass=f=2900")
    breath = load_recording("breath", cache, 12.55, 16.65, "highpass=f=74,lowpass=f=7600")
    metal = load_recording("metal", cache, .4, 2.12, "highpass=f=105,lowpass=f=4200")

    close = np.zeros(round(3.20 * MIX_RATE))
    horns = np.zeros_like(close)
    # The breath comes right up to the microphone. Its second, raspier intake
    # peeks through the collapsing voice at the end; both are distinct breaths.
    add_take(close, breath, 0, .39, 1.08, 4.0,
             [(0, 0), (.006, 1), (.080, 1), (.15, .70), (.255, 0)])
    add_take(close, breath, 2.19, 2.10, .88, 2.8,
             [(0, 0), (.12, .70), (.25, 1), (.52, .49), (.76, 0)])

    # The high voice is almost at its recorded speed. The other throat comes from
    # an independent performance, including the moment it catches and loses air.
    add_take(horns, voice, .075, .164, .96, .89,
             [(0, 0), (.018, .82), (.106, 1), (.47, .93), (.76, .67), (.91, .37),
              (1.105, .78), (1.38, .84), (1.69, .69), (1.97, .91), (2.22, .82),
              (2.48, .47), (2.78, .44), (3.02, 0)])
    add_take(horns, growl, .012, .041, .93, 1.28,
             [(0, 0), (.027, .84), (.13, 1), (.34, .77), (.60, .57), (.79, .32),
              (.94, .76), (1.28, .70), (1.56, .31), (1.92, .38), (2.18, 0)])

    # Actual outdoor sirens already contain reflections and fluctuating air noise.
    # A second, dying horn takes over as the exposed voice breaks off.
    add_take(horns, siren, .006, 2.04, .92, 1.25,
             [(0, 0), (.034, .50), (.14, .92), (.38, 1), (.72, .55), (1.03, .85),
              (1.29, .71), (1.62, .24), (1.93, 0)])
    add_take(horns, siren, 1.04, 31.05, .83, 1.55,
             [(0, 0), (.17, .44), (.50, .91), (.92, .69), (1.27, .47),
              (1.54, .18), (1.94, 0)])
    # A spring actually biting and releasing supplies the cabinet strain.
    add_take(horns, metal, .045, .175, .69, .84,
             [(0, 0), (.026, .95), (.095, 1), (.26, .41), (.64, .18), (1.02, 0)])

    # Colour the voices and siren together through the same PA passband.
    # Leave some direct sound in it to retain the lips and original mic overload.
    pa = subprocess.check_output([
        "ffmpeg", "-nostdin", "-v", "error", "-f", "f32le", "-ar", str(MIX_RATE),
        "-ac", "1", "-i", "pipe:0", "-af",
        "highpass=f=185,equalizer=f=770:t=q:w=1.05:g=3,lowpass=f=4200",
        "-f", "f32le", "pipe:1",
    ], input=horns.astype("<f4").tobytes())
    speaker = .32 * horns + .68 * np.frombuffer(pa, "<f4")
    mix = close + speaker
    delay = round(.054 * MIX_RATE)
    mix[delay:] += .097 * speaker[:-delay]

    # Match the loudness of the previous release with these fixed takes.
    return game_pcm(mix, .57)


def render_windup(cache=CACHE):
    # One unlayered intake; the hiss and tiny catches belong to the performer.
    breath = load_recording("breath", cache, 13.54, 14.40, "highpass=f=70,lowpass=f=7600")
    inhale = np.zeros(round(.68 * MIX_RATE))
    add_take(inhale, breath, 0, .018, .94, 1.0,
             [(0, 0), (.024, .9), (.10, 1), (.53, 1), (.65, .78), (.68, 0)])
    return game_pcm(inhale, .205)


def game_pcm(mix, peak):
    # Preserve transients and filter before reducing the sample rate for GoldSrc.
    taps = np.arange(-48, 49)
    kernel = .45 * np.sinc(.45 * taps) * np.kaiser(len(taps), 8)
    samples = np.convolve(mix, kernel / kernel.sum(), mode="same")[::2]
    samples[:44] *= np.linspace(0, 1, 44)
    samples[-110:] *= np.linspace(1, 0, 110)
    return np.rint(samples * (peak * 32767 / np.abs(samples).max())).astype("<i2")


def write_wave(path: Path, pcm=None):
    if pcm is None:
        pcm = render()
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
        partial = Path(tmp.name)
    try:
        with wave.open(str(partial), "wb") as wav:
            wav.setparams((1, 2, SAMPLE_RATE, 0, "NONE", "not compressed"))
            wav.writeframes(pcm.tobytes())
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)
