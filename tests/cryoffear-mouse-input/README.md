# Original CoF mouse arithmetic

Builds the actual C++20 x86 mouse helper with O3/ThinLTO/AVX2 and precise
floating point, then compares four output integers against the SHA-pinned
original instructions, including the actual internal sensitivity getter.

The 5,804 cases cover optional integer filtering and history updates, wrapping
adds, negative odd values, HUD/base selection, float rounding, CVTT indefinite,
signed FOV comparison and the separate zoom truncation. Samples and original
instruction bytes remain private. The helper takes explicit history and never
defines a reset/initialization policy.

Run the command in the [integration report](../../docs/2026-11-12_实现-CoF原鼠标缩放与宿主接线-report.md).
This does not test actual device acquisition, MetaHook installation, gameplay,
graphics, or runtime performance.
