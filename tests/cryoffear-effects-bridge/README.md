# CoF client effects bridge

This bridge connects the existing original-backed Studio and viewpunch consumers to the real CS client paths. It is a host adapter, not a complete CoF effects implementation.

Original CoF client SHA-256: d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00. Studio events use the previously verified 5004/5006/5080 consumer. The viewpunch event is 10052E00 ->100AE9A0; original custom angles are added after native punch at100B0870, then advanced at100B08A1 before stairs and smoothing. Original nextView==1 exits before that stage. Its custom angle/velocity have no observed weapon/death/video clear.

Matched host client SHA-256: b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2. Native punch executes at1006B7D8..1006B809. GoldCraftCof_AfterNativePunch is a matched patch at RVA0x6b80e; initialization checks 83 be c4 00 00 00 00. The hook calls the existing custom punch consumer before executing the original comparison and remaining stair/smoothing/final-global updates. It preserves GPRs, flags, x87/SIMD and MXCSR. Calls outside its downstream refdef scope do not apply custom punch.

The actual registration uses ResourceCatalog's events/guest/cof/viewpunch.sc. The adapter preserves original AttachCam event gating; nonzero camera state must come from a proven producer. The current server integration has no complete CoF AttachCam lifecycle. Custom state persists across weapon release and resource invalidation; a new connection generation starts an isolated zero state as host session protection.

Cold preparation requires an installed punch hook, completed void event registration, real prepared CoF audio, matching namespaced Glock/P345 viewmodels and shell, and native effect interfaces. It caches model pointers/indexes. An owned Studio failure is reported and consumed, preventing an unrelated native sound fallback. Ordinary CS models remain on their existing path. 5004 uses PlayGuestCofLocalSound and real prepared LocalAudio;5006 keeps the original attachments, velocity*10,2.5-second lifetime and sound request. 5080 dispatches the original zoominmdl command; the host server currently has no matching consumer, so that effect is incomplete. Non-weapons spatial5004 assets have no verified namespace mapping and remain MissingBackend. Other weapons/world model Studio events and complete muzzleflash/other effects are outside this bridge.

Run from Git Bash:

    python tests/cryoffear-effects-bridge/run.py --environment build/resource-lifecycle-engine-20261108-inputs/environment.json --commands build/resource-bridge-20261108/csol/compile_commands.json --output build/cof-effects-bridge-<new-id>

The test builds actual production adapters/consumers against the real x86 SDK with O3/ThinLTO/AVX2 and precise floating point. It prepares generated silent PCM in the real XAudio2 backend and executes the actual naked post-punch thunk. It checks dispatch, no native audio fallback/hot model lookup, native punch exactly once, scalar/state sequencing, full FX state/flags, view/AttachCam gates, persistent state, connection retirement, nested scopes and failed consumer admission even while audio is ready. It uses a test hook installer and does not prove real MetaHook relocation, event delivery, rendered shells, audible playback, full DLL integration or live FPS.

No hot file/network lookup or allocation is added. Once custom punch has been accepted, preserving FP state adds one FXSAVE/FXRSTOR pair per reached native stage. That cost and all gameplay effects still require live measurement.
