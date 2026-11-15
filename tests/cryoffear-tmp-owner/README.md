# TMP canonical owner and animation binding

Use the actual SDK owner runner with --weapon tmp, a fresh workspace build output, and COF_COMPILER set to the local x86 Arkari Clang executable:

~~~bash
python tests/cryoffear-p345-owner/run.py --weapon tmp \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-tmp-owner-validation
~~~

This compiles the production typed owner view, canonical OwnerRuntime and animation adapter against the actual ReGameDLL SDK and three pinned player models. TMP borrows canonical input/movement/life while keeping independent first draw, damage/aim rules and original90/300 reserve rule. Stock/CSO compact-weapon animation plans require actual standing/crouching labels.

Checks include canonical fields, independent first draw,10000 cached reads without further model reads, accepted jump/input changes, rules/rebinding, stale life/model keys, cancellation, two synthetic SDK players and all six standing/crouching actions on actual male/female CSO models. They do not establish a live map installation, purchase/input delivery, rendered grip, multiplayer gameplay or live performance.
