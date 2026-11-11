# CoF magazine model preparation

Validates cold extraction of original sequence durations from complete Studio
v10 models. The actual SDK header establishes field offsets. Each duration is
compared bit for bit with the unchanged, SHA-pinned original `1000F390` function,
including all sequences in the user's four pistol models and frame-count
conversion boundaries. Malformed tables, missing sequences and invalid timing
must leave the previous prepared output intact.

Run from Git Bash with a fresh output directory:

```bash
python tests/cryoffear-magazine-model/run.py \
  --sample analysis/cof-weapons-20261111/samples/hl.dll \
  --ida-bytes analysis/cof-weapons-20261111/root-actions/byte-verification.json \
  --models '/h/Steam/steamapps/common/Cry of Fear/cryoffear/models/weapons' \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-magazine-model-check
```

Original modules, models and disassembly are private inputs and are not
distributed. This does not authenticate a whole weapon, activate a factory,
establish rendered animation fidelity or measure live performance.
