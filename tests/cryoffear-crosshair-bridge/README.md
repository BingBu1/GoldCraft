# Actual CoF crosshair client bridge

Builds the production message consumer, codec, geometry and actual SDK draw
callback. Checks accepted/replayed/wrong-life/wrong-presentation state, fixed
wire size, preservation on malformed input, local inventory/HUD/menu gates,
connection/video resets, wrong owner thread and retirement during a draw.
Cold screen preparation is not repeated in steady Frame/Draw calls.

The connection and engine are controlled. This is not a pixel, actual network,
physical device, game or live performance test. See the
[report](../../docs/2026-11-12_实现-CoF准星快照与原版三线绘制-report.md).
