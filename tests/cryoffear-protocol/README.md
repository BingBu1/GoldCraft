# Cry of Fear host presentation transport

Compiles the real catalog, authority, presentation service and 24-byte state
codec as x86 C++20. The 65,676 checks exercise round trips, malformed/version/reserved
fields, all four original pistol identities, scope retirement and stale/replayed
session, life, instance and revision packets, global sound dispatch without
equipment authority, all signed coordinate words, and the 192-byte sound limit.

```bash
python tests/cryoffear-protocol/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-protocol-check
```

`CoFWpnState` and `CoFSFX` are explicit GoldCraft transport adaptations. The sound
codec accepts prequantized signed eighths and borrows its decoded string for
synchronous dispatch; it does not infer the original server's rounding. The
179-byte sample limit follows the host's message bound and differs from the
original reader. Actual sound-message hookup remains separate work. The host IDs,
namespaces, prices and buy placement are host authoring choices. This suite
does not register the catalog or prove original CoF network compatibility,
gameplay, resource availability or runtime performance.
