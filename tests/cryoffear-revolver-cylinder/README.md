# Revolver cylinder display

The suite compiles the actual SDK consumer and executes original client `1008D21C..1008D2A4`, with the pinned private original passed through `--sample`. Run `python tests/cryoffear-revolver-cylinder/run.py --sample <workspace-client.dll> --environment <compiler-env.json> --output build/cof-revolver-cylinder-validation`.

It covers current-viewmodel/absent-metadata/other-weapon gates, normal cylinder angles and boundaries, random raw float bits, NaNs/infinities and all six controller writes. The fourth current/latched controller remains untouched. The caller still supplies authenticated angle and selected original weapon; no network or plugin draw-hook acceptance is implied.
