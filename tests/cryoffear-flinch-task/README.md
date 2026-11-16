# CoF small flinch task

This suite executes the original StartTask and RunTask dispatch for task29,
including both dispatch tables, the actual case-sensitive classname helper and
the complete activity selector. MakeVectors and model LookupActivity are
controlled external boundaries. It compares task fields and callback order with
the actual C++ component; it does not substitute a timer for animation completion.

From Git Bash, with matched private evidence:

```bash
python -B tests/cryoffear-flinch-task/run.py \
  --sample "$COF_MP_SAMPLE" --proof "$COF_FLINCH_TASK_PROOF" \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output "$COF_FRESH_BUILD"
```

The proof must predate the implementation and match the original module and all
archived IDA bytes. Every executed original instruction must fall in its pinned
regions, except the two explicitly controlled callbacks. Cases cover all four
class exceptions, skin gates, case/prefix differences, first and second selector
results changing, exact missing-model behavior, task-failure conditions, and
nonzero sequence completion. The runner also checks callback failure/cancellation,
malformed fixtures, stable compiler dependencies and zero allocations in 10,000
hot compositions. Original bytes and binaries are not published.

The production scenario target's ownership, damage attribution, route retirement
and actual SetActivity/animation completion bridge remain separate work. Passing
this suite does not prove live monster behavior, audibility, multiplayer or game
performance.
