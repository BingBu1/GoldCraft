# Golden plan rules

Verified on 2026-11-02: **265 checks / 0 failures** across the six groups below.
The existing champion 78/0 checkpoint is separate and was not rerun. This target
does not load the SDK or prove a live monster can be replaced. The result comes
from the complete frozen input tree; the minimal publication subset was reviewed
statically and was not rebuilt separately. See the
[execution report](../../docs/2026-11-02_验证-Golden纯组件-report.md).

The six bounded CTest groups exercise the actual Golden implementation, packet
state and trigger/wave runtime adapter:

| Group | Required behavior |
|---|---|
| `golden_parser` | Threshold selection, last equal record, float32 probability conversion, schema rejection, optional file states and loader reentry |
| `golden_parameters` | Fixed wire bytes, initial missing-key zero, keys 1/2/4, table replacement, first duplicate retained, nonzero subtype ignored, zero-count clearing, unsigned byte count/key, malformed and nonfinite update rollback |
| `golden_candidates` | Source order/duplicates, vector round index, excluded types, trigger command and current-wave availability |
| `golden_plans` | Known MT19937 first roll, strict chance/delay, deferral, second-plan uniqueness, wave-end replacement and person-zero gate |
| `golden_members` | Immutable type copy, positive overrides, actual current health, independent plan/member counts, identity reuse, reclaim and death restoration order |
| `golden_runtime` | Same-ID trigger lookup, cross-group cooldown lifetime, geometry default, pending row clocks, empty runtime insertion, dispatch iteration stability, low16 aliases and failed-snapshot preservation |

For a fresh reproduction, configure this directory with the project's Clang
C++20 toolchain, then build and run the six CTest groups in a coordinated single-job
window. The report gives the commands and required dependencies. Keep the first
failing run. Do not reuse objects from a different source snapshot. This execution
used x86/O3/ThinLTO/AVX2/precise FP and `/MT`, with no CRT DLL deployment.

Observed checks: parser 27, parameters 61, candidates 19, plans 33, members 34,
runtime 91; every group reported zero failures. The first attempt stopped after
core linking because its strict dependency manifest omitted the CRT's default
`uuid.lib`. After preserving that failure and registering the complete observed
default-library set, a new output directory reused five verified objects and
completed the remaining work; no test had run before that continuation.

The parameter fixtures contain literal little-endian float32 bytes; they do not
use a production encoder to construct their expected values. Valid packet
behavior follows the HW `Packet_ZBSGoldenZB` producer and v698 getter. Exact
length checking and rejection of NaN/infinity are the host's transactional
validation policy, not claims about original malformed-packet behavior. The
rollback cases include a valid first entry followed by an invalid value so they
can detect partial replacement. No packet is sent over the network by this test.

`golden_runtime` uses a separate `csol_scenario_golden_runtime` executable. Its
smallest build has four translation units: `runtime_main.cpp` and production
`controller.cpp`, `waves.cpp`, `trigger_program.cpp`. The fixture constructs
public rule/wave tables directly and uses real install, trigger execution,
controller preparation/activation, row ticks and snapshot reads. It does not
need `golden.cpp`, CSV parsers, the SDK or game binaries. Trigger notifications
use a capture sink; this is not a full server trigger adapter test.

Runtime cases distinguish trigger 10 from the wave 20 it activates, a preloaded
but inactive clock from a finished clock still on the active list, and event
point 9999 from its scheduling state. New empty runtimes are inserted inside
an actual dispatch while both the current runner and later waves continue.
Uninitialized-controller rejection is exercised when trigger metadata requires
runtime access; it must preserve the caller's prior snapshot. All six groups
have now run; this remains component evidence rather than a host gameplay test.

Host tests still need real Factory/Pool replacement, wave-runtime touch order,
current map/round/lease guards around engine callbacks, contribution identities,
death notifications, replay cleanup and actual multiplayer gameplay. Original
producer evidence is in `analysis/csol-model-20261017/scenario/golden-host-20261102`;
the trigger cooldown and pending row-clock mapping is in the adjacent
`golden-wave-metadata-20261102` report. Those static findings do not establish
authenticated host packet delivery or actual runtime snapshot wiring.
