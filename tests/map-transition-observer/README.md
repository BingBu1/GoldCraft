# Map transition observer

Run from Git Bash on Windows:

```bash
python -X utf8 -B tests/map-transition-observer/main.py --output build/map-transition-observer-tests
```

Checks atomic result replacement, preservation of an existing temporary file, loopback-only single-player admission with an arbitrary player name, and rejection of incomplete JSON. It does not start a game, send RCON, or prove map-loading performance. Actual observation uses `tools/Measure-ClientMapTransition.py` and requires a live, managed sandbox B, a fresh rendered view and the server's complete HUD download closure.
