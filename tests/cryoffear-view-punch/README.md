# Cry of Fear viewpunch

Run run.py with --environment build/csol-season2-trace-20261109/compiler-env.json --sample <private original client.dll> --output build/cof-client-view-punch-20261111. The runner uses actual GoldSrc event_args/ref_params SDK and compiles PE32 with the pinned Clang toolchain. oracle.py verifies the exact original DLL SHA and runs its impulse, CalcRefdef addition stage and decay instructions with Unicorn; original bytes are never distributed.

ViewPunchEvent takes the authenticated event and actual AttachCam entity/mode state. ViewPunchAtStage adds custom angle to a view that already includes engine punch, then updates the independent persistent state. Call only when that original stage executes. No weapon/death clear is inferred. Nonfinite/overflow input is an explicit host rejection with no partial writes. No allocation, resource I/O or mutable global cache is introduced in the view path.

5947 instruction differential cases passed including negative time steps, clamping/thresholds, camera modes and SP/MP360-frame trajectories. Host hook placement, gameplay and CPU performance remain unvalidated.
