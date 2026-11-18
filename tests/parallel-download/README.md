# Independent downloader regression

Build the production plugin and SDK bridge in the existing configured Windows x86 CMake directory with `tools/Build-ParallelDownload.py`. The bridge includes the actual production queue/frame/reset code and uses a fault-injecting local HTTP server.

Run from the project root in Git Bash:

```bash
python -X utf8 -B tests/parallel-download/run.py build/parallel-download/parallel_download_tests.exe build/download-network
python -X utf8 -B tests/parallel-download/run.py build/parallel-download/parallel_download_bridge_tests.exe build/download-bridge --bridge
python -X utf8 -B tests/parallel-download/run.py build/parallel-download/parallel_download_bridge_tests.exe build/download-ui --bridge --ui-reentry
python -X utf8 -B tests/parallel-download/run.py build/parallel-download/parallel_download_bridge_tests.exe build/download-connected --bridge --connected
python -X utf8 -B tests/parallel-download/run.py build/parallel-download/parallel_download_bridge_tests.exe build/download-recovery --bridge --recovery
```

Choose fresh output directories. The connected suite verifies same-connection completion, native precache gating, empty keepalive packets, failure fallback, stale/reentrant cancellation, replacement admissions and explicit user cancellation. It does not simulate full renderer loading or authentication. Real host Hook identity, UI appearance, signon, cancellation and slow-download evidence require isolated gameplay; see the [implementation report](../../docs/2026-11-18_实现-HTTP下载期间保持原连接-report.md).
