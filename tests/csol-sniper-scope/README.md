# SKULL5 scope validation

`client.cpp` links the actual weapon dispatcher, inventory and SKULL5 modules against real SDK types. It exercises SetFOV 80/35/90, native center placement, the shared dynamic-crosshair consumer, hidden/dead/spectator states, both stock/extended weapon-message orders, inactive messages, video reset and repeated shutdown. Engine callbacks, unrelated UI services and the host scheme metric are controlled boundaries.

`gpu.cpp` executes the complete scope renderer on hidden Windows OpenGL contexts in compatibility and core profiles. It compares uploaded vertices with recorded original instruction output, reads actual framebuffer mask pixels, checks inherited GL state, real shader compilation failure, missing-resource recovery, repeated cleanup and independent-context resource ownership. Image decode is a complete production TU; UploadImage is extracted verbatim from its production body. This does not control a game window or prove final in-game pixels.

`run_client.py` accepts `--build`, `--environment` and a fresh `--output`. It reads SDK paths and definitions from the actual CMake/Ninja command, freezes the full compiler dependency closure, and compiles project sources directly with C++20/AVX2/ThinLTO and precise floating point. No source tree is copied. It also tests the original 0x80 viewmodel-hide flag through actual SetFOV/weapon/session routing, both scope levels, unzoom and weapon switches. `run_gpu.py` requires `--oracle`, `--resources` and `--output`; resource roots remain local and are not published.

The public client fixture also includes the tracked `tests/csol-skull-balrog/integration_client.cpp` and links `tests/csol-weapon-slots/session_edges.cpp`. Neither runner copies game assets or original binaries into the publication tree.

Immediate predicted zoom remains a separate open item: the native client's last-FOV cache is consumed by HUD_UpdateClientData, so updating a custom mirror of FOV alone would move the scope frame ahead of the actual camera. These tests verify the existing authoritative SetFOV path, not that unfinished prediction path.

The historical `run_gpu.py` still needs its old SDK proposal; it was not rerun for the 2026-11-17 visibility change. Scope GL geometry and resources were not changed. Final game pixels and performance require separate live verification. See [the visibility evidence and validation report](../../docs/2026-11-17_修复-SKULL5开镜枪身遮挡-report.md).

`run_client.py --host-guard` links the real native-scope interceptor and checks its SDK callback ABI, unchanged native return/float bit patterns, replacement ownership, missing-symbol rejection and immediate native fallback. The separate controller run verifies actual SetFOV/weapon messages determine ownership for both SKULL5 levels and release it on switches/reset. Neither suite proves in-game pixels.
