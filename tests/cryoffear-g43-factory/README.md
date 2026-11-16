# G43 actual native factory

```bash
python -B tests/cryoffear-p345-factory/run.py --weapon g43 --sample <copied-original-MP> --compiler <clang-cl.exe> --environment <compiler-environment.json> --output build/<fresh-directory>
```

The runner verifies the original factory and virtual-table byte regions and immutable pre-edit capture, then compiles actual SDK GetClassPtr/OnCreate/CCSPlayerWeapon creation with the production typed factory and G43 subclass. Cases cover initial allocation without Spawn, repeat identity with pending state preserved, invalid/incomplete hosts, callback retirement, reentry, edict recycling and actual Spawn metadata. Engine allocation callbacks are controlled; this does not install a production map factory, enable purchases or prove gameplay/multiplayer/performance.
