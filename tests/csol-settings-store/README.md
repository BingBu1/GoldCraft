# Persistent custom settings store

This independent Windows test links the actual strict custom parser and SettingsStore, with no engine or AMXX boundary replacement. It creates only a fresh directory below build, uses actual file handles/hardlinks/junctions, and keeps all failed receipts.

Run directly from Git Bash in the project root:

    python tests/csol-settings-store/run.py --output build/settings-store-validation-new --environment build/csol-season2-trace-20261109/compiler-env.json --compiler /d/Arkari/build_ninja/install/bin/clang-cl.exe

Use a fresh output name. The runner compiles the actual source in place with x86/C++20/O3/ThinLTO/AVX2/precise and records Git revision plus input hashes before and after. It does not build the server or modify the Git index. The supplied compiler environment contains the installed Windows SDK/toolchain paths; neither runtime nor dependencies are copied.

Coverage includes default creation; real comment serialization; no premature snapshot publication; one outstanding transaction; stale expected revision; final Poll consumed once; two backups; CREATE_NEW temporary collision does not overwrite; deny-delete failure preserves old primary and snapshot; external file changes; invalid existing startup JSON; exclusive store writer lock; owner-thread submission; linked primary/backup rejection including links added after startup; actual junction traversal rejection; explicit relative/parent-traversal roots; and protected installation rejection before opening it.

Store API: Open is cold startup IO. The trusted server adapter authorizes before Submit/PrepareEdit. One worker persists one candidate. Poll is owner-thread nonblocking and returns a final StoreResult exactly once. Only a successful result exposes a new revision/snapshot for adapter publication. Authorization linearizes at accepted Submit; no engine/permission callback runs on the worker. Destroy drains any accepted job and joins the worker, so keep this owner outside gameplay frame hot paths.

The implementation targets Windows10 with FileRenameInfoEx POSIX replacement semantics: it retains the checked old-file read handle while atomically replacing the name. All new file contents are CREATE_NEW independent files and flushed before rename. File-data flush and atomic namespace replacement are verified here; power-loss durability is not tested. The two backup slots can rotate during an ultimately rejected main-file update; primary file and published snapshot stay unchanged on rejection.
