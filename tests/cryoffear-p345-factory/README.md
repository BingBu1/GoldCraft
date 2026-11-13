# CoF P345 native entity factory

The fixture compiles the actual P345 subclass, SDK GetClassPtr, OnCreate and
CCSPlayerWeapon wrapper with the project's x86 C++20/O3/ThinLTO/AVX2/precise
configuration. It covers ownerless precache, duplicate acquisition without
state reset, native wrapper identity, missing callbacks, callback reentry,
entity retirement/recycling and normal world Spawn.

Engine private allocation and unrelated gameplay calls are explicit boundaries.
This checks native entity construction; it does not establish shop admission,
actual resource transport, complete runtime wiring, gameplay or live performance.

Run in Git Bash with a fresh workspace build output:

```bash
python tests/cryoffear-p345-factory/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-p345-factory-validation
```

Generated SDK adapters and compiler-discovered input receipts remain in build.
No source tree copy, original game resource or toolchain environment is published.
