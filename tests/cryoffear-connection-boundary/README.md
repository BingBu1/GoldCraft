# Connection authority boundary checks

The production GoldCraft connection observer advances at the existing verified
ClearState, Disconnect and direct reset entries before the original engine or
resource cancellation can run. It does not add an engine hook or depend on an
overlay, resource manifest or resource-refresh feature.

```bash
python -B tests/cryoffear-connection-boundary/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/client2/compile_commands.json \
  --output build/connection-boundary-new
```

The runner compiles the actual production TU with the real x86 SDK. Its
fixture directly includes that TU and exercises actual clear/disconnect/reset
entries with controlled engine trampolines. Both resource-feature macro values
are compiled and run. Empty and populated public overlay Snapshot states are
owned by the resource callback; no filesystem implementation or resource owner
is required. It checks retirement before native callbacks/cancellation,
longjmp, nested clear, unavailable/failed-hook capability gates, owner-thread
reads, monotonic generation, terminal exhaustion and final invalidation.

An additional test DLL named GoldCraft.dll verifies the public header using
real Win32 module/export lookup: unavailable module, invalid size/version/null
function rejection, successful retry and a cached valid API. That small test
DLL is not the actual GoldCraft DLL.

Only an unpublished working tree which adds resource-owner calls to precache
needs `--resource-owner-wip`. It explicitly links that extra real owner TU;
the published baseline and connection-only change require no such dependency.
The private review also compiles Git HEAD plus only the connection hunks from
memory through compiler stdin, keeping unrelated resource changes excluded.

Consumers query `GoldCraftConnectionInterface()` during cold initialization,
cache its typed API and compare nonzero opaque `generation()` tokens before
processing authority or frames. Zero is unavailable/exhausted/finally retired;
never revive prior authority from zero. Nested cleanup can advance more than
once. Calling the final `invalidate_connection_observer()` is plugin teardown,
not ordinary map/video initialization. The observer makes no heap allocation,
I/O, atomic operation or lock; the read checks its Win32 owner thread.

These checks do not execute a live engine disconnect, link the full GoldCraft
DLL, deploy resources, verify gameplay or establish real CPU/frame performance.
