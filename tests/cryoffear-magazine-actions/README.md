# Cry of Fear magazine action differential

Executes the matched original Glock, P345 and VP70 reload-start, secondary-melee
scheduling and idle functions. The original reload helper and model-duration
instructions run inside the emulator; side effects are observed at their ABI
edges. The production C++ implementation is compiled separately in place.

```bash
python tests/cryoffear-magazine-actions/run.py \
  --sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --models '/h/Steam/steamapps/common/Cry of Fear/cryoffear/models/weapons' \
  --ida-bytes analysis/cof-weapons-20261111/root-actions/byte-verification.json \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-magazine-actions-check
```

Requires the user's authorized original DLL/models and private IDA-byte receipt;
none are embedded in this repository. The sample SHA, IDA windows, model hashes,
compiler and production inputs are checked and recorded. The pistol suite
contains 17,334 original-function cases. The shared safety checks also cover
the automatic reload API; the current run has 94 checks. Compilation disables
floating-point contraction, and compiler temporaries stay inside the selected
build directory.

Add `--automatic` to execute the original TMP/MP5 reload-start functions using
their actual view models. Supply the corresponding private IDA proof containing
those two complete windows and the same common helper windows. This mode checks
3,852 original-function cases, including snapshots observed at animation
callbacks, aimed deferral, all input gates, equality and large-time rounding.
It does not emulate automatic fire or its sound latch. Skill-expanded magazines
are separate host safety checks: physical reload must preserve granted rounds
instead of discarding an overfilled magazine.

The revolver and special-state paths are outside this module. This does not
register a weapon, apply actual melee damage, run a full server or establish
gameplay/performance fidelity.
