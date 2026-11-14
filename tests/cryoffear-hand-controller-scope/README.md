# Controller-independent CoF hand authoring

Run `run.py` with the actual Revolver view model, paired CSO hand donor, explicit authored profile and a fresh workspace `build` output. The test pins the real target SHA, checks every original frame/channel and the entire old payload, and proves that selected hand geometry and all ancestors have no controller dependency. Full or controlled cylinder poses remain rejected. It also rejects malformed tables, controller references, affected descendants and authored bindings into controlled bones.

This verifies offline geometry preparation. The generated donor remains a candidate requiring visual calibration, advertised-resource integration and actual character/gameplay checks.
