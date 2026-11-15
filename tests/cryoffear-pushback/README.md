# Original CoF long-gun pushback

Execute the full original player callback at 100E8B40 and compare its event
order, target arguments and stamina bits against the shared production core.
The original engine sphere wrapper, MakeVectors thunk, monster-pointer and
alive functions execute as original instructions. Sound/recoil, gun position,
engine enumeration and target damage are explicit controlled boundaries.

Set COF_CLANG_CL to the matched x86 compiler and COF_COMPILER_ENV to its saved
SDK environment. From Git Bash in the project root:

~~~bash
python -B tests/cryoffear-pushback/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-pushback.json \
  --compiler "$COF_CLANG_CL" \
  --environment "$COF_COMPILER_ENV" \
  --output build/cof-pushback-new
~~~

Use a fresh output directory. Private original samples, raw instructions and
pre-edit proofs are not published. Python Unicorn and the compiler environment
must already be available.

Cases cover zero/negative/low stamina, infinite stamina, engine order including
self, non-monsters, every deadflag, target damage returning zero or one, and
changes to stamina/infinite mode inside original callbacks. The stale entry
snapshot negative must fail. Every host callback is cancelled and rejected;
subsequent valid calls remain usable. Ten thousand calls check zero allocations.

The production core is independent of an active weapon binding. The host still
must implement the original player-owned delayed task, authoritative stamina,
target identity leases, the quiet-audio wrapper and native damage translation.
originalBits describes CoF semantics and must never be passed directly to CS
TakeDamage. This fixture does not certify SDK monster flinch/force, complete
M16/TMP integration, weapon admission, deployment, gameplay or live performance.
