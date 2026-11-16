# StandAlone native frame owner

This fixture executes the real GameDLL stamina owner, registered scenario/player runtime, ClientCommand route and native ResetMaxSpeed. Original fidelity is documented in [the frame integration report](../../docs/2026-11-16_接入-绝命档案服务端疾跑帧与命令-report.md). It is CSOL StandAlone, not Cry of Fear.

Compile against a complete current GameDLL link seal:

```bash
python tests/csol-emergency-kick/run.py \
  --seal build/standalone-readiness-server-full-20261116-01/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --output build/standalone-frame-validation-new --sealed-production \
  --fixture tests/csol-standalone-frame/sdk.cpp \
  --groups init normal crouch release depleted postthink clock retire new-life \
    callback-close callback-reset callback-partial command speed-layers disabled capability-closed \
    readiness old-client ready-retired fake-client probe-reset probe-close probe-ack-reentry
```

Use a new output directory. Actual compiled production objects and headers must match the seal. No copied source tree is created. Engine callbacks, clock, configuration root and ReAPI callback are explicit controlled boundaries. The fixture does not simulate actual PM, a graphical client or a network peer.

The arithmetic path already has an independent original-instruction oracle in `csol-standalone-movement`. This suite checks native ownership: one frame per timestamp, malformed/stale life requests, horizontal movement, duck/release/depletion, no duplicate PostThink recovery, backwards clocks, retirement and new life, native ResetMaxSpeed callback cancellation, real ClientCommand argument/identity checks and freeze/observer/weapon-speed ordering. The actual current-life probe must be acknowledged through ClientCommand before a frame changes movement; old consumers get no repeated probes, speed changes or distance debit. Retired/foreign tokens, fake clients, partial acknowledgements and callback invalidation/reentry are covered. Closed full capability and disabled configuration must not grant input or alter movement even when a client manually sends sprintON.

The user explicitly limits this port to stamina, sprint and movement speed; retain scenario AI and exclude StandAlone noise/hearing/target selection. Those AI consumers are not sprint prerequisites. The full sprint capability stays unavailable until input locks and movement/prediction are complete. Directly driving the frame owner in a controlled SDK fixture is evidence for that component, not authorization to enable unfinished gameplay. Real PM/prediction, collision, ladders, multiplayer and sampled game performance remain separate acceptance work.
