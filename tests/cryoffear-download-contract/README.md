# CoF automatic download contract

Compiles the real resource overlay plus its actual cache and SDK dependencies, then loads the verified workspace IFileSystem009 DLL. It exercises existing mount/read/write/PAK/session behavior and CoF path admission against traversal, device names, extensions and similar prefixes. Run `python tests/cryoffear-download-contract/run.py --commands <compile_commands.json> --environment <compiler-env.json> --filesystem <isolated-filesystem.dll> --output build/cof-download-overlay-check`.

Actual HTTP Store/transport checks remain in native/engine/tests/http_download_tests.cpp. HUD generic registration is exercised by tests/cryoffear-resource-preparation/run.py --presentation <isolated-cstrike>. These are separate from actual B delivery, HUD pixels and gameplay.
