#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual native input consumer with matched SDK callback types."""
import argparse
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client-build", "environment", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("env_runner", ROOT / "tests/csol-challenge-environment/run.py")
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    # Reuse the tested SDK command parsing/actual-include inventory runner while
    # selecting this fixture explicitly. No source tree or candidate is copied.
    import sys
    sys.argv = [str(ROOT / "tests/csol-challenge-environment/run.py"),
                "--client-build", str(args.client_build), "--environment", str(args.environment),
                "--output", str(args.output), "--input-fixture", str(Path(__file__).with_name("client.cpp"))]
    runner.main()


if __name__ == "__main__":
    main()
