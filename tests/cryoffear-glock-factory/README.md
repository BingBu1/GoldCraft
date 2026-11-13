# Actual SDK Glock factory lifecycle

This fixture compiles `guest_cof_factory.cpp`, the real ReGameDLL `GetClassPtr<CCSPlayerWeapon>` template, actual wrapper construction, the SDK `CBaseEntity::OnCreate` implementation and the production Glock class. It reuses the existing actual SDK Glock animation/world composition harness.

```bash
python tests/cryoffear-glock-factory/run.py --compiler <clang-cl.exe> --environment <compiler-env.json> --output build/<fresh-directory>
```

The engine private allocator is a controlled zeroed allocation boundary. Five wrapper gameplay virtual methods abort if called; the test only claims actual wrapper construction/type/ownership. Original Cry of Fear weapon mechanics are separately verified by `cryoffear-glock-weapon`; this suite verifies the required host factory adaptation and does not run an original DLL oracle.

Coverage includes temporary factory→Precache→GetItemInfo without Spawn, normal factory→Spawn, repeat acquisition preserving cooldown/reload/ammo, actual CCS wrapper identity, missing providers, reentrant installation/reset, callback-time entity/host retirement, private-data replacement, wrong private class, mismatched entvars, entity reuse and broken wrapper ownership. Unused SDK virtuals remain generated abort boundaries.

The runner discovers and seals all real compiler include/source dependencies before and after compilation/execution. Generated SDK adapters and concise receipts stay under build; no source snapshot, game asset or original binary is published. Registration, shop admission, deployment, full gameplay and live performance remain outside this fixture.
