# MP5 canonical owner and animation binding

Run `tests/cryoffear-p345-owner/run.py --weapon mp5` with `--compiler`, `--environment` and a fresh `--output` under workspace `build`.

The actual SDK fixture checks original common input/movement/life fields, independent MP5 rules and binding/animation types, stale life/model/map handles and two synthetic native players. It uses three pinned stock/male-CSO/female-CSO models and checks every standing/crouching action. TMP first-draw mutation cannot be called on the MP5 owner; changing TMP's state does not create one for MP5. Ten thousand cached reads must not reread the model.

These are SDK state/model tests, not installed map/client hooks, rendered hand contact, actual multiplayer or live CPU/frame-time acceptance.
