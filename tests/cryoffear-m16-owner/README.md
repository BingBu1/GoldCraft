# M16 canonical owner and independent action life

Compose the production M16 owner view with the real canonical Glock owner/runtime,
the production action-life runtime and actual stock/CSO player models. Engine-stage,
sound, HUD and target-damage callbacks are controlled boundaries. This fixture does
not install a map host or assert whole-gun gameplay.

From Git Bash in the repository, with private matched samples/evidence and an
installed x86 compiler environment:

```bash
python -B tests/cryoffear-p345-owner/run.py --weapon m16 \
  --sample "$COF_MP_SAMPLE" --proof "$COF_M16_OWNER_PROOF" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output "$COF_FRESH_BUILD"
```

The output must be a new workspace `build` directory. The runner verifies the
sample, 55 original code/data regions and archived IDA proof identities before
compilation. All transitive inputs are sealed before/after execution; generated
SDK adapters remain in the output, with no production-source snapshot.

Coverage includes input edges/jump/random seed, real-life stamina changes,
matching player/map/life checks, cross-player rejection, transactional cold
preparation failure, preservation of pending player tasks across model changes,
rebinding after death, stale output protection and every action/stance on three
actual models. Ten thousand reads must allocate nothing and perform no model
lookup. That allocation regression is not a live CPU/FPS measurement.
