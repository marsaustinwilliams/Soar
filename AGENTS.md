# Agent Workflow Policy

This file defines required behavior for all coding agents working in this repository.

## Failed-Fix Rollback Rule

When a non-serializer code edit is intended to fix a failing test:

1. Apply the smallest plausible change.
2. Run the targeted repro test immediately.
3. If the targeted failure is not fixed, revert that specific edit immediately.
4. Continue debugging with new evidence; do not leave known-ineffective fixes in place.

This rule is mandatory for non-serializer changes.

Serializer-scoped edits may remain in place across failed attempts while iterating with targeted diagnostics and evidence.

## Snapshot Debug Test Loop

Use this loop for snapshot/restore regressions:

1. Build and run only the failing targeted test first.
2. Keep commands bounded with explicit timeout for long runs.
3. Prefer deterministic single-test repros before broad suites.
4. Use staged diagnostics that identify whether failure occurs on save, reset command, or load.
5. Use valgrind/gdb only after the targeted repro is stable.

## Canonical Commands

Primary targeted repro command:

```bash
cd /home/poiso/Projects/Soar
python3 scons/scons.py UnitTests -j2 && \
LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
./build/UnitTests/UnitTests --snapshot-every-step -l -c FunctionalTests -t "[snapshot] testTowersOfHanoiFast"
```

Bounded valgrind command:

```bash
cd /home/poiso/Projects/Soar
env LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
timeout 180 valgrind --track-origins=yes --num-callers=32 --leak-check=no \
./build/UnitTests/UnitTests --snapshot-every-step -l -c FunctionalTests -t "[snapshot] testTowersOfHanoiFast"
```
