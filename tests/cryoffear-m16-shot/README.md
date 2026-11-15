# M16 per-shot instruction comparison

Execute the entire original `CM16::Shoot` at `10052120`, including the actual
forward-copy autoaim and `.05F` scheduler helpers. Engine, animation, bullet
and cooperative sound consumers are controlled at their ABI boundaries.
State is compared at each callback, including ammo, spread and pending burst.

Set `COF_CLANG_CL` to the matched compiler and `COF_COMPILER_ENV` to its
saved SDK environment JSON. From Git Bash in the project root, with the private
original evidence available:

```bash
python -B tests/cryoffear-m16-shot/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-shot.json \
  --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" \
  --output build/cof-m16-shot-new
```

The output directory must be fresh. Original bytes are neither embedded nor
published. The private proof pins the sample, five IDA instruction windows and
archived analysis before implementation. Python Unicorn and the matched x86
compiler environment are required.

Cases cover single/burst, hip/aimed, empty/water, legal large magazines, existing
pending thinks, future group deadlines and precise floating-point boundaries.
A deliberately stale pre-recoil angle snapshot must fail the original
comparison. All external callbacks are separately cancelled/rejected; rejection
does not revoke weapon admission. Failed scheduling does not commit a new
typed pending callback.10000 calls also check zero dynamic allocations.

These checks do not establish the actual owned-think lifecycle, server damage
policy, SDK integration, complete gun gameplay or live performance. Those remain
required before admitting M16 and claiming delivery.
