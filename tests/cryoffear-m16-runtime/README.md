# M16 retained-map integration

Runs the actual M16 map runtime, cold resources, factory, class, canonical owner view and player-action life through the shared seven-gun SDK suite. The six older guns retain their complete cases. External audiovisual, target-damage and engine-stage consumers are controlled; this suite does not install the production M16 host.

```bash
python -B tests/cryoffear-p345-runtime/run.py \
  --compiler "$COF_COMPILER" \
  --environment "$COF_COMPILER_ENV" \
  --resources "$COF_SIX_GUN_RESOURCES" \
  --mp5-resources "$COF_MP5_RESOURCES" \
  --m16-resources "$COF_M16_RESOURCES" \
  --m16-sample "$COF_ORIGINAL_MP" \
  --m16-proof "$COF_M16_MAP_PROOF" \
  --deployed-root sandbox/cs-server/Half-Life/cstrike \
  --compile-host \
  --output build/cof-m16-map-validation
```

The private pre-edit proof identifies the exact original MP and its SDK/resource/owner evidence. Candidate resources must already exist in isolated workspace build roots; original assets and evidence are not part of public source. Use a fresh output directory. The runner records all actual dependencies before and after compilation.

M16 cases cover required callback/effect/resource failures, actual Create/Spawn/Add/Attach/Deploy/fire/reload, actual draw and mode-switch deadlines, failed cold action preparation retaining a valid old weapon, same-life model renewal preserving reload and stamina, delayed player pushback surviving holster/model renewal, death/new-life invalidation and map/factory cleanup. Tests supply an explicit 100-point stamina seed; they do not establish the original constructor seed, full sprint or the real damage response.

No production purchase admission, deployment, client presentation, natural multiplayer or performance acceptance is established here.

The map runtime now owns the original cooperative FireSound transport in addition to common native sounds. Tests install controlled real SDK message callbacks and check 32-listener fanout, local/remote flags, original M16 sample key, gun-position coordinates and map-cached registration. A temporarily missing writer fails one sound request; after restoration the real gun fires again without owner/life invalidation. Registration failure and absent explicit cooperative rules reject cold preparation. This is writer/fanout validation, not actual network delivery or audio playback.
