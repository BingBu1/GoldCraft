#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual SDK crosshair receiver/lifecycle and production codec."""
import runpy
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
scope=runpy.run_path(str(ROOT/'tests/cryoffear-host-admission/run.py'))
main=scope['main']
main.__globals__['SOURCES']=(
    'tests/cryoffear-crosshair-bridge/main.cpp',
    'native/csol/client/weapons/guest_cof_crosshair.cpp',
    'native/guest/cof/crosshair_protocol.cpp','native/guest/cof/client/crosshair.cpp')
main.__globals__['EXTRA_INPUTS']=('tests/cryoffear-crosshair-bridge/run.py',)
main.__globals__['NEEDS_PRESENTATION']=False
main.__globals__['BOUNDARY']='Actual SDK crosshair receiver/drawing callback, production codec/geometry and lifecycle; engine/connection services controlled; no game/render/performance acceptance'
if __name__=='__main__':main()
