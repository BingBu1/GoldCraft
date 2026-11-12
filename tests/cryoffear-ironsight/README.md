# Cry of Fear ironsight host adapter

The adapter suppresses the ordinary CS client's viewmodel bob scalar while an authenticated CoF guest is aimed. It preserves the native camera bob, clock advance, shake, FOV offsets, stairs and smoothing. It does not implement an unproven xyz translation or the complete CoF bob/idle/lag pipeline.

Original CoF client SHA-256: d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00. IronSights handler 100709A0 writes enabled 1018C9F4; bob 100AD9D0 updates its state before enabled clears its two bob scalars, and enabled also suppresses idle. Lag 100ADEC0 returns immediately when enabled. The three message coordinates have no established transform consumer.

Matched host client SHA-256: b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2. Normal refdef 1006AB20 contains the VM bob load at 1006B67C. The symbol GoldCraftCof_ViewmodelBobLoad is a patch with RVA 0x6b67c. Installation verifies f3 0f 10 74 24 2c. The ordinary branch executes the original trampoline; the aimed branch loads positive zero into xmm6 and continues at 1006B682. The original host function has no CoF lazyweapon lag stage.

Initialize on the client owner thread after the matching catalog is available. Feed SetGuestCofIronsight only authenticated current guest ownership/aim state. Reset on session/ownership/death/video lifecycle retirement. Enclose only downstream V_CalcRefdef in GuestCofIronsightViewScope. Shutdown before removing the remaining client hooks. Failed symbol/byte checks leave this adapter inactive and must be reported by integration.

Run from Git Bash:

    python tests/cryoffear-ironsight/run.py --environment build/resource-lifecycle-engine-20261108-inputs/environment.json --commands build/resource-bridge-20261108/csol/compile_commands.json --output build/cof-ironsight-<new-id>

The runner compiles the actual production adapter and tests against the real x86 SDK using O3/ThinLTO/AVX2 and precise floating point. The test executes its actual naked thunk and checks scalar bits, stack/flags, state/view gates, nesting, owner thread, reset, shutdown and reinitialization. It does not emulate successful MetaHook installation or prove live gameplay/FPS. No per-frame allocation, resource lookup or I/O is added; live CPU cost is still unmeasured.
