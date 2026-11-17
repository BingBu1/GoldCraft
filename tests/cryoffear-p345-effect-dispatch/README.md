# Shared Glock/P345 effect dispatcher

Compiles the actual unified effect dispatcher and native packet senders/codecs.
Native engine callbacks capture real packet writes; the fixture checks one
monotonic per-player presentation stream across Glock → P345 → Glock → release,
P345 crosshair/particle identity, direct shell parameters, muzzle/recoil event
fields, PlaySFX fire key, view-animation bounds and inventory updates.

```bash
python tests/cryoffear-p345-effect-dispatch/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-p345-effect-dispatch-validation
```

Wrong weapon/resource identity rejects. Retirement during MESSAGE_BEGIN still
finishes the packet from its snapshot and reports cancellation. Canonical owner
state and actual player models are real SDK composition; native animation and
inventory notification callbacks remain explicit controlled edges. Captured
packets do not prove network delivery, client rendering or gameplay.

870 uses the same actual dispatcher runner with the matched original MP/client
samples and pre-edit evidence:

```bash
python -B tests/cryoffear-p345-effect-dispatch/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --sample analysis/cof-weapons-20261111/g43-20261116/reopened/cof-g43-mp.dll \
  --client-sample analysis/cof-weapons-20261111/samples/client.dll \
  --g43-proof analysis/cof-weapons-20261111/g43-20261116/reopened/PREEDIT-effects.json \
  --g43-aurora-proof analysis/cof-weapons-20261111/g43-20261116/reopened/PREEDIT-effects-aurora.json \
  --shotgun-proof analysis/cof-weapons-20261111/shotgun-20261116/integration-20261117/PREEDIT-effects.json \
  --output build/cof-shotgun-effect-dispatch-validation
```

870 scenarios exercise the actual gun, two aim messages, type2 shell packet,
attachment1 smoke, ShotState5 and lifecycle/endpoint recovery. Native audio,
reload owner and target damage are controlled boundaries, not gameplay evidence.
