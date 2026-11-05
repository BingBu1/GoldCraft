# SKULL5 scope validation

`client.cpp` links the actual weapon dispatcher, inventory and SKULL5 modules against real SDK types. It exercises SetFOV 80/35/90, native center placement, the shared dynamic-crosshair consumer, hidden/dead/spectator states, both stock/extended weapon-message orders, inactive messages, video reset and repeated shutdown. Engine callbacks, unrelated UI services and the host scheme metric are controlled boundaries.

`gpu.cpp` executes the complete scope renderer on hidden Windows OpenGL contexts in compatibility and core profiles. It compares uploaded vertices with recorded original instruction output, reads actual framebuffer mask pixels, checks inherited GL state, real shader compilation failure, missing-resource recovery, repeated cleanup and independent-context resource ownership. Image decode is a complete production TU; UploadImage is extracted verbatim from its production body. This does not control a game window or prove final in-game pixels.

The runners accept an existing matching SDK proposal at `build/csol-skull5-integration-20261103/sdk-01/proposal.json`. They freeze inputs, capture includes, permit checked object reuse, and retain all initial failures. `run_client.py --old-skull5 <file>` expects the old source to compile and fail behavior checks. `run_gpu.py` requires `--oracle`, `--resources` and `--output`; resource roots remain local and are not published.

The public client fixture also includes the tracked `tests/csol-skull-balrog/integration_client.cpp` and links `tests/csol-weapon-slots/session_edges.cpp`. Neither runner copies game assets or original binaries into the publication tree.

Immediate predicted zoom remains a separate open item: the native client's last-FOV cache is consumed by HUD_UpdateClientData, so updating a custom mirror of FOV alone would move the scope frame ahead of the actual camera. These tests verify the existing authoritative SetFOV path, not that unfinished prediction path.
