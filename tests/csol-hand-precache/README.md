# Explicit hand donor precache

Builds the current complete production loader, dependency collector and cache with matching ReHLDS SDK headers. Only filesystem/model registration and unused engine services are explicit boundary doubles. No source snapshots are generated.

```bash
python tests/csol-hand-precache/run.py --environment ENV_JSON --output build/hand-precache-sdk
```

Use an actual compiler environment JSON and a fresh output. Covers cold/warm discovery, explicit/default donors, changed config, invalid and missing resources, texture-only companions, donor cycles, deferred registration and cache memory accounting. Component checks do not prove full engine, HTTP, gameplay, visuals or performance.

The fixture also compiles the exact automatic model-sound collector bodies from current ReHLDS. CoF pistol Studio5004 dependencies follow the actual local audio catalog. It checks mapped and unknown samples, unaffected ordinary models/events, the preserved public collector wrapper, and rejection when a required mapped file is absent. The generated include is a narrow SDK adapter; source trees are not copied.

For the original 870 model, add `--shotgun-root PRIVATE_AUTHORED_PACK --original-client PRIVATE_CLIENT_DLL --shotgun-proof PRIVATE_PREEDIT_JSON`. All three paths are required together. The runner verifies the original client/model identities and the three IDA byte regions before compiling. This mode loads the actual authored resources, checks all seven original Studio5004 events and their six existing WAV dependencies, then exercises cold/warm cache reuse and disappearance of a required WAV. A minimal fixture donor activates the full collector; this does not test hand retargeting or rendering.

Only the exact original 870 draw cue is classified as originally missing. Other models, events and unknown samples keep their required dependency checks; the public unnamed native collector keeps its original fallback. No substitute audio asset is created.
