# Menu input host adapter checks

Compiles the production `menu_input.hpp` helper directly. Checks press/capture/release, release outside and drag back, duplicate callbacks, two event clicks between polls, stale polling after an event, recovery when an engine edge is absent, opening with a held button, focus/hide reset, independent mouse buttons, and the original TabButton background truth table.

The helper merges host input observations only. It does not implement the original command interval or network transaction lifecycle. These checks do not establish actual CS input delivery or menu appearance; parent integration, full client compilation and live gameplay remain separate.

Run `run.py --compiler <clang-cl.exe> --environment <x86-environment.json> --output <fresh workspace build directory>` from Git Bash. The runner records the exact command, source identities before/after, compilation and execution separately.
