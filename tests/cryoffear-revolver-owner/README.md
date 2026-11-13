# Revolver canonical owner view

Run the shared `tests/cryoffear-p345-owner/run.py` driver with `--weapon revolver`. It compiles the real canonical owner and the Revolver view against stock and two actual CSO player models. Tests explicit first-draw/cylinder seeds, reset-versus-stepped gate writes, independent Glock first-draw state, stale generations, model/life retirement and absence of model I/O during repeated reads. The caller still owns original life/reset policy and client cylinder presentation; neither is implied by this component.
