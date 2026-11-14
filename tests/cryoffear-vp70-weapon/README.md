# CoF VP70 server lifecycle fixture

This fixture compiles the actual ReGameDLL weapon subclass and player animation adapter using x86 SDK headers, C++20, O3, ThinLTO, AVX2, precise floating point and disabled FP contraction. Unreached SDK virtual bodies abort. Actual SDK sequence/reset, item metadata and pickup/attach bodies are exercised with a workspace player model.

The private oracle requires the original x86 hl.dll SHA-256 before executing 320 complete Primary/Shoot cases, 180 world lifecycle cases, 279 Fidget cases, 90 derived Post/aim/Holster cases and six common Post call-order runs. Ballistics, resource metadata, model timing, audio and engine calls are explicit controlled edges. Private original instructions and game assets are not publication inputs.

The comparisons retain group-only event dispatch, independent Shoot eligibility, exact floating-point timers, shot latch, shell vectors, recoil/shake/view order, clip/infinite-ammo state, burst scheduling and the empty-click branches. Fidget gates aiming before RNG, consumes choice with an empty clip without a view sequence, and changes no timers. Full Post covers jump and sprint entry/loop/exit, action2 reset and aim toggle; common Post verifies empty flag, Fidget, wear, reserve HUD and reload notification order.

Actual SDK checks cover cold preparation failure, every absent or empty melee sample, missing effect consumers, metadata, AddToPlayer/AddWeapon/AttachToPlayer, both reserve limits, first-draw sequences, sleeve RNG, reload equality and notification while loading, pending aimed reload, strict sprint deadlines, player/weapon recycling, death and ownership invalidation. Holster preserves aim and both original callbacks: the three-shot continuation can fire while another item is active without changing its owner attack gate. Both public action orders exercise simultaneous pending burst and player-melee deadlines through the host's single weapon think slot.

Four malformed oracle inputs must fail. Compiler-discovered inputs are hashed before and after, without source snapshots. Run from Git Bash with a fresh output directory; set COF_COMPILER to the local x86 Arkari Clang executable:

```bash
python tests/cryoffear-vp70-weapon/run.py \
  --sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-vp70-validation
```

Host extended ID899, HUD slot1/position3 and per-player spread state are authored CS adaptations; original ID19 is never used as a CS table index. Registry, authenticated owner producers, real effects/transport and native WorldTouch inventory insertion remain integration work. Unsupported special states and original source overrides remain rejected. This fixture establishes no deployment, gameplay, visible/audio fidelity or measured live performance acceptance.

The current fixture also covers trusted scenario reserves 181/500/65535, three-shot continuation, a skill-expanded 90-round clip, reload with 500 reserve and an authority refill during pending reload. These are explicit native-CS inventory adaptations; metadata and original carry/pickup rules remain unchanged. The old reserve upper-bound rejection is preserved as a negative receipt before the fix.
