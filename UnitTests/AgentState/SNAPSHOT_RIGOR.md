# Snapshot Rigor Guide

This document defines the validation rigor for Soar snapshot save/load work.

## Goal

A restored runtime must behave like uninterrupted execution. Snapshot tests are not only file roundtrip tests; they are behavioral equivalence tests.

## Terms

- Vanilla run: normal execution without any snapshot/load during progression.
- Snapshot run: for each progression step, save kernel state, reset (`soar init`, `production excise --all`, `run 1`), then reload snapshot and continue.
- Step: usually one decision (`run --self -d 1`) unless a test intentionally uses phase/elaboration/output stepping.

## Current Working Baseline (2026-04-13)

The following snapshot CLI tests pass locally against `out/soar`:

- `test_basic_applied_roundtrip`
- `test_tie_impasse_roundtrip`

Run commands used:

```bash
cd UnitTests/AgentState
SOAR_CLI_BIN=/home/poiso/Projects/Soar/out/soar \
SOAR_SOURCE_DIR=/home/poiso/Projects/Soar \
python3 -m unittest test_agent_state_cli.AgentStateCliRoundtripTests.test_basic_applied_roundtrip -v

SOAR_CLI_BIN=/home/poiso/Projects/Soar/out/soar \
SOAR_SOURCE_DIR=/home/poiso/Projects/Soar \
python3 -m unittest test_agent_state_cli.AgentStateCliRoundtripTests.test_tie_impasse_roundtrip -v
```

## Required Validation Pattern

For each scenario under active development:

1. Run vanilla baseline to completion and capture key observables.
2. Run snapshot-at-every-step equivalent.
3. Compare canonicalized outputs and counters at matched checkpoints.
4. Confirm continuation after restore still reaches the same end behavior.

## Required Observables

At minimum, capture/compare all of the following where applicable:

- Goal stack shape and state IDs (`print --stack`, `p S*`).
- Operator and result WMEs in active states.
- Production firing counts (`fc <prod>`).
- Preference support state for critical slots (`preferences <id> <attr>`).
- Impasse/substate structure (`^impasse`, `^superstate`, `^item`).
- IO link behavior (`I2`, `I3`, output command structures).

## Integrity Checks for Restore Logic

When debugging serializer/restore changes, explicitly verify:

- Pending assertion behavior is preserved (IE/PE paths).
- Production firing counts are restored correctly.
- Detached/orphan WMEs are not left behind.
- Decider/owner links are consistent after remove/add cycles.
- Chunking/explanation identity structures remain coherent.

## Test Layers

Use both layers continuously:

- CLI black-box roundtrip tests: `UnitTests/AgentState/test_agent_state_cli.py`.
- C++ harness snapshot mode: `--snapshot-every-step` through test harness categories.

Both layers are required before declaring a change stable.

## Failure Triage Protocol

If a snapshot test fails:

1. Reproduce with the smallest single test method.
2. Capture pre-save vs post-load prints/firing counts/preferences.
3. Determine first divergence step index.
4. Check whether reset preconditions succeeded before load.
5. Inspect pending assertions, instantiations, and owner lists at divergence.

## Update Policy

Keep this file conservative:

- Add "Working Baseline" entries only after locally reproducible pass results.
- Do not mark broad suites green from one-off spot checks.
- Record exact commands used for every new green milestone.

## Serializer-First Change Policy

When snapshot behavior fails, the default and highest-priority fix path is the serializer/restore pipeline.

- Assume the baseline agent/kernel code should remain unchanged unless proven otherwise.
- Prefer serializer export/restore fixes over changing core behavior outside serialization.
- The target is full destroy-and-rebuild correctness: a restored runtime must match pre-snapshot runtime semantics.

If a possible fix requires editing non-serializer baseline code, do not make that change silently.

- Stop and ask for explicit permission first.
- Include why serializer-only fixes are insufficient.
- Include the minimal non-serializer change proposed and risk scope.
- Proceed only after approval.

## Failed-Fix Rollback Rule

When attempting a non-serializer candidate fix for a snapshot failure:

1. Apply the smallest plausible change.
2. Run the targeted failing test immediately.
3. If the failure is not fixed, remove that specific change immediately.
4. Continue with the next hypothesis using fresh evidence.

