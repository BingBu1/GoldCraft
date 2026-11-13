# Custom settings codec tests

The fixture compiles the actual pure `settings.cpp`, with no game engine,
filesystem policy or administrator substitute. It covers strict commented JSON,
typed defaults and edits, immutable publication, feature requirements, numeric
ranges, Chinese explanations and reversible feature toggles.

Use the project's specified Clang toolchain with CMake, or replay the local
compiler/link commands in `build/csol-custom-settings-20261113-03/receipt.json`.
The executable runs all checks with no arguments and prints the codec's canonical
default JSON with `--example`.

```bash
python tools/Generate-CSOLCustomSettings.py --help
```

Pass the workspace-built executable and a new workspace output path to generate
an example. Existing files are refused. Runtime atomic writes and permissions
belong to the server adapter, not this offline generator.

The 2026-11-13 x86 build passed 504 checks with recorded compiler inputs stable.
The zero-allocation hot-getter check does not establish live server performance,
and this suite does not validate menu permissions or gameplay consumers.
