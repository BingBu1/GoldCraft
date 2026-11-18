# FAMAS reload and pushback controls

Replay complete original x86 `1003C2C0` and `1003C370` with their actual
reload/model-duration helpers and the matching FAMAS view model. The private
pre-edit proof binds the module/model hashes, virtual slots, original bytes
and IDA archives. Original code and resources are not included here.

From Git Bash, with the existing Unicorn and matched x86 compiler environment:

```bash
python -B tests/cryoffear-famas-control/run.py \
  --sample "$COF_MP_SAMPLE" --model "$COF_FAMAS_VIEW" \
  --proof "$COF_FAMAS_PREEDIT" --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" --output build/cof-famas-control-new
```

The original `GetItemInfo` registers capacity30 at runtime. The replay seeds
that actual registry entry; treating the PE's zero-filled BSS as initialized
weapon metadata would invalidate full-magazine coverage. ID23 deliberately
bypasses the common reserve gate. The tests include zero-reserve reloads,
capacity-before-aim ordering, actual loaded5/empty6 model durations, cleared
shot phase, sequence11 pushback, positive shared stamina and clock/clip changes
inside callbacks. State and every ordered event snapshot must match.

Host cases retain skill-expanded magazines, stop retired lifetimes and reject
missing required inputs without modifying state. A failed optional view event
returns `AppliedWithoutView` while mechanical reload/cooldowns continue; failed
pushback scheduling leaves firearm state usable. These are explicit host
adaptations. Malformed comparison files must fail. All real compiler/evidence
inputs are hashed around the test; no hot-call allocation is permitted.

This covers action starts, not reload completion, primary fire, aim, a full SDK
weapon, purchase/HUD/download admission, gameplay or measured performance.
Stamina comes from the existing CSOL ledger. CSOL monster/BOSS AI is retained.
