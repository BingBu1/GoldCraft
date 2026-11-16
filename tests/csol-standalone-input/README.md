# CSOL StandAlone input

The production adapter chains the real SDK `+speed`/`-speed` commands. Original client `f4145136…`, sender `02A59180`, stores one integer before calling out: changed1 queues `sprintON`, changed0 queues `sprintOFF`, both consume the native callback; identical states fall through. This differs from the separate inactive CoF two-key/axis adapter.

The named host API is `pfnClientCmd`, matching the original HW command-buffer route. No bindings, movement axes or archived cvars change. The 14-byte `CsoRun` host transport now uses version2: byte1 is retire0, enable1 or probe2, followed by little-endian life64 and revision32. A probe is not an input grant. Only installed native speed-command handlers acknowledge it with `csol_sprint_ready <life> <revision>\n`; the server checks the exact current owner before enabling speed or sending a newer grant. Generic `csol_protocol=1` is not this proof. Version1 packets and grants without a matching probe cannot activate input.

Run commands retain the decimal life token and newline so the server rejects queued commands from a retired life. Probe/acknowledgement is a GoldCraft transport adaptation, not an original CSO command or a security claim about a trusted client. The original one-integer sprint edge is unchanged. The complete capability remains closed pending input-lock lifecycle, actual native movement/prediction and gameplay validation. Per the user's scope correction, preserve scenario AI and do not integrate StandAlone noise, hearing or target selection.

Run the actual SDK consumer:

```bash
python tests/csol-standalone-input/run.py \
  --client-build build/resource-bridge-20261108/csol \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/standalone-input-sdk-new
```

The fixture executes the production command/message implementation with matched SDK types. It checks ordinary fallback, probe/grant ordering, exact acknowledgement text, replay and foreign-life rejection, repeated states, callback ordering/return, reentrancy, connection retirement, foreign threads, missing native commands/APIs and HUD callback replacement. Two thousand real adapter edges allocate nothing and perform no command lookup. This is a bounded hot-path check, not live CPU/FPS evidence.

Compare the complete original sender separately, using private workspace copies:

```bash
python tests/csol-standalone-input/run_oracle.py \
  --original analysis/csol-standalone-stamina-20261115/samples/client32.dll \
  --ida-evidence analysis/csol-standalone-stamina-20261115/client32-input-ida.json \
  --compiler "$GOLDCRAFT_CLANG_CL" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/standalone-input-oracle-new
```

The runner requires the exact original x86 SHA and all25 IDA instruction boundaries. It binds only the two command strings and the command-buffer callback; every original branch executes. It tests32,768 five-event sequences, including invalid integer states, and compares163,840 transitions. Generated original bytes stay in the private build output. Neither fixture executes a complete original client or proves gameplay, modal dispatcher lock semantics, server speed/prediction or multiplayer.
