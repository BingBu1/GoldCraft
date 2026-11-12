# CoF host presentation, input and admission

The test builds the actual SDK input/admission translation units and both real
host presentation parsers. It consumes the resource tool's generated JSON and
exercises key edges, multi-key holds, command collisions, life/selection/focus
loss, real resource-transaction invalidation, generations and cold preparation.
Other service readiness is explicitly controlled; this is not a replacement
for full Renderer, network, game input, graphics or live performance tests.

Run the two commands in the linked report with a fresh build directory:
[integration report](../../docs/2026-11-12_实现-CoF机瞄输入与购买HUD准入-report.md).

The report also records the original SHA-pinned IDA command/bit consumers,
host-only UI adaptations and publication boundaries. Original binaries,
disassembly, assets and machine-specific build outputs are not published.

The fixture compiles the production CreateMove and CharacterCreateMove bodies
against a native command-rebuild boundary. It checks the aim bit after native
input, all three menu suppression paths, release, inactive input and preservation
of Use/right-click/movement. The presentation filename comes from the compiled
contract. This component does not establish full plugin/weapon activation.