Do not leave known-ineffective fixes in the tree.

Serializer-scoped edits may remain while iterating on targeted instrumentation and root-cause isolation.

## Practical Testing Method (Current Effective Loop)

1. Reproduce with one targeted snapshot test first.
2. Use bounded runtime commands to avoid hangs.
3. Keep stage-level diagnostics in place to identify save vs reset vs load failure.
4. Escalate to valgrind/gdb only after targeted repro is stable.
5. Periodically rerun broader vanilla checks to catch collateral regressions.

### Targeted command

```bash
cd /home/poiso/Projects/Soar
python3 scons/scons.py UnitTests -j2 && \
LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
./build/UnitTests/UnitTests --snapshot-every-step -l -c FunctionalTests -t "[snapshot] testTowersOfHanoiFast"
```

### Bounded valgrind command

```bash
cd /home/poiso/Projects/Soar
env LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
timeout 180 valgrind --track-origins=yes --num-callers=32 --leak-check=no \
./build/UnitTests/UnitTests --snapshot-every-step -l -c FunctionalTests -t "[snapshot] testTowersOfHanoiFast"
```

## Checkpoint Ledger (2026-04-13)

### FunctionalTests up through towers (vanilla)

All passed:

- `testWaterJug`
- `testWaterJugHierarchy`
- `testTowersOfHanoi`
- `testTowersOfHanoiFast`

Command:

```bash
cd /home/poiso/Projects/Soar
env LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
timeout 1200 ./build/UnitTests/UnitTests \
	--category FunctionalTests \
	--test testWaterJug \
	--test testWaterJugHierarchy \
	--test testTowersOfHanoi \
	--test testTowersOfHanoiFast
```

### FunctionalTests up through towers (snapshot)

Result:

- `[snapshot] testWaterJug` passed (with refcount leak warning in output)
- `[snapshot] testWaterJugHierarchy` passed (with refcount leak warning in output)
- `[snapshot] testTowersOfHanoi` crashed with assertion in `remove_from_hash_table` (`mem.cpp:422`)
- `[snapshot] testTowersOfHanoiFast` crashed with same assertion

Minimal repro commands:

```bash
cd /home/poiso/Projects/Soar
env LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
timeout 1200 ./build/UnitTests/UnitTests \
	--snapshot-every-step --category FunctionalTests \
	--test "[snapshot] testTowersOfHanoi"

env LD_LIBRARY_PATH=/home/poiso/Projects/Soar/build/Core:/home/poiso/Projects/Soar/build \
timeout 1200 ./build/UnitTests/UnitTests \
	--snapshot-every-step --category FunctionalTests \
	--test "[snapshot] testTowersOfHanoiFast"
```

### Additional easy FunctionalTests (vanilla vs snapshot)

Vanilla passed:

- `testEightPuzzle`
- `testBlocksWorld`
- `testBlocksWorldOperatorSubgoaling`
- `testBlocksWorldLookAhead`

Snapshot currently crashes at first test in this group:

- `[snapshot] testEightPuzzle` crashes with same assertion in `remove_from_hash_table` (`mem.cpp:422`)

### Towers Serializer Progress (2026-04-13, later)

Targeted test remains:

- `FunctionalTests::[snapshot] testTowersOfHanoiFast`

Confirmed progression achieved:

- First blocker was save-time staged parse failure after `export_live_match_instantiations`.
- Parse failure narrowed to non-live instantiation `apply*move-disk*add*upper-disk*source` during instantiated-condition WME export.
- Serializer-side export constraints removed the early save parse abort.
- Serializer-side restore gating avoided immediate `mem.cpp:422` abort on repeated restore cycles.

Current active blocker:

- Run advances much farther (roughly step ~100+) but then times out.
- Logs show repeated warnings: `can't find an existing instantiation ... to retract` for `apply*move-disk*...` productions.
- Follow-on symptom is repeated no-change impasse depth growth (goal stack depth exceeded 100).

Working hypothesis:

- Non-live/synthetic instantiation and pending assertion reconstruction is still incomplete/inconsistent.
- Next serializer path: restore non-live saved instantiation entries via fallback reconstruction while keeping parse-safe export constraints.
