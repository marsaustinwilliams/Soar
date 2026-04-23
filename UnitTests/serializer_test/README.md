# Kernel State Compare Helper

This folder contains a helper script to compare vanilla kernel state against snapshot-reload kernel state at the same decision cycle (and optional phase).

## Script

- `compare_kernel_state.py`
- `compare_kernel_state_compound.py`

## What It Does

The script runs two GDB captures against `UnitTests`:

1. Vanilla run (`--category/--test`)
2. Snapshot run (`--snapshot-every-step` with `[snapshot] <test>` by default)

It extracts structured agent dumps from both runs, normalizes pointer-heavy output, and writes a unified diff.

## Requirements

- Build with debug symbols (recommended for reliable breakpoints):

```bash
cd /home/poiso/Projects/Soar
python3 scons/scons.py UnitTests --dbg -j2
```

- `gdb` on PATH
- `LD_LIBRARY_PATH` should include:
  - `/home/poiso/Projects/Soar/build/Core`
  - `/home/poiso/Projects/Soar/build`

The script sets `LD_LIBRARY_PATH` automatically if missing, but exported values are fine too.

## Common Usage

### Phase-agnostic compare at decision cycle 1

```bash
cd /home/poiso/Projects/Soar
./UnitTests/serializer_test/compare_kernel_state.py \
  --category FunctionalTests \
  --test testWaterJug \
  --decision-cycle 1 \
  --output-dir /tmp/soar_debug/kernel_state_compare_waterjug_dc1
```

### Phase-aligned compare at decision cycle 1 (propose)

```bash
cd /home/poiso/Projects/Soar
SOAR_SNAPSHOT_STOP_PHASE=propose \
./UnitTests/serializer_test/compare_kernel_state.py \
  --category FunctionalTests \
  --test testWaterJug \
  --decision-cycle 1 \
  --phase propose \
  --output-dir /tmp/soar_debug/kernel_state_compare_waterjug_dc1_propose
```

### Specify snapshot test name explicitly

```bash
./UnitTests/serializer_test/compare_kernel_state.py \
  --category FunctionalTests \
  --test testWaterJug \
  --snapshot-test "[snapshot] testWaterJug" \
  --decision-cycle 1
```

## Outputs

For decision cycle `N`, the script writes:

- `raw_vanilla_dcN.txt`
- `raw_snapshot_dcN.txt`
- `norm_vanilla_dcN.txt`
- `norm_snapshot_dcN.txt`
- `diff_report_dcN.txt`
- `gdb_vanilla.log`
- `gdb_snapshot.log`

Use `diff_report_dcN.txt` first, then inspect raw files/logs for root-cause details.

## Compound Drift Script

`compare_kernel_state_compound.py` is a companion tool that keeps `compare_kernel_state.py` unchanged.
It captures vanilla vs snapshot state at selected decision cycles while the snapshot side performs
multiple restore cycles in the same run (compounding effect).

Key controls:

- `--snapshot-stride N`: perform snapshot/restore every N decision cycles (instead of every cycle)
- `--checkpoints a,b,c` or `--checkpoint-count K`: choose capture cycles
- `--random-checkpoints --seed S`: spread checks at random divided points
- `--halt-on-drift`: stop at the first checkpoint that shows a diff
- `--drift-criteria refcount-error`: define drift as refcount leak errors (instead of diff lines)
- `--refcount-scope snapshot|either`: only snapshot-side refcount errors (default) or either run
- `--halt-on-refcount-error`: stop at the first checkpoint that emits refcount leak errors
- `--min-restores N`: only allow capture after at least N restore events

Example:

```bash
cd /home/poiso/Projects/Soar
./UnitTests/serializer_test/compare_kernel_state_compound.py \
  --category FunctionalTests \
  --test testTowersOfHanoiFast \
  --phase decide \
  --start-decision-cycle 100 \
  --max-decision-cycles 2047 \
  --checkpoint-count 10 \
  --random-checkpoints \
  --seed 42 \
  --snapshot-stride 500 \
  --snapshot-step-start 1 \
  --run-debug-mode \
  --normalize-timer-fields \
  --halt-on-drift \
  --output-dir /tmp/soar_debug/kernel_state_compare_compound_hanoi
```

Outputs are written per checkpoint under the output directory and summarized in:

- `summary.tsv`

### Refcount-driven drift stop

Use this when you want drift to mean refcount leakage and halt as soon as it appears:

```bash
cd /home/poiso/Projects/Soar
python3 ./UnitTests/serializer_test/compare_kernel_state_compound.py \
  --category FunctionalTests \
  --test testTowersOfHanoiFast \
  --phase decide \
  --start-decision-cycle 1 \
  --max-decision-cycles 2047 \
  --checkpoint-count 2047 \
  --snapshot-stride 500 \
  --snapshot-step-start 1 \
  --drift-criteria refcount-error \
  --refcount-scope snapshot \
  --halt-on-drift \
  --halt-on-refcount-error \
  --output-dir /tmp/soar_debug/kernel_state_compare_compound_refcount
```

`summary.tsv` now includes `vanilla_refcount_errors` and `snapshot_refcount_errors` columns.

## Important Flags

- `--phase input|propose|apply|output`
  - Requires the capture to hit that exact phase on both sides.
- `--gdb-timeout <sec>`
  - Timeout per GDB run (default: 180).
- `--max-wmes <count>`
  - Limits WME dump size (default: 500).
- `--keep-addresses`
  - Keeps raw pointer addresses in normalized output (normally stripped).

## Interpreting Results

- If both `raw_*` files are non-empty and `MS_COUNTS` matches, captures are usually phase/cycle-aligned.
- Differences in pointer values are expected and normalized away.
- Differences in persistent counters/flags may indicate true serializer drift.

## Current Status (Apr 16, 2026)

At WaterJug DC1/propose, parity is now achieved for this bookkeeping set:

- `current_retesave_amindex`
- `reteload_num_ams`
- `current_retesave_symindex`
- `reteload_num_syms`
- `alpha_mem_id_counter`
- `beta_node_id_counter`
- `current_tc_number`
- `name_of_production_being_reordered`

Remaining differences still observed in:

- `reason_for_stopping`
- timer/callback fields (`timers_*`, `callback_timers`)
- `tf_printing_tc`
- `output_link_tc_num`

These are the next parity targets if strict full-state equality is required.

## Troubleshooting

- Empty vanilla/snapshot dump:
  - Check `gdb_vanilla.log` / `gdb_snapshot.log` for missed breakpoints.
  - Rebuild with `--dbg`.
- Script times out:
  - Increase `--gdb-timeout`.
- Unexpected phase behavior:
  - Ensure `SOAR_SNAPSHOT_STOP_PHASE` matches `--phase` for snapshot runs.
