# CoF client presentation service

This suite compiles the actual `native/guest/cof/client/presentation.cpp`, its
header, and the original FOV helper. No host or alternate presentation model is
substituted. It runs lifecycle/authority/backend failures and reuses the private
original x86 FOV instruction oracle through the presentation entry point.

```bash
python -I tests/cryoffear-client-presentation/run.py \
  --sample /path/to/private/client.dll \
  --environment /path/to/existing/x86-environment.json \
  --output build/cof-client-presentation-validation
```

The DLL identity and original FOV instruction ranges are pinned by
`tests/cryoffear-aim/oracle.py`. The suite does not distribute original bytes.
Compiler outputs/receipts stay in `build`, actual sources stay in `native` and
`tests`, and compilation records Git revision plus identical before/after input
hashes. Clang x86 C++20/O3/ThinLTO/AVX2/precise are required.

Host contract:

- Decode/authenticate the versioned `CoFWpnState` transport, establish
  session/life/generation with `BeginAuthority`, then feed typed snapshots.
  Message version, ordering, sequence replay and FOV seed are the root bridge's
  responsibility. The service rejects stale identities and unsupported IDs.
- Call `UpdateClientData` after downstream `HUD_UpdateClientData`, write only
  its returned FOV, and expose `Sensitivity` at `HUD_GetSensitivity`. Do not
  advance FOV in frame, prediction or CalcRefdef callbacks.
- Use `ViewModel` before the actual bob/idle/lag consumers. It provides boolean
  policy only; subtracting guessed viewmodel offsets after CalcRefdef is not an
  implementation of the original.
- Original CurWeapon does not clear FOV. Apply the original server's death/exit
  state through a snapshot; do not synthesize a mode or seed from local health.
  `VideoInit` clears only the proven aim flag. `EndAuthority` relinquishes host
  overrides and rejects future traffic; the caller supplies any new seed.
- Sound callbacks are synchronous and borrow their sample view only during the
  call. Remote sound receives the exact original engine parameters and path
  rewrite. The local callback is a required independent sound backend with
  original slot/voice semantics; it is not an EV_PlaySound fallback. Missing or
  failed backends return explicit failure, and no audio success is claimed.
- Animation receives a reliable server selection. This service creates no
  attack/reload/idle predictor or sequence-selection timer.

The service allocates no memory and performs no I/O or resource lookups. Message
paths are bounded; safety rejection of oversized/embedded-NUL samples is a host
transport adaptation. Real integration, sound backend, gameplay and performance
remain separate validation.
