# CoF Glock native inventory round trip

This fixture runs the actual ReGameDLL inventory insertion/removal and weapon-box
packing bodies with the production CoF Glock subclass. It covers empty, ordinary
and host-granted magazines (0, 1, 14, 15, 28 and 75 rounds), duplicate box packing,
owner rebinding, ammunition conservation, reloading and firing after reacquisition.
Firing retains the human-selected original cooperative presentation branch.

The engine/effect consumers and game-rule policy are explicit controlled edges.
Automatic weapon switching aborts if reached. The fixture does not execute the
weapon-box Touch loop by default. With `--full-touch`, the complete actual SDK
Touch body runs with explicit rule/engine boundaries, including air, spectator,
death, VIP, shield, restriction and occupied-slot rejection. Unsupported clients
must preserve the packed extension, and a later supported touch can acquire and
fire it. A mixed box also preserves the extension while the native list code
transfers a stock pistol through its explicitly controlled item consumer.
Hookchains, collision, packet transport and real gameplay remain separate.
It is not evidence of visual fidelity, multiplayer correctness or live performance.

Run in Git Bash with a fresh output directory and the workspace x86 toolchain:

```bash
python tests/cryoffear-glock-pickup/run.py \
  --sample analysis/cof-weapons-20261111/server-evidence/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-glock-pickup-validation
```

Add `--full-touch` to run the complete box entry point. C4, grenade, Bot and
automatic-switch paths abort if reached; they are outside this Glock fixture.

The runner checks the original sample identity, generates SDK adapters directly
from the existing source tree and hashes all discovered inputs before and after
compilation/execution. Generated adapters and receipts remain in build; no source
snapshot, original binary, game resource or compiler environment is published.
