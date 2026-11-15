#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and run the real StandAlone command/message consumer with SDK types."""
import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

if __name__ == '__main__':
    path = ROOT / 'tests/csol-challenge-environment/run.py'
    spec = importlib.util.spec_from_file_location('standalone_input_sdk', path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    sys.argv.extend(['--input-fixture', str(Path(__file__).with_name('client.cpp'))])
    runner.main()
