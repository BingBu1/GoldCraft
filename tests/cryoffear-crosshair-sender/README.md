# Actual CoF crosshair snapshot sender

Compiles the production sender/codec against the current ReGameDLL SDK.
The test completes fixed packets after retirement or entity recycling at every
message callback, validates recipient/channel/payload, and refuses nested sends,
bad identity, malformed packets and absent backend functions. Unreached SDK
virtual methods are generated as aborts, not successful gameplay substitutes.

The actual outgoing snapshot caller is compiled separately. This fixture does
not claim full Glock runtime, real network delivery or game/performance
acceptance. See the
[report](../../docs/2026-11-12_实现-CoF准星快照与原版三线绘制-report.md).
