# CoF pistol ballistics differential

Run from the project root through Git Bash, with Python Unicorn and an x86 clang-cl environment:

    python tests/cryoffear-ballistics/run.py --sample <private-original-hl.dll> --compiler <clang-cl.exe> --environment <compiler-environment.json> --output build/<fresh-directory>

The private original must have SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63. No original binary, game resources, or decompiled source belong in publication. The environment JSON supplies matching INCLUDE/LIB/LIBPATH. Output must be a new workspace build directory. Temporary compiler data stays there.

The oracle executes original x86 function 0x10088CA0 and its original random, trace wrapper, water, bubble, decal and multi-damage helpers. Only engine and virtual entity boundaries are controlled. The C++ fixture compiles the production adapter against actual ReGameDLL SDK types and the upstream random table/helper. It compares bit-exact returned spread, trace vectors, damage flags/order, point-content queries and all effect packet fields across 512 cases: aim/no aim; zero and positive damage; zero through four successive player hits; world/object/miss; three water modes; varied vectors and seeds.

Compile with precise floating point AND contraction disabled (-ffp-contract=off). Precise alone permits FMA and produces a one-ULP endpoint mismatch. Keep the original separate multiply/add order. The runner discovers actual transitive headers using the compiler and hashes the closure before/after, together with explicit source inputs and the compiler. It also checks empty/truncated case files, incorrect results, and trailing expected data are rejected.

Additional actual-SDK checks cover cancellation at clear/trace/damage/apply/content/message boundaries, balanced packets after cancellation, recycled owner/target serials, invalid preflight with no effects, and a genuine recursive adapter call restoring outer multi-damage. An entered SDK ApplyMultiDamage callback completes its own TakeDamage; cancellation is observed when that callback returns.

Integration must preflight before ammo/recoil/animation, supply the current owner/life/map/weapon generation guard, and prepare real customdecal/bubble resource IDs. The required original cooperative player policy retraces past the first player then directly damages that player when MyMonsterPointer is non-null, including TakeDamage(0); it skips up to two further players before final TraceAttack. It is not a blanket harmless-player skip policy.

The normal muzzle path is the implemented scope. Original owner+0x390 EHANDLE/active-weapon override, CoF Doctor-boss classes/statistics, other bullet types and multi-pellet weapons remain outside it. Unsupported source mode is rejected explicitly. This controlled-edge differential does not establish full scenario damage integration, actual client customdecal rendering, DLL linkage, deployment, multiplayer, gameplay or live CPU performance. The production path uses bounded stack state without allocation, I/O, cache growth or extra threads; no performance gain is claimed without real sampling.
