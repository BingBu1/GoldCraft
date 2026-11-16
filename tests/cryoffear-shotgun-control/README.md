# CoF 870 reload and pushback controls

Run from the project root directly in Git Bash. Supply the matching original
CoF MP sample, the private proof directory, an x86 clang-cl compiler and its
INCLUDE/LIB environment JSON:

```bash
python tests/cryoffear-shotgun-control/run.py \
  --sample "$COF_SAMPLE" --proof "$COF_PROOF_ROOT/PREEDIT-control.json" \
  --compiler "$COF_COMPILER" --environment "$COF_COMPILER_ENV" \
  --output build/cof-shotgun-control-new
```

The original SHA is `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.
The oracle executes complete original reload, insert and pushback functions plus
two explicitly bounded frame subgraphs. It compares callback order, observed
state and bit-exact deadlines, including empty/full/interrupted completion and
callback-driven changes. Engine animation/scheduling boundaries are controlled.
The runner records the real transitive dependency closure and malformed-input
negative checks. This does not validate a complete SDK weapon or gameplay.

The pure component preserves the original five-round reload comparison. A future
SDK owner must prevent an ordinary reload from reducing or trapping a magazine
expanded by CSOL skills. Shared CSOL stamina supplies pushback; no CoF monster AI
or separate recovery cycle is installed here.
