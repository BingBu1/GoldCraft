# P345 cold resource preparation

Before implementation gate (2026-11-11): the copied original x86 hl.dll has SHA256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63, base 0x10000000. All 33 previously IDA-verified intervals (17013 bytes) were rehashed against the copied PE before writing production code. Private evidence is analysis/cof-weapons-20261111/server-evidence/p345-byte-verification.json and P345_BACKENDS.md. The Precache consumer 1005EB90 explicitly uses the full police/models/weapons/p345/p_p345.mdl string; no substring fallback is accepted. Its complete model/sound arguments were recovered from the saved IDA instruction addresses and current matching PE.

Aim consumers 1005DDB0/1005E090 read inventoryitems/weapons/weapon_p345.txt through 100DFF70, then write forward/right/up at 17C/180/184 and integer zoom at188. The original metadata SHA is 376a05dad38387981be4a7f88a1a6444582f5781afd209df9a3d5a7ea18bb2fe (461 bytes). Cold parsing is a deliberate strict admission policy: all four fields and exact classname must be present once, with finite fully parsed numbers; missing or malformed values are rejected rather than inheriting old weapon state. Original field order and double-to-float conversion are retained.

The helper prepares actual SDK engine model/sound/event/generic and ALLOC_STRING IDs through the normal VFS. It does not register a weapon, set particle/audio/GL/backend readiness, or validate a candidate P model's grip. Namespaced host assets preserve original logical sound keys; local weapons_new assets are download resources, not proof that XAudio voices were prepared. Melee voice sounds retain player/police/Swing keys and their existing CHAN_VOICE consumer. The actual ReHLDS PrecacheModelSpecifiedFiles service remains responsible for model texture/sequence-group dependencies; no competing dependency cache is added.

Only cold preparation may read files. The caller owns generation authentication and caches the immutable result. Failures preserve the prior output; engine precache side effects cannot be rolled back. Original custom models and inventory capacity/pickup rules remain outside this helper.

Validation and exact command will be appended after the actual SDK fixture completes. No deployment, gameplay or performance acceptance is implied.

The implemented API is csol::server::cof::PrepareP345Resources(ResourceAuthority, P345Resources). Authority snapshots map/content generation and checks it around every external engine callback, including file-release boundaries. Output contains eight model indices, twenty engine sound indices, seven local PCM download indices, two event indices, five engine string IDs, two generic file indices, thirty-four sequence durations and the parsed aim fields. Generic indices are zero-based: actual ReHLDS PF_precache_generic_I/AddGeneric accepts zero. No readiness boolean is inferred from these indices.

Build04 passed 570 actual SDK checks with zero failures using39 real files and87 engine callback edges. Coverage includes every missing static resource, cancellation at every engine edge, every returned engine failure, all34 real P345 timings, exact metadata float bits/order, malformed metadata/model rejection, transactional output and repeat stable IDs. Builds01/02 preserve fixture compilation failures; build03 preserves a test-only struct-padding memcmp error. The semantic transaction comparison was corrected without weakening production rejection. The final source snapshots the authority by value; build05 is the final frozen rerun.

Run directly from Git Bash (use the copied exact original module for COF_HL_SAMPLE and the read-only original resource root for COF_RESOURCE_ROOT):

~~~bash
python tests/cryoffear-resource-preparation/run.py \
  --sample "$COF_HL_SAMPLE" --original "$COF_RESOURCE_ROOT" \
  --clip sandbox/cs-server/Half-Life/valve/models/w_9mmclip.mdl \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-resource-preparation-20261111-05
~~~

The named clip resource is absent from the original loose CoF directory. The fixture explicitly uses the existing workspace valve MDL as a host common dependency; this does not prove the original CoF inherited VFS source. The original police P file is present and is read directly by this fixture, with no candidate substitution or grip acceptance. The controlled SDK fixture maps original bytes to the documented host paths in memory. No resource installer, live engine precache, registry/factory, deployment, game, client native sound-cache/voice/particle preparation or real performance test ran. The final receipt hashes every compiler-discovered input, original window, resource, compiler and toolchain configuration before/after; no source snapshot is made.

Integration boundaries: the existing native PistolMelee sound consumer still uses original non-namespaced paths until its separately owned samples interface is connected. The unchanged original V-model has seven Studio5004 events using magout/magplace/magin/slide original weapons/p345 keys. Client audio plans must map those to their prepared weapons_new keys and namespaced host assets. ReHLDS sv_auto_precache_sounds_in_models defaults0; enabling its generic original-key scan is a separate closure concern. The helper does not claim these consumers are connected. The parent owns their exact mapping, other backend admission, publication and shared continuity.
