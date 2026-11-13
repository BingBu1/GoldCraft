# Revolver SDK composition and original instruction comparison

This fixture compiles the actual ReGameDLL SDK class in `native/guest/cof/server/revolver_weapon.cpp`. It does not register or deploy the weapon, and does not establish gameplay or live performance acceptance.

The private original input is Cry of Fear x86 `hl.dll`, image base `10000000`, SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`. Original ID21 maps to host898. Original sample bytes and disassembly remain private under analysis.

Run from Git Bash with Python/Unicorn and the project's x86 Clang/MSVC environment:

```bash
python tests/cryoffear-revolver-weapon/run.py --sample <workspace-original-hl.dll> --compiler <clang-cl.exe> --environment <compiler-env.json> --output build/<fresh-result-directory>
```

The runner rejects reused/linked output paths, discovers actual compiler include dependencies, hashes inputs before/after compilation and execution, and emits `receipt.json`. Build output contains generated SDK adapters and compiler products, with no source snapshot.

| Comparison | Original instructions | Explicit controlled boundary |
|---|---|---|
| Primary attack | 10064C60 | Previously verified ballistics, recoil, shake, sound, player animation |
| Cylinder reload | 10065270, 10065430, 10065660, 10065760 | Engine event transport, player animation |
| Post/input/aim | 10064320, 1011D560, 10065C10, 10064070, 10063DE0 | Authenticated owner state, metadata strings, model sequence durations |
| Idle/holster/melee | 10065CE0, 10063D10, 100656A0 | Player animation and melee component |
| Deploy | 10063BC0 | Previously verified common DefaultDeploy helper composition |
| Owner cylinder | 100EFB41 through 100EFC5D | GetItem21 and selected owner identity; separate spread updater is outside this fragment |
| World lifecycle | 100657A0 and shared FallThink/Materialize/Drop/Kill | Precache and engine SetModel/SetOrigin/SetSize |

The original SendWeaponAnim instructions execute and preserve owner `pev.weaponanim`; jump exit reads the old view sequence duration before sending14. Cylinder target and actual owner angle remain distinct. Reload commits only after strict `now > deadline`, preserves the original float/double rounding, and never uses a magazine helper.

SDK checks cover real AddToPlayer/AddWeapon/AttachToPlayer/CCSPlayerItem composition, mandatory resource/effect admission, original five-round cylinder stages, cancellation, independent melee/cylinder clocks, native viewmodel handoff, owner/weapon serials, death, active item, model and host-generation retirement. Engine insertion, target damage and transport are abort/controlled edges and are not claimed as accepted integration.

Declared host adaptations are extension identity, authored third-person animation mapping, per-owner spread isolation, lifecycle tokens and a bounded two-clock dispatcher. Holster additionally cancels delayed melee for safe native weapon switching. Game paths perform no dynamic allocation or I/O; no measured performance claim is made.

The host inventory extension additionally snapshots the exact cylinder stage and pending commit, independent think deadlines, body, armor suppression, firing/reload locks and spread. It authenticates map/player/model/entity generations and republishes existing aim/body without starting a new animation. The tests exercise a failed-purchase style detach/rebind/restore during delayed melee and individual insertion, including equality at the original strict commit deadline. Common Deploy suppression covers negative/zero/fractional/one/other/NaN armor values; original DefaultDeploy evidence is kept separately. Host skills exceeding the original five-round cylinder still require a reviewed visual/cylinder policy before runtime admission.
