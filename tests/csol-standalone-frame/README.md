# StandAlone native frame owner

This fixture executes the real GameDLL stamina owner, registered scenario/player runtime, ClientCommand route and native ResetMaxSpeed. Original fidelity is documented in [the frame integration report](../../docs/2026-11-16_接入-绝命档案服务端疾跑帧与命令-report.md). It is CSOL StandAlone, not Cry of Fear.

Use the current GameDLL link seal and build the lightstyle provider as described in the [environment fixture](../csol-challenge-environment/README.md).

```bash
GOLDCRAFT_TEST_LIGHT_PROVIDER="$(pwd -W)/build/environment-provider-new/swds.dll" \
python tests/csol-emergency-kick/run.py \
  --seal "$GOLDCRAFT_CURRENT_SERVER_LINK_SEAL" \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --output build/standalone-frame-validation-new --sealed-production \
  --fixture tests/csol-standalone-frame/sdk.cpp \
  --groups rules-frame init normal crouch release depleted postthink clock retire new-life \
    callback-close callback-reset callback-partial command speed-layers disabled \
    readiness old-client ready-retired fake-client probe-reset probe-close probe-ack-reentry hud-order
```

Use a new output directory. Actual compiled production objects and headers must match the seal. No copied source tree is created. Engine callbacks, clock, configuration root and ReAPI callback are explicit controlled boundaries. The fixture does not simulate actual PM, a graphical client or a network peer.

The arithmetic path already has an independent original-instruction oracle in `csol-standalone-movement`. This suite checks native ownership: one frame per timestamp, malformed/stale life requests, horizontal movement, duck/release/depletion, no duplicate PostThink recovery, backwards clocks, retirement and new life, native ResetMaxSpeed callback cancellation, real ClientCommand argument/identity checks and freeze/observer/weapon-speed ordering. The actual current-life probe must be acknowledged through ClientCommand before a frame changes movement; old consumers get no repeated probes, speed changes or distance debit. Retired/foreign tokens, fake clients, partial acknowledgements and callback invalidation/reentry are covered. A map started with stamina disabled retains native movement even after a console edit during the map.

The `rules-frame` group runs the real rules clock and lightstyle provider. It checks noon speed, dusk sprint at 300, the retained grant in night, immediate revocation, stale commands and same-map replay.

The `hud-order` group executes native `CBasePlayer::UpdateClientData`, rather than setting a private readiness flag: the rules frame must not send a one-shot probe before that method queues InitHUD and records `m_fGameHUDInitialized`. The other focused groups explicitly start after this native HUD boundary, then acknowledge the actual probe; they do not claim to execute the full native connection.

The user explicitly limits this port to stamina, sprint and movement speed; retain scenario AI and exclude StandAlone noise/hearing/target selection. Those AI consumers are not sprint prerequisites. Real PM and prediction, collision, ladders, multiplayer and sampled game performance have separate acceptance checks.
