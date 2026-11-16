# CoF flinch activity

Executes the complete original MP `0036b91c` helper at VA `10089C80`, including its jump tables. Compares the actual C++ selector, model lookup order, exact `-1` fallback and missing-activity sentinel. MakeVectors and LookupActivity are controlled external edges; this does not validate their engine implementations or the later monster schedule.

Run from Git Bash with workspace copies of the original sample and pre-edit evidence:

```bash
python -B tests/cryoffear-flinch-activity/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-flinch-activity.json \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-flinch-activity-fresh
```

Set COF_CLANG_CL to the configured x86 compiler and COF_COMPILER_ENV to its saved SDK environment. The runner checks original byte/evidence identities and all discovered compiler dependencies before and after. It rejects empty/truncated/wrong/extra fixtures, covers backend cancellation/failure without publishing a fabricated activity, and checks 10,000 calls for allocations. These are component checks, not game performance or gameplay acceptance. Original binaries and disassembly remain private.
