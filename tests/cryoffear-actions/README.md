# CoF host player-action bindings

This tests the custom host adapter, not original Cry of Fear player animation behavior.
Read the actual ReGameDLL `LookupSequence`, `GetSequenceDuration`, `GetSequenceInfo`
and matching character descriptors before choosing labels. There is no automatic
weapon-to-action mapping and no `PLAYER_ATTACK2` or sequence-zero fallback.

Run directly from Git Bash:

```bash
python -B tests/cryoffear-actions/run.py \
  --compiler /path/to/x86/clang-cl.exe \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-actions-<new-receipt>
```

The runner pins full SHA-256 identities of real urban/csol95/csol168 resources and
reads them in place. It compiles the production module as x86 C++20 with Arkari,
O3/ThinLTO/AVX2 and precise floats. Receipts record input hashes before/after,
compiler/environment identities, revision and separate compilation/test results.
No copied source or game assets are created.

Coverage includes actual standing/crouching pistol and extended labels, SDK float
timing, male/female index reversals, missing guilotine and pistol shoot2, host
case comparison and first duplicate match, malformed inputs, failed-bind cache
invalidation, map/model generations, source lifetime and allocation/comparison
counts over 200,000 hot selections. This is not a real-game performance sample.

The authoritative host must supply its actual configured `Q_stricmp`, fully
validate the model before binding, advance nonzero generations on map/session
and model/role changes, and validate owner/life/generation again when applying a
selected result. The cache is game-thread-only and owns numeric results only.
Integration, network lifecycle, weapon animation suitability and live gameplay
remain separate validation work.
