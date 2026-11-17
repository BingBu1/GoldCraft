# 870 actual SDK weapon lifecycle

Compile the production `ShotgunWeapon` with the current ReGameDLL headers and
real extracted SDK attach/animation/inventory methods. Frozen original evidence
must precede implementation and match the supplied PE32 CoF MP sample. The
original engine RNG and view transport are controlled while all original 870
fidget instructions run in Unicorn.

From Git Bash in the project root, set `COF_COMPILER` to the verified local
`clang-cl.exe` path recorded in the private toolchain configuration:

```bash
python tests/cryoffear-shotgun-weapon/run.py \
  --sample analysis/cof-weapons-20261111/g43-20261116/reopened/cof-g43-mp.dll \
  --proof analysis/cof-weapons-20261111/shotgun-20261116/sdk-20261117/PREEDIT.json \
  --spread-proof analysis/cof-weapons-20261111/shotgun-20261116/integration-20261117/PREEDIT-crosshair.json \
  --compiler "${COF_COMPILER:?Set the verified clang-cl path}" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-shotgun-sdk-fresh
```

Use a new output directory. The private original sample and frozen evidence are
not distributed in Git. Tests cover attack edges, eight-pellet dispatch, delayed
pump geometry, three reload endings, skill-expanded clips, reserve exhaustion,
switches, same-life model rebind, inventory snapshots, pending ADS reload,
player-scoped shove, cancellation and entity recycling. The no-allocation loop
checks 10,000 owner/weapon frames; it is not live CPU or frame-time acceptance.
Empty, truncated, extra and wrong oracle output must fail.

The original player-frame updater also runs for the 870, independently of its
fixed eight-pellet cone. A sequential original x86 trace checks 640 actual
weapon frames across movement, sprint, crouch, air and owner absence. SDK
checks retain the spread on same-life model rebind/inventory rollback, and
verify the two original DefaultDeploy resets without changing pellet spread.

Ballistics, network/audio effects and target damage are explicit controlled
integration boundaries; their separate original/SDK suites remain required.
This test does not certify a registered factory, purchase, paired client,
deployment, gameplay, multiplayer or live performance.
