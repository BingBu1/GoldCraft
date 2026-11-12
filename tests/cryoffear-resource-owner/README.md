# CoF cold resource owner

Builds the production owner, sound/particle message consumers, XAudio2 service
and Aurora parser against the actual client SDK. The fixture supplies the native
connection/subscription/sound-cache boundary and reads the original fourteen
WAVs and smoke script through a controlled resource provider. It creates real
XAudio2 voices with user volume zero; it is not audible or in-game acceptance.

Checks ordinary-server zero file/sprite/device preparation, missing/partial native
registration rejection, registered-only native sounds with independently prepared
local PCM, explicit memory/voice budgets, real PCM and script
admission, both individual weapon groups, duplicate completion, immediate
retirement, stale callback isolation, replacement/reentry, thread ownership,
video-mode particle rebuild without restarting a loop, and failure cleanup.
The native lifecycle ABI has its own actual-engine-TU tests; this fixture does
not emulate a whole engine or establish complete weapon readiness.

`run.py --environment <compiler-env.json> --commands <compile_commands.json>
--original <original-game-root> --output <fresh-workspace-build-directory>`

No original resources, engine binaries or IDA evidence are copied into the
repository or the build directory. The runner records actual compiler
dependencies and verifies unchanged input hashes. These checks do not establish
actual loading performance, gameplay audio, drawing or multiplayer behavior.

The fixture also links the real Studio/viewpunch adapter. It installs its matched
post-punch consumer through a controlled installer, records the real namespaced
event registration, prepares cached viewmodels/shells, and checks the public
studioAndRecoilPrepared field against actual adapter readiness. Missing model
or consumer leaves that field false while audio/particles can remain prepared.
It exercises actual Studio5004 through the original WAV-backed XAudio2 voices
and5006 through the cached shell request, plus cancellation/reentry during cold
model admission. It does not fake EffectsReady or prove real MetaHook relocation.

The cold owner now also reads the five SHA-pinned shot TGA files for the Glock
group. This audio/particle fixture supplies a controlled decal preparation
provider (and checks those bytes), so it makes no decal GL readiness claim.
The actual decal adapter, real textures, PVS and single-owner closure are tested
by `tests/cryoffear-custom-decal`, including a hidden real WGL context.
