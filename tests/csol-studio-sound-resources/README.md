# CSOL studio-event sound dependencies

Runs the production dependency collector and current ReHLDS loader against the
real SDK. The filesystem and resource advertisement are controlled test doubles;
this is not a full server, HTTP download, or gameplay test.

The original client `HUD_StudioEvent` branch at `02A419D6..02A41A97` resolves
numeric event 5008 to WAV files absent from model option strings. The test covers
both ordinary Lastclue BOSS models without any Oberon/type 30200 registration,
cold and warm collection, selected sound variants, duplicate suppression,
imported SEQ events, existing literal sound events, and missing required files.
Ordinary models without 5008 retain the loader's early return, including bounded
malformed table cases. Original evidence and implementation mapping are in
[`../../docs/2026-11-05_审计-最初线索通用BOSS调用链-report.md`](../../docs/2026-11-05_审计-最初线索通用BOSS调用链-report.md).

Use a successful frozen `csol-dependency-cache` SDK baseline, the pre-change
`dependencies.cpp`, an unpacked CSOL common directory, and a new workspace build
output directory:

```powershell
param(
    [Parameter(Mandatory)][string]$Baseline,
    [Parameter(Mandatory)][string]$Before,
    [Parameter(Mandatory)][string]$Common,
    [Parameter(Mandatory)][string]$Output
)
python -X utf8 -B tests/csol-studio-sound-resources/run.py --baseline $Baseline --before $Before --common $Common --output $Output
```

The runner verifies all baseline files and toolchain inputs, freezes current
sources separately, compiles x86 C++20 with the baseline flags, and links with at
most two ThinLTO jobs. It records actual includes, input SHA-256 identities,
commands, outputs, and original resource identities. The old collector must fail
the first missing-footstep assertion; the candidate must pass. All input and
resource identities are rechecked after execution. Failed runs are preserved.

The frozen baseline may share immutable files by hard link. Candidate files
always replace links with independent files. The runner never writes to game
resources, deploys binaries, or changes Git state.
