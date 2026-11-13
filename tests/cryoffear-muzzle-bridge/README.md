# CoF actual muzzle bridge

Build the production namespaced event bridge and original emitter with the matching MetaHook SDK and original P-model. The parser requires the exact original model hash. Scene readiness and engine services are doubles. Tests cover actual header/model divergence, valid owner and active weapon, transformed attachments, exact shell parameters, once-only consumption, cancellation and resource/thread lifetime.

```bash
python tests/cryoffear-muzzle-bridge/run.py --environment ENV_JSON --commands COMPILE_COMMANDS_JSON --model PRIVATE_GLOCK_P_MODEL --output build/cof-muzzle-sdk
```

Supply real local paths for uppercase arguments. This is component evidence, not full scene/gameplay/performance acceptance.

Optional `--p345-model` requires the matching original P345 P-model hash and adds
its type7 world/local callback checks. Type7 world effects do not emit Glock's
type3 secondary shell. Both share the original single overwrite latch, while
their actual model/header/index/resource identities remain separate. Missing
P345 assets leave Glock readiness intact. Model/attachment preparation is cold.
