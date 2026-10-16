# Workspace analysis cleanup

The user requested clearing the complete `analysis` directory on 2026-10-16.
The directory is now empty. Historical documentation references into that
directory describe deleted evidence; they do not provide retained test inputs
or replayable results.

The cleanup removed 111,958 files, 27,690 directories
and 11 junctions/symlinks. Deleted regular-file contents total
11,170,590,150 bytes (10.40 GiB). Links were removed without traversing their targets.
No deletion errors or remaining entries were reported.

One current, undeployed combined candidate and necessary small continuity
records were moved to `build`: 46 files totaling 10,249,931 bytes, with matching
SHA-256 hashes. The current candidate is described by
`build/candidates/combined-current/manifest.json` and contains 16 artifacts.
Current summaries live in `build/continuity`; incomplete movement work lives in
`build/status-motion-work`. The download domain uses `build/fastdl-work`.
Old caches, extracted references, IDA databases and previous test runs were deleted.

Read-only verification matched 19,575 original-installation file
metadata records, 5 original content hashes and 7 deployed module hashes.
The main checkout's Git index was unchanged. No gameplay deployment, restart or
input was performed. This maintenance checkpoint adds no gameplay acceptance.

Local receipts are `build/analysis-cleanup-20261016/result.json`, `retained.json`
and `verification.json`. These machine-local records are excluded from publication.
Public-source scan reports now default to `build/publication`.
