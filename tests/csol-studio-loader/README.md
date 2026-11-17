# Client studio-load regression

Runs production LoadStudio, the real x86 entry bridges and SequenceLoadScope against the pinned MetaHook SDK. Filesystem IO and engine cache allocation are controlled boundaries. The wrapper counts calls to the real validator without approving malformed bytes.

The native caller builds the same [EBP-0x110] file-length slot as matched HL10210, then executes both production entry bridges. Checks cover exact return/frame/buffer/size ownership, full-read receipts, short reads, direct-call fallback, unchanged source bytes, v10/20/21 decoding, bad headers and safety records, contiguous SEQ append, nested loads, guard-page bounds and resource-generation retirement.

From Git Bash, generate the toolchain plan with tests/csol-wall-upgrade/run_current_client.py, then use a fresh output directory:

    python -B tests/csol-studio-loader/run.py --seed build/sequence-probe-guard-20261108-r2/plan.json --output build/studio-loader-new --resource-refresh 1

Repeat with --resource-refresh 0 for the ordinary build. An in-place optional host adapter can be linked with --extra-source <path>; the current attachment extension uses native/csol/client/studio_attachments.cpp. This fixture does not validate attachment rendering.

--baseline-ref <commit> compiles a historical loader from Git through stdin and requires its duplicate-animation-validation failure. --baseline-blob <blob> uses the actual pre-edit loader and requires the redundant-VFS-read failure after successful compilation and linking. --loader-blob <blob> verifies a narrowed publication without copying a source tree.

The runner seals actual includes, compiler/environment identity and source hashes before/after. It reuses the workspace GLEW library. Sprite rendering, live hooks, actual texture uploads, UI and complete map-change performance are outside this fixture.
