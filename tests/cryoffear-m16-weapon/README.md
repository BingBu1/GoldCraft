# M16 actual SDK lifecycle

Build the actual M16 entity against the pinned ReGameDLL SDK and real player
sequence consumers. The private pre-edit proof verifies 42 original instruction
and inventory windows before compilation. Original code/data is not published.

Set COF_CLANG_CL to the matched x86 compiler and COF_COMPILER_ENV to its saved
SDK environment JSON. Run from Git Bash in the project root:

```bash
python -B tests/cryoffear-m16-weapon/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-sdk.json \
  --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" \
  --output build/cof-m16-weapon-new
```

Use a fresh output directory and the workspace sandbox urban player model.
Generated SDK adapters stay in build; production sources are compiled in place.
The receipt hashes all actual compiler dependencies before and after the build.

Checks exercise attack-edge groups, independent burst/push deadlines, inactive
continuation without overwriting the selected weapon's attack gate, owner death
and serial reuse, both fire modes and reload variants, partial pickups, valid
skill-expanded ammunition, model rebinding, inventory snapshots, fidget RNG
ordering and a rejected consumer followed by a successful retry.

Ballistics, position audio, original long-gun pushback targets and message
transport are controlled boundaries. The fixture checks pushback dispatch;
it does not establish target damage, stamina mutation, owner-global replacement
across different weapons, or complete original PostFrame equivalence. Those
remain necessary before factory registration, purchase admission and gameplay.
The original control/shot instruction comparisons are separate tests.
