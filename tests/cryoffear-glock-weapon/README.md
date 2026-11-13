# CoF Glock server lifecycle fixture

`run.py` compiles the actual ReGameDLL subclass with the project's x86 SDK,
C++20/O3/ThinLTO/AVX2 and precise floating point. Original machine-code oracles
first verify the private CoF hl.dll SHA. The suite covers 160 full PrimaryAttack
cases, 180 world lifecycle cases and 105 derived transition cases including
attachment, light switching, holster, sprint, Glock jump sequences and three
loaded/empty Fidget choices. Original timing fields and ordered effects are
compared directly. Shared animation, damage, model and engine edges are explicit
controlled boundaries; separate component tests remain required.

First-person effects follow the human-selected original cooperative mode. The
PrimaryAttack oracle installs the original multiplayer rule table and executes
its actual rules+0x20 function; it does not obtain the same shell-branch result
merely by leaving the rules pointer null. This does not establish the original
own-player world-model drawing or first-person shell lifecycle.

The actual SDK fixture also covers Spawn/AddToPlayer/Attach, extended metadata,
deploy RNG/effect order, reload completion and callback ordering, delayed melee,
ownership recycling and missing backends. Tactical-light stages additionally
run through their actual scheduling APIs with early/equal deadlines, complete
attachment/switch/holster, and owner/death/callback cancellation. No registry or
transport wiring, gameplay, audiovisual fidelity or live performance is claimed.

Run in Git Bash with a fresh workspace build output.
Set `COF_COMPILER` to the local x86 Arkari Clang executable first:

```bash
python tests/cryoffear-glock-weapon/run.py \
  --sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-glock-validation
```

Private samples, original instruction evidence and game models are excluded
from publication. Compiler-discovered inputs are hashed before/after; no source
tree copies are created. Four malformed or incorrect primary oracle inputs
must reject with their specified exit codes.

Scenario reserve grants above CoF metadata capacity (151/500/800/65535/INT_MAX)
exercise actual CanDeploy, Deploy, reload completion, Holster and redeploy.
Original reload clears the old magazine before transferring from reserve; its
150-round metadata is not a current-inventory invariant. Negative reserve still
rejects. See the 2026-11-12 scenario reserve correction report.
