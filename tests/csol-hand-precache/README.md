# Explicit hand donor precache

Builds the current complete production loader, dependency collector and cache with matching ReHLDS SDK headers. Only filesystem/model registration and unused engine services are explicit boundary doubles. No source snapshots are generated.

```bash
python tests/csol-hand-precache/run.py --environment ENV_JSON --output build/hand-precache-sdk
```

Use an actual compiler environment JSON and a fresh output. Covers cold/warm discovery, explicit/default donors, changed config, invalid and missing resources, texture-only companions, donor cycles, deferred registration and cache memory accounting. Component checks do not prove full engine, HTTP, gameplay, visuals or performance.
