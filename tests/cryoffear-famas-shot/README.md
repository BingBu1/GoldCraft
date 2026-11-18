# FAMAS ordinary primary attack

Execute complete original x86 `1003BBD0`, actual autoaim and eye-position code,
then compare the typed component's state and every ordered ABI event. The
private pre-edit proof must match the original module, complete code regions,
referenced floating-point constants and reusable consumer evidence.

```bash
python -B tests/cryoffear-famas-shot/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_FAMAS_SHOT_PREEDIT" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-famas-shot-new
```

Use Git Bash, a fresh workspace output directory and the existing Unicorn/x86
compiler environment. Original binaries and disassembly are private inputs.
All actual compiler/evidence dependencies are hashed before and after running.

The original comparison uses damage15 without modifying original instructions.
Host-only cases separately verify configured damage. Coverage includes actual
0.06-second double-to-float cooldown, last-shot override, secondary cooldown,
underwater versus empty-magazine behavior, automatic reload, fixed hip4/aim17
animation, recoil RNG/order, crouch/standing shake, spread recovery, extended
magazines, infinite ammo, sound origin and current post-recoil shot angles.

Host failure cases interrupt each real callback. Retired authority cancels;
missing geometry/recoil/damage stops the operation explicitly. Optional
presentation/audio failure completes the mechanical shot and reports
`FiredWithoutPresentation`. Missing empty-reload support cannot disable loaded
shooting. Missing presentation and invalid optional shell data cannot suppress
valid authoritative bullets. No hot-call allocation is allowed by this fixture.

Four malformed result fixtures and a deliberately stale pre-recoil angle read
must fail. These checks cover an ordinary attack component, with controlled
engine/damage/audio edges. They do not prove a complete SDK weapon, purchase,
deployment, actual effects, multiplayer or measured game performance.
