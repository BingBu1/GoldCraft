# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the FAMAS SDK composition and actual typed entity factory together."""
import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('cof_famas_sdk_driver',Path(__file__).parents[1]/'cryoffear-famas-weapon/run.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
if __name__=='__main__':module.main(factory=True)
