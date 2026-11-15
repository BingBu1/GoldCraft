# Original CoF stamina cycles

This suite executes the complete original 858-byte periodic stamina/exhaustion/HUD block and 151-byte scalar Spawn/HUD subsection, comparing event order and all owned fields with the production component. It requires the matched private CoF MP and its pre-edit proof; no original bytes are published.

From Git Bash, use a fresh output directory:

~~~bash
python -B tests/cryoffear-stamina-cycle/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-stamina-cycle.json \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-stamina-cycle-local
~~~

Set COF_CLANG_CL to the configured x86 clang-cl and COF_COMPILER_ENV to its saved SDK environment. Sources stay in place; compiler dependencies, original evidence and generated answers are hashed before/after. Empty, truncated, extra and wrong answers must fail.

The ordinary cycle runs only when now is strictly greater than its deadline. The oracle covers drain/recovery, infinite-drain handling, alternate cycle intervals, zero/exhaustion, delayed negative clamping, voice selection/cooldown, and HUD callbacks changing time/value/flags/pulse. Spawn retains the prior-sent cache and pulse in the verified subsection. Original state fields are explicit inputs; the +3BC and +1EE0 producer identities are not guessed.

HUD Coord events carry the original native pfnWriteCoord argument. The fixture observes writer arguments; it does not claim a wire-float format or packet transport. Client evidence identifies signed16/8 coordinate decoding. Original sound wrapper, RNG/rules and network sinks are controlled ABI boundaries. Actor/map ownership, movement/sprint/FOV/water tail, actual effects/transport, live gameplay and performance remain separate acceptance work.
