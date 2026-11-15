# CoF scene rejection and firearm admission

This suite executes the production scene and admission translation units with
the real point-light renderer in a hidden OpenGL 4.4 Core context. Other
resource, menu and effect services are controlled boundaries.

```sh
python tests/cryoffear-scene-admission/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --output build/cof-scene-admission-check
```

Cold preparation alone must not advertise firearms: the backend must first
accept a real opaque frame. Once validated, a missing, linear, auxiliary-world
or over-capacity frame must skip its draw without withdrawing all five firearm
capabilities. Ten thousand alternating accepted/rejected frames must produce
no new userinfo writes or cold preparations. Wrong thread/context, resource or
connection replacement and actual scene retirement remain rejected.

The pre-fix production source fails six assertions. Each run seals its actual
compiler input closure before and after compilation/execution. This establishes
the host adapter's lifecycle behavior, not original gameplay fidelity, live
network behavior or performance. Original allocation/lifecycle evidence and
the captured live cancellation are documented in the linked report:
[CoF scene lifetime repair](../../docs/2026-11-15_修复-CoF场景波动撤销武器权限-report.md).
