# Cry of Fear client bridge checks

Builds the production MetaHook message/FOV bridge, presentation codec/service,
the actual session implementation, and the plugin translation unit against the
real x86 SDK. Only the unrelated HUD owners and engine callbacks are controlled.
No game installation is modified or launched.

Run from the repository in Git Bash, using the locally generated compiler
environment and an actual CSOLSupport CMake compilation database:

```bash
python tests/cryoffear-client-bridge/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --output build/cof-client-bridge-check
```

The 126 checks cover packet delivery to the actual exported callback, native
output ordering, explicit release to CS scopes, replay/previous-life rejection,
same-life interpolation across weapon instances, invalid settings recovery,
map and same-map resource-session reset, empty-overlay reconnects through the
typed connection-generation provider, invalid providers and shutdown, cached cvar access, duplicate
initialization, and preservation of another message consumer on collision.

The runner records the real compiler-discovered include closure before and
after compilation, tool identities and the executable hash. It retains failed
build logs. This is not a full DLL link, visual or input validation, sensitivity
integration, gameplay, or measured runtime performance acceptance.
