# M16 native factory

The native M16 factory uses the shared real GetClassPtr/CCSPlayerWeapon
allocation path. Its pre-edit original proof covers factory 10050AE0, Spawn
100527E0 and the M16 vtable, with the copied original MP identity verified.
The source factory can be compiled without installing a map owner or admitting
purchase. Missing owner/effect backends are not acknowledged as ready.

Set COF_CLANG_CL to the matched x86 compiler and COF_COMPILER_ENV to its saved
SDK environment. From Git Bash:

~~~bash
python -B tests/cryoffear-p345-factory/run.py \
  --weapon m16 \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-m16-factory-new
~~~

Use a fresh output directory, matching private proof and the existing workspace
urban player model. The suite executes actual SDK GetClassPtr, OnCreate,
CCSPlayerWeapon and M16 Precache/Spawn. Cases cover callback replacement,
cancellation, serial reuse, foreign private objects, cold-resource failure,
existing-inventory reuse without resetting skill ammo or reload, and default20.

Engine allocation and prepared resources are explicit controlled boundaries.
This does not establish live entity export lookup, map owner installation,
purchase, pushback, transport, P-model grip, deployment or gameplay.
