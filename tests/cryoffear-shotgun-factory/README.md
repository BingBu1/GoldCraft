# 870 actual native factory

```bash
python -B tests/cryoffear-p345-factory/run.py --weapon shotgun \
  --sample "$COF_MP_SAMPLE" --compiler "$COF_COMPILER" \
  --environment "$COF_COMPILER_ENVIRONMENT" --output build/cof-shotgun-factory-fresh
```

Set the three environment variables to the private, SHA-verified original MP,
compiler and compiler environment. The output directory must not exist.

The runner verifies frozen original factory/vtable evidence and compiles the
real ReGameDLL GetClassPtr/OnCreate/CCSPlayerWeapon path with the production 870
class. Tests cover repeated entity identity with pending shell state preserved,
factory retirement, invalid hosts, reentry, entity recycling and actual Spawn.
Engine allocation is controlled. No map factory installation, purchasing,
deployment or gameplay/performance acceptance is implied.
