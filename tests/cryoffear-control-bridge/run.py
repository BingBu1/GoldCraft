#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reuse the real-SDK particle runner, linking actual control and particle TUs."""
import runpy
import hashlib
import json
import sys
from pathlib import Path
runner = Path(__file__).resolve().parents[1] / 'cryoffear-particle-bridge/run.py'
scope = runpy.run_path(str(runner))
main = scope['main']
main.__globals__['SOURCES'] = (
    'native/csol/client/weapons/guest_cof_control.cpp', 'native/guest/cof/control_protocol.cpp',
    'native/csol/client/weapons/guest_cof_particles.cpp', 'native/guest/cof/particle_protocol.cpp',
    'native/guest/cof/client/aurora.cpp', 'native/guest/cof/client/aurora_render.cpp',
    'native/guest/cof/client/forward_displacement.cpp',
    'tests/cryoffear-control-bridge/main.cpp')
wrapper = Path(__file__).resolve()
before = hashlib.sha256(wrapper.read_bytes()).hexdigest()
main()
assert hashlib.sha256(wrapper.read_bytes()).hexdigest() == before
out = Path(sys.argv[sys.argv.index("--output")+1])
(out / "runner-identity.json").write_text(json.dumps({"path":str(wrapper),"before":before,"after":before}), "utf-8")
