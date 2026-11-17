# 870 canonical input, shared stamina and reload clock

```bash
python -B tests/cryoffear-p345-owner/run.py --weapon shotgun \
  --sample "$COF_MP_SAMPLE" --proof "$COF_SHOTGUN_OWNER_PROOF" \
  --compiler "$COF_COMPILER" --environment "$COF_COMPILER_ENVIRONMENT" \
  --output build/cof-shotgun-owner-fresh
```

Set those variables to the original MP, frozen pre-edit owner proof and verified
local toolchain inputs. Original binaries, models and private evidence are not
distributed in Git. The output directory must not exist.

This composes the production owner view, canonical native input runtime and
player-scoped action runtime with three real stock/CSO models. It checks the
five typed reload callbacks: start/insert retain the original one-second player
clock, while each finish uses the cold-bound host Shotgun track's duration.
Standing/crouching, equality expiry, native activity priority, saved state,
optional unavailable stamina, rejected cold requests, per-player isolation and
life retirement are covered. Hot reads and reload queries allocate no memory
and do not query model files.

The external authority/stamina ledger is controlled; actual map hooks,
transport, resource authoring, deployment, gameplay and live performance remain
separate validation requirements. This does not introduce CoF AI or a second
stamina recovery cycle.
