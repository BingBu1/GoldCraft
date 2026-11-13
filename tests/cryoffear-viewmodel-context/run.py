# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual scoped CoF viewmodel context with the matching MetaHook SDK."""
import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('driver',Path(__file__).resolve().parents[1]/'cryoffear-host-admission/run.py')
driver=importlib.util.module_from_spec(spec);spec.loader.exec_module(driver)
driver.SOURCES=('tests/cryoffear-viewmodel-context/main.cpp','native/csol/client/weapons/guest_cof_viewmodel.cpp')
driver.NEEDS_PRESENTATION=False
driver.EXTRA_INPUTS=('tests/cryoffear-viewmodel-context/run.py',)
driver.BOUNDARY='Actual SDK/RAII onehanded viewmodel context; original native render mirror branch is verified separately, not full gameplay or performance'
if __name__=='__main__':driver.main()
