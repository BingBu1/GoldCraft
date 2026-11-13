# Human-kick client input checks

input.cpp consumes the actual client InputState in native/csol/client/scenario/core/human_kick.hpp and the actual CsoKick protocol. It checks native-server/other impulse passthrough, exact100 ownership, menu/input context passthrough, server-only Ready, active/cooldown/reset/life state and malformed messages.

Run from Git Bash with the matched x86 toolchain environment:

```sh
python tests/csol-human-kick-client/run.py --compiler "$CSOL_CLANG_CL" --environment "$CSOL_COMPILER_ENV" --output build/csol-human-kick-client-input-check
```

Set `CSOL_CLANG_CL` to the matched compiler and `CSOL_COMPILER_ENV` to its recorded environment JSON. Use a fresh output directory. The runner compiles actual current sources without copying a source tree, with x86/C++20/O3/ThinLTO/AVX2/precise and one linker job. It records source/compiler/environment hashes before and after, actual includes after compilation and across execution, command, logs and result in receipt.json. The 2026-11-13 input-01 run passed 47 checks with unchanged recorded inputs. It includes rejection of an older life's Ready and acceptance of a new server life namespace after reset.

These pure checks do not prove actual CS keyboard/chat dispatch, VGUI painting, native animation transport, dedicated server gameplay or performance. The full client DLL build and deployment remain with the parent.

The simple F / 可以踢击 presentation is an explicit user-requested HUD, not a reconstructed original layout. Original foot animation remains server pev.viewmodel/WeaponAnim1; no client animation override or delayed restoration is added.
