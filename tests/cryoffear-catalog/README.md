# CoF host symbol catalog

Runs the production MetaHook catalog parser and CRC-based symbol query as x86
C++20. It checks the three native camera globals, the client CRT import and two
native mouse sites against generated metadata. The old missing-operand defect
must fail exactly three checks; the corrected generated catalog must pass all
six. The complete installed catalog is checked separately.

```bash
python tests/cryoffear-catalog/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --client sandbox/cs-client-b/Half-Life/cstrike/cl_dlls/client.dll \
  --catalog sandbox/cs-client-b/Half-Life/cstrike/metahook/gamedata \
  --output build/cof-catalog-check
```

Use a fresh output directory. The generator verifies the exact original PE
identity and instruction operands. The runner seals compiler dependencies and
catalog inputs, without copying sources or game binaries. This proves metadata
parsing and querying, not injected-hook execution, gameplay or performance.
