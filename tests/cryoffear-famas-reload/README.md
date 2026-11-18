# FAMAS reload completion

Compare the production completion helper with the original ID23 branch at
`1011D91F..1011D9B6` and its actual weapon-clock instructions. The private
pre-edit proof must match the authorized original CoF module and IDA archives.

```bash
python -B tests/cryoffear-famas-reload/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_FAMAS_RELOAD_PREEDIT" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-famas-reload-new
```

Use Git Bash, the existing Unicorn/x86 compiler environment and a fresh output
directory. Original bytes stay private. Real compiler/evidence inputs are
hashed before and after running; malformed comparison fixtures must fail.

The replay seeds the actual original FAMAS ID23/capacity30 runtime registry.
Coverage includes every physical clip count, negative/zero/large reserve,
loading, item flags and exact earlier/equal/later clock boundaries. The original
fills capacity without reading/debiting reserve or notifying the ordinary
reload observer. The test fails if that observer is called.

Host cases preserve skill grants above capacity, finish a pending reload only
once and leave invalid inputs unchanged. This is an arithmetic/state component,
not the complete common frame, SDK weapon, purchase, deployment, multiplayer or
measured performance acceptance.
