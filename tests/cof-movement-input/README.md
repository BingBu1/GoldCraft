# Native movement input handoff

The client adapter wraps the real matched CS `CL_CreateMove` function. A valid server life policy temporarily supplies forward/back/side reference cvars, defers the native weapon-speed input cap to the pending authoritative PM stage, and observes the original `+speed/-speed` commands to emit `IN_RUN` intent. The original native input code still computes keyboard fractions and view angles. No binding or archived config string is changed, and the original up axis is retained.

The current server does not publish this input policy, and the incomplete CofSprint capability remains unavailable. This checkpoint prepares the actual client input consumer; it does not claim full sprint, prediction or PM integration.

Run its actual production implementation against matching SDK callback types:

```bash
python -X utf8 -B tests/cof-movement-input/run.py \
  --client-build build/csol-client-crc/client \
  --environment "$GOLDCRAFT_COMPILER_ENV" \
  --output build/cof-input-test-new
```

The test executes the real message, command-chain and native-hook adapter. The downstream native function is an explicit command-generation boundary reproducing the IDA-proven cap-before-slowdown ordering; it is not the original whole CS client DLL. Checks cover uncapped reference inputs, half input, diagonal handoff to the original arithmetic core, two physical keys/manual release, ordinary/disabled/no-policy clients, death/menu/inactive clearing, external numeric/slot writes, nesting, map retirement and shutdown during native input. A thousand actual hot adapter calls must allocate nothing and perform no symbol/CVAR lookup.

An opaque foreign callback also retains the temporary adapter and is exercised after a second input scope, shutdown and reinitialization. The pre-fix implementation recurses in this case. The adapter now captures its original fallback once per resolved native binding, while each input scope separately preserves the current callback head. This is host plugin lifecycle protection, not an inferred CoF gameplay rule.

The matching runtime symbol metadata is SHA/CRC-specific. Verify generation and mismatched metadata refusal without changing any source file:

```bash
python -X utf8 -B tests/cof-movement-input/gamedata.py \
  --client analysis/cof-weapons-20261111/client-evidence/host-client/client.dll \
  --output build/cof-input-gamedata-new
```

The exact client SHA-256 is `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`, with CRC64/XZ `124D6034A6B28B40`. The generated records identify the complete native CreateMove function and its verified maximum-speed callback operand. The callback slot resides in zero-initialized virtual data, so its runtime value must also be nonnull; it is not assumed to be a file-backed pointer. Production resolves these records through MetaHook's versioned game-symbol API and never applies the private IDA addresses directly.

The wire message `CsoInput` is fixed at 26 bytes: version and enabled flag, little-endian revision32/life64, then three IEEE float reference axes. Retired lives cannot reactivate and every new connection resets the old policy. Server/PM publication, stamina eligibility, raw/final authoritative caps, shared client prediction, real collision/water/ladder behavior and live CPU/multiplayer acceptance remain separate work. Original ordinary input must remain unchanged until all of those paths are installed.
