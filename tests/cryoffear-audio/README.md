# Local CoF PlaySFX audio

This suite compiles the real Windows XAudio2 service in
`native/guest/cof/client/audio.cpp`, creates actual system voices and submits
silent buffers. No game process, original irrKlang DLL, alternate sound model
or EV_PlaySound fallback is used. Device playback and voice queries are tested;
audible gameplay acceptance remains separate.

```bash
python -I tests/cryoffear-audio/run.py \
  --environment build/resource-lifecycle-engine-20261108-inputs/environment.json \
  --output build/cof-client-audio-validation \
  --sample /path/to/private/original/client.dll \
  --resources /path/to/private/cryoffear/sound/weapons_new
```

The last two arguments are optional. `--sample` enables original x86 parameter
instruction replay; `--resources` reads the four weapon directories without
copying their files. No private bytes/assets/paths are embedded in this suite.
Clang x86 C++20/O3/ThinLTO/AVX2/precise, Windows SDK and a working Windows audio
endpoint are required. Missing device support fails explicitly.

## Connect the local callback

Create one `LocalAudio` for the receiving process, independent of local weapon
selection. Call `Initialize` with explicit sample/voice/PCM budgets and volume
and RandomFloat providers. Both providers return float; the random provider
must implement the host's actual `RandomFloat(-0.1f,0.1f)` contract.

On the owner thread, cold-load the real workspace `gameDir/sound/` WAV files and
call `Prepare(rewrittenKey, bytes, simultaneousVoices)` for each. `Prepare`
copies immutable PCM data and precreates that sample's bounded voice pool.
After `Activate`, preparation is closed until `Shutdown`/reinitialization.
Do preparation outside drawing, input and message hot paths. A failed preparation
must prevent advertising that resource as ready.

The presentation local callback forwards `LocalSound` directly to `Play` and
returns true only for `AudioResult::Applied`. Presentation already rewrites the
path; the backend must not rewrite it twice. Audio transport authentication and
recipient/session isolation belong to the bridge. Ordinary CS listeners and
clients without a local active CoF weapon still receive other players' sounds.
Never tie all audio reception to the local FOV authority or source weapon's
generation.

Call `Update` at the original per-frame audio maintenance stage to refresh only
the global loop slot's volume. Forward the actual paused-state transition to
`SetPaused`. It affects existing active voices. As in original PlaySFX, a new
sound is explicitly unpaused after creation; it does not inherit a prior pause
operation. Do not clear the service on death, weapon switch or VideoInit.
Call `Shutdown` on the initializing thread at the actual backend lifetime end,
before plugin unload. This destroys voices before releasing their PCM data,
then releases the independent engine and COM ownership. All methods and the
destructor belong to the initializing thread. Only the device-critical-error
callback runs on an audio thread, writing a lock-free status word; it never
accesses game state.

## Retained semantics and host adaptations

| Original behavior | Service behavior |
| --- | --- |
| mode 1 | Stop previous global slot, create a new loop, retain the new slot |
| mode 2 | Stop global slot only; no resource lookup, volume or RNG read |
| other mode | Create independent non-looping sound; duplicates overlap |
| local bool and origin | Accepted but unused by playback; 2D, no spatial listener |
| create paused, set volume/speed, unpause | Set properties on stopped source, submit, then start |
| live volume cvar | Refresh only retained slot; one-shots keep initial volume |
| new backend instance | Explicit cold initialization; release everything on shutdown |

The backend uses Windows system XAudio2 2.9, loaded from System32, and adds no
third-party runtime. Its mixer/resampler/device output is a host adaptation,
not a claim of bit-identical irrKlang output. RIFF PCM 8/16/24/32-bit mono/stereo
is supported; unsupported encodings and malformed/truncated chunks fail.
Budget exhaustion, unprepared keys and voice saturation fail without stealing
active voices or loading resources in the hot path. The caller selects budgets
within hard ceilings of 256 samples, 2,048 voices and 256 MiB PCM. Cached sample
memory is released on shutdown; there is no unbounded cache.

The original requested parameter bit patterns are preserved, including negative
zero volume. XAudio2 reports negative zero volume as positive zero; the oracle
records that system normalization separately. Parameter application is
asynchronous, so diagnostic tests wait for a processing quantum. Production
calls do not wait. Non-finite or invalid provider values may cause XAudio2 to
reject playback and return `DeviceFailure`.

## Validation boundaries

The lifecycle test covers malformed PCM, cold capacity and duplicate keys,
parallel source buffers, the single global loop slot, stop/replacement,
pause/resume/new sounds during pause, owner-thread rejection, natural completion,
pool reuse, shutdown/reinitialization and zero application `operator new` calls
during its hot interval. It does not measure XAudio2 driver allocations or real
game frame time. Device-critical-error forwarding is source/SDK reviewed;
physical device removal has not been exercised.

The optional parameter oracle runs the SHA-pinned original instructions at
`10080572..100805F4`, stubbing only cvar/random/external voice providers, then
compares them with values submitted by the actual service and queried from
actual XAudio2 voices. The optional resource test prepares and simultaneously
submits the four original weapon directories at volume zero. Source hashes,
compiler identity and outputs are recorded in `compile-receipt.json`.
