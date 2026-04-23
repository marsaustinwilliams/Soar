#!/usr/bin/env python3

import argparse
import difflib
import os
import random
import re
import shutil
import sys
from pathlib import Path

from compare_kernel_state import (
    PHASE_ENUM_MAP,
    PHASE_STOP_ENV_MAP,
    extract_block,
    gdb_helper_py,
    normalize_dump,
    run_cmd,
    vanilla_gdb,
    write_text,
)


def parse_int_csv(raw):
    values = []
    for piece in raw.split(","):
        piece = piece.strip()
        if not piece:
            continue
        values.append(int(piece))
    return values


def evenly_spaced_cycles(start_cycle, end_cycle, count):
    if count <= 0:
        return []
    total = end_cycle - start_cycle + 1
    if count >= total:
        return list(range(start_cycle, end_cycle + 1))
    if count == 1:
        return [end_cycle]

    points = []
    for i in range(count):
        pos = start_cycle + round((end_cycle - start_cycle) * (i / (count - 1)))
        points.append(pos)

    dedup = sorted(set(points))
    if dedup[-1] != end_cycle:
        dedup[-1] = end_cycle
    if dedup[0] != start_cycle:
        dedup[0] = start_cycle
    return dedup


def snapshot_count_until(cycle, step_start, step_end, stride):
    cap_end = min(cycle, step_end)
    if cap_end < step_start:
        return 0

    if stride <= 1:
        return (cap_end - step_start) + 1

    first = step_start
    remainder = first % stride
    if remainder != 0:
        first += (stride - remainder)
    if first > cap_end:
        return 0

    return ((cap_end - first) // stride) + 1


def snapshot_gdb_compound(
    category,
    snapshot_test_name,
    cycle,
    step_start,
    step_end,
    stride,
    helper_path,
    phase_enum,
    phase_name,
    run_debug_mode,
    min_restores,
):
    debug_arg = "-r " if run_debug_mode else ""
    phase_stop_value = PHASE_STOP_ENV_MAP.get(phase_name, "apply") if phase_name else "apply"
    stride_arg = f" --snapshot-stride {stride}" if stride > 1 else ""

    return f"""set pagination off
set confirm off
set breakpoint pending off
set debuginfod enabled off
set print pretty off
set print elements 0
set print repeats 0
set env SOAR_SNAPSHOT_STEP_START {step_start}
set env SOAR_SNAPSHOT_STEP_END {step_end}
set env SOAR_SNAPSHOT_STOP_PHASE {phase_stop_value}
set args {debug_arg}--snapshot-every-step{stride_arg} -l -c \"{category}\" -t \"{snapshot_test_name}\"
start
sharedlibrary libSoar
source {helper_path}
set $restore_count = 0
break restore_agent_state_message
commands
  silent
  set $restore_count = $restore_count + 1
  continue
end
break do_one_top_level_phase(agent_struct*)
commands
  silent
  set $ag = $rdi
    if ($restore_count >= {min_restores})
    python maybe_dump_and_quit(\"$ag\", \"SNAPSHOT_LOAD_DC{cycle}\", {cycle}, {phase_enum if phase_enum is not None else 'None'})
  end
  continue
end
continue
"""


def semantic_diff_count(diff_text):
    pattern = re.compile(r"^[+-](?!\+\+\+|---)")
    lines = []
    for line in diff_text.splitlines():
        if not pattern.match(line):
            continue
        if re.search(r"timer|callback|last_derived|max_dc_time_usec", line):
            continue
        lines.append(line)
    return len(lines)


def refcount_error_count(log_text):
    pattern = re.compile(r"refcount leak of\s+\d+\s+identifiers\s+detected", re.IGNORECASE)
    return len(pattern.findall(log_text))


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Compounded snapshot drift checker. Keeps compare_kernel_state.py unchanged while "
            "running one snapshot process that performs repeated snapshot/restore operations "
            "before a capture cycle, then compares against vanilla at the same cycle."
        )
    )
    parser.add_argument("--category", default="FunctionalTests")
    parser.add_argument("--test", required=True)
    parser.add_argument("--snapshot-test", default=None)
    parser.add_argument("--phase", choices=["input", "propose", "decide", "apply", "output"], default="decide")
    parser.add_argument("--start-decision-cycle", type=int, default=1)
    parser.add_argument("--max-decision-cycles", type=int, required=True)
    parser.add_argument("--checkpoints", default=None, help="Comma list of capture decision cycles")
    parser.add_argument("--checkpoint-count", type=int, default=8)
    parser.add_argument("--random-checkpoints", action="store_true")
    parser.add_argument("--seed", type=int, default=42)

    parser.add_argument("--snapshot-step-start", type=int, default=1)
    parser.add_argument(
        "--snapshot-step-end",
        type=int,
        default=None,
        help="Absolute upper bound for snapshot injections. Default: current checkpoint cycle.",
    )
    parser.add_argument("--snapshot-stride", type=int, default=200)
    parser.add_argument(
        "--min-restores",
        type=int,
        default=1,
        help="Require at least this many restore events before capture can trigger.",
    )

    parser.add_argument("--output-dir", default="/tmp/soar_debug/kernel_state_compare_compound")
    parser.add_argument("--unit-tests-bin", default="./build/UnitTests/UnitTests")
    parser.add_argument("--run-debug-mode", action="store_true")
    parser.add_argument("--gdb-timeout", type=int, default=90)
    parser.add_argument("--max-wmes", type=int, default=500)
    parser.add_argument("--recursive-depth", type=int, default=0)
    parser.add_argument("--recursive-max-nodes", type=int, default=2000)
    parser.add_argument(
        "--rete-only-recursive",
        action="store_true",
        help=(
            "When --recursive-depth > 0, recurse only from RETE roots "
            "(all_wmes_in_rete, dummy_top_node, ms_* lists, alpha hash tables)."
        ),
    )
    parser.add_argument(
        "--wme-only-recursive",
        action="store_true",
        help="When --recursive-depth > 0, recurse only from WME root (all_wmes_in_rete).",
    )
    parser.add_argument(
        "--symbol-only-recursive",
        action="store_true",
        help="When --recursive-depth > 0, recurse only from symbol manager root (agent.symbolManager).",
    )
    parser.add_argument(
        "--goal-decider-only-recursive",
        action="store_true",
        help=(
            "When --recursive-depth > 0, recurse only from Goal+Decider roots "
            "(top_goal, bottom_goal, active_goal, Decider)."
        ),
    )
    parser.add_argument(
        "--matchset-only-recursive",
        action="store_true",
        help=(
            "When --recursive-depth > 0, recurse only from match-set roots "
            "(ms_i_assertions, ms_o_assertions, postponed_assertions)."
        ),
    )
    parser.add_argument(
        "--io-only-recursive",
        action="store_true",
        help=(
            "When --recursive-depth > 0, recurse only from IO roots "
            "(io_header*, io_header_link, existing_output_links, collected_io_wmes)."
        ),
    )
    parser.add_argument(
        "--slot-only-recursive",
        action="store_true",
        help=(
            "When --recursive-depth > 0, recurse only from slot ownership roots "
            "(changed_slots, context_slots_with_changed_accept_prefs, slots_for_possible_removal)."
        ),
    )

    parser.add_argument("--keep-addresses", action="store_true")
    parser.add_argument("--normalize-timer-fields", action="store_true")
    parser.add_argument("--normalize-rete-junk", action="store_true")
    parser.add_argument("--normalize-wme-junk", action="store_true")
    parser.add_argument("--normalize-symbol-junk", action="store_true")
    parser.add_argument("--normalize-goal-decider-junk", action="store_true")
    parser.add_argument("--normalize-matchset-junk", action="store_true")
    parser.add_argument("--normalize-io-junk", action="store_true")
    parser.add_argument("--normalize-slot-junk", action="store_true")

    parser.add_argument(
        "--drift-criteria",
        choices=["diff", "semantic-diff", "refcount-error"],
        default="diff",
        help=(
            "How drift is defined: diff=any unified diff line, semantic-diff=non-timer/callback diff lines, "
            "refcount-error=refcount leak errors in test output."
        ),
    )
    parser.add_argument(
        "--refcount-scope",
        choices=["snapshot", "either"],
        default="snapshot",
        help="When --drift-criteria refcount-error is used, check snapshot output only or either run.",
    )
    parser.add_argument(
        "--halt-on-refcount-error",
        action="store_true",
        help="Stop immediately at first checkpoint whose output contains refcount leak errors.",
    )
    parser.add_argument("--halt-on-drift", action="store_true")
    parser.add_argument("--show-field-diff-lines", type=int, default=30)
    parser.add_argument("--repo-root", default=None)

    args = parser.parse_args()

    if args.start_decision_cycle < 1:
        print("--start-decision-cycle must be >= 1", file=sys.stderr)
        return 2
    if args.max_decision_cycles < args.start_decision_cycle:
        print("--max-decision-cycles must be >= --start-decision-cycle", file=sys.stderr)
        return 2
    if args.snapshot_step_start < 1:
        print("--snapshot-step-start must be >= 1", file=sys.stderr)
        return 2
    if args.snapshot_stride < 1:
        print("--snapshot-stride must be >= 1", file=sys.stderr)
        return 2
    if args.min_restores < 1:
        print("--min-restores must be >= 1", file=sys.stderr)
        return 2

    if args.repo_root:
        repo_root = Path(args.repo_root).resolve()
    else:
        repo_root = Path(__file__).resolve().parents[2]

    gdb_bin = shutil.which("gdb")
    if not gdb_bin:
        print("gdb not found on PATH", file=sys.stderr)
        return 2

    if args.checkpoints:
        checkpoints = sorted(set(parse_int_csv(args.checkpoints)))
    else:
        all_cycles = list(range(args.start_decision_cycle, args.max_decision_cycles + 1))
        if args.random_checkpoints:
            random.seed(args.seed)
            if args.checkpoint_count >= len(all_cycles):
                checkpoints = all_cycles
            else:
                checkpoints = sorted(random.sample(all_cycles, args.checkpoint_count))
        else:
            checkpoints = evenly_spaced_cycles(
                args.start_decision_cycle,
                args.max_decision_cycles,
                args.checkpoint_count,
            )

    if not checkpoints:
        print("No checkpoints selected.", file=sys.stderr)
        return 2

    bad_points = [c for c in checkpoints if c < args.start_decision_cycle or c > args.max_decision_cycles]
    if bad_points:
        print("Checkpoint out of range: " + ", ".join(map(str, bad_points)), file=sys.stderr)
        return 2

    out_root = Path(args.output_dir).resolve()
    out_root.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.setdefault("LD_LIBRARY_PATH", f"{repo_root / 'build' / 'Core'}:{repo_root / 'build'}")

    snapshot_test = args.snapshot_test or f"[snapshot] {args.test}"
    phase_enum = PHASE_ENUM_MAP.get(args.phase) if args.phase else None

    summary_rows = []
    halted_on_drift = False

    for cycle in checkpoints:
        step_end = args.snapshot_step_end if args.snapshot_step_end is not None else cycle
        if step_end > cycle:
            step_end = cycle

        cycle_dir = out_root / f"dc{cycle:04d}"
        cycle_dir.mkdir(parents=True, exist_ok=True)

        helper_py = cycle_dir / "gdb_dump_agent_fields.py"
        vanilla_gdb_file = cycle_dir / "capture_vanilla.gdb"
        snapshot_gdb_file = cycle_dir / "capture_snapshot_compound.gdb"
        vanilla_log = cycle_dir / "gdb_vanilla.log"
        snapshot_log = cycle_dir / "gdb_snapshot.log"

        raw_vanilla = cycle_dir / f"raw_vanilla_dc{cycle}.txt"
        raw_snapshot = cycle_dir / f"raw_snapshot_dc{cycle}.txt"
        norm_vanilla = cycle_dir / f"norm_vanilla_dc{cycle}.txt"
        norm_snapshot = cycle_dir / f"norm_snapshot_dc{cycle}.txt"
        diff_report = cycle_dir / f"diff_report_dc{cycle}.txt"

        write_text(
            helper_py,
            gdb_helper_py(
                args.max_wmes,
                args.recursive_depth,
                args.recursive_max_nodes,
                args.rete_only_recursive,
                args.wme_only_recursive,
                args.symbol_only_recursive,
                args.goal_decider_only_recursive,
                args.matchset_only_recursive,
                args.io_only_recursive,
                args.slot_only_recursive,
            ),
        )
        write_text(
            vanilla_gdb_file,
            vanilla_gdb(
                args.category,
                args.test,
                cycle,
                helper_py,
                phase_enum,
                run_debug_mode=args.run_debug_mode,
            ),
        )
        write_text(
            snapshot_gdb_file,
            snapshot_gdb_compound(
                args.category,
                snapshot_test,
                cycle,
                args.snapshot_step_start,
                step_end,
                args.snapshot_stride,
                helper_py,
                phase_enum,
                args.phase,
                args.run_debug_mode,
                args.min_restores,
            ),
        )

        rc_vanilla, err_vanilla = run_cmd(
            [gdb_bin, "-q", "--batch", "-x", str(vanilla_gdb_file), str(args.unit_tests_bin)],
            cwd=repo_root,
            timeout_sec=args.gdb_timeout,
            out_path=vanilla_log,
            env=env,
        )
        if err_vanilla:
            print(f"dc{cycle}: vanilla capture failed: {err_vanilla}", file=sys.stderr)
            return 1

        rc_snapshot, err_snapshot = run_cmd(
            [gdb_bin, "-q", "--batch", "-x", str(snapshot_gdb_file), str(args.unit_tests_bin)],
            cwd=repo_root,
            timeout_sec=args.gdb_timeout,
            out_path=snapshot_log,
            env=env,
        )
        if err_snapshot:
            print(f"dc{cycle}: snapshot capture failed: {err_snapshot}", file=sys.stderr)
            return 1

        vanilla_text = vanilla_log.read_text(encoding="utf-8", errors="replace")
        snapshot_text = snapshot_log.read_text(encoding="utf-8", errors="replace")

        begin_v = f"BEGIN_VANILLA_DC{cycle}"
        end_v = f"END_VANILLA_DC{cycle}"
        begin_s = f"BEGIN_SNAPSHOT_LOAD_DC{cycle}"
        end_s = f"END_SNAPSHOT_LOAD_DC{cycle}"

        raw_v = extract_block(vanilla_text, begin_v, end_v)
        raw_s = extract_block(snapshot_text, begin_s, end_s)

        write_text(raw_vanilla, raw_v)
        write_text(raw_snapshot, raw_s)

        norm_v = raw_v if args.keep_addresses else normalize_dump(
            raw_v,
            normalize_timer_fields=args.normalize_timer_fields,
            normalize_rete_junk=args.normalize_rete_junk,
            normalize_wme_junk=args.normalize_wme_junk,
            normalize_symbol_junk=args.normalize_symbol_junk,
            normalize_goal_decider_junk=args.normalize_goal_decider_junk,
            normalize_matchset_junk=args.normalize_matchset_junk,
            normalize_io_junk=args.normalize_io_junk,
            normalize_slot_junk=args.normalize_slot_junk,
        )
        norm_s = raw_s if args.keep_addresses else normalize_dump(
            raw_s,
            normalize_timer_fields=args.normalize_timer_fields,
            normalize_rete_junk=args.normalize_rete_junk,
            normalize_wme_junk=args.normalize_wme_junk,
            normalize_symbol_junk=args.normalize_symbol_junk,
            normalize_goal_decider_junk=args.normalize_goal_decider_junk,
            normalize_matchset_junk=args.normalize_matchset_junk,
            normalize_io_junk=args.normalize_io_junk,
            normalize_slot_junk=args.normalize_slot_junk,
        )

        write_text(norm_vanilla, norm_v)
        write_text(norm_snapshot, norm_s)

        diff_lines = list(
            difflib.unified_diff(
                norm_v.splitlines(keepends=True),
                norm_s.splitlines(keepends=True),
                fromfile=str(norm_vanilla.name),
                tofile=str(norm_snapshot.name),
            )
        )
        diff_text = "".join(diff_lines)
        write_text(diff_report, diff_text)

        non_header = [
            line for line in diff_text.splitlines()
            if (line.startswith("+") or line.startswith("-")) and not line.startswith("+++") and not line.startswith("---")
        ]
        sem_count = semantic_diff_count(diff_text)
        vanilla_refcount_errors = refcount_error_count(vanilla_text)
        snapshot_refcount_errors = refcount_error_count(snapshot_text)
        restore_count = snapshot_count_until(cycle, args.snapshot_step_start, step_end, args.snapshot_stride)
        if args.drift_criteria == "refcount-error":
            if args.refcount_scope == "either":
                drift = (vanilla_refcount_errors + snapshot_refcount_errors) > 0
            else:
                drift = snapshot_refcount_errors > 0
        elif args.drift_criteria == "semantic-diff":
            drift = sem_count > 0
        else:
            drift = len(non_header) > 0

        summary_rows.append(
            {
                "cycle": cycle,
                "snapshot_restores_before_capture": restore_count,
                "vanilla_rc": rc_vanilla,
                "snapshot_rc": rc_snapshot,
                "vanilla_dump": bool(raw_v.strip()),
                "snapshot_dump": bool(raw_s.strip()),
                "diff_lines": len(diff_lines),
                "semantic_diff_lines": sem_count,
                "vanilla_refcount_errors": vanilla_refcount_errors,
                "snapshot_refcount_errors": snapshot_refcount_errors,
                "drift": drift,
            }
        )

        status = "DRIFT" if drift else "OK"
        print(
            f"dc={cycle} restores={restore_count} status={status} diff_lines={len(non_header)} "
            f"semantic_diff_lines={sem_count} snapshot_refcount_errors={snapshot_refcount_errors} "
            f"vanilla_refcount_errors={vanilla_refcount_errors}"
        )

        if drift and args.show_field_diff_lines > 0:
            shown = 0
            for line in non_header:
                if "FIELD " not in line:
                    continue
                print(line)
                shown += 1
                if shown >= args.show_field_diff_lines:
                    break

        if drift and args.halt_on_drift:
            halted_on_drift = True
            break

        has_refcount_error = (
            (snapshot_refcount_errors > 0)
            if args.refcount_scope == "snapshot"
            else (snapshot_refcount_errors > 0 or vanilla_refcount_errors > 0)
        )
        if args.halt_on_refcount_error and has_refcount_error:
            halted_on_drift = True
            break

    summary_path = out_root / "summary.tsv"
    with open(summary_path, "w", encoding="utf-8") as out:
        out.write(
            "cycle\trestores\tvanilla_rc\tsnapshot_rc\tvanilla_dump\tsnapshot_dump\tdiff_lines\tsemantic_diff_lines\t"
            "vanilla_refcount_errors\tsnapshot_refcount_errors\tdrift\n"
        )
        for row in summary_rows:
            out.write(
                f"{row['cycle']}\t{row['snapshot_restores_before_capture']}\t{row['vanilla_rc']}\t{row['snapshot_rc']}\t"
                f"{int(row['vanilla_dump'])}\t{int(row['snapshot_dump'])}\t{row['diff_lines']}\t"
                f"{row['semantic_diff_lines']}\t{row['vanilla_refcount_errors']}\t{row['snapshot_refcount_errors']}\t"
                f"{int(row['drift'])}\n"
            )

    print("Generated summary:")
    print(summary_path)

    if halted_on_drift:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
