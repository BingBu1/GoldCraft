# MP5 native sound dispatch

Run `tests/cryoffear-p345-native-effects/run.py --weapon mp5` with the matched private `--sample`, local `--compiler`, `--environment` and a fresh workspace-build `--output`.

The original MP5 vtable must resolve EmptyPlay to the shared original consumer. The fixture executes180 original EmptyPlay/pickup/ammo/sleeve cases against the actual MP5 class and its typed native sender. It also checks loop/tail/stop delivery and owner, edict, private-data and resource-generation cancellation at callback boundaries. MP5's independent sample enum and resource table are used by the shared compile-time implementation.

This does not verify actual playback, the client Studio5004 path, map/Host admission, deployment, multiplayer or live performance.
