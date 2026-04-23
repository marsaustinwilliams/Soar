#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
from pathlib import Path


DEFAULT_PHASES = ["input", "propose", "decide", "apply", "output"]
SUPPORTED_PHASES = {"input", "propose", "decide", "apply", "output"}


def run_compare(compare_script, repo_root, args, env):
    cmd = [
        str(compare_script),
        "--category",
        args.category,
        "--test",
        args.test,
        "--decision-cycle",
        str(args.decision_cycle),
        "--phase",
        args.phase,
        "--output-dir",
        str(args.output_dir),
        "--gdb-timeout",
        str(args.gdb_timeout),
        "--max-wmes",
        str(args.max_wmes),
        "--repo-root",
        str(repo_root),
    ]
    if args.snapshot_test:
        cmd.extend(["--snapshot-test", args.snapshot_test])
    if args.unit_tests_bin:
        cmd.extend(["--unit-tests-bin", args.unit_tests_bin])
    if args.keep_addresses:
        cmd.append("--keep-addresses")
    if args.normalize_timer_fields:
        cmd.append("--normalize-timer-fields")
    if args.normalize_rete_junk:
        cmd.append("--normalize-rete-junk")
    if args.normalize_wme_junk:
        cmd.append("--normalize-wme-junk")
    if args.normalize_symbol_junk:
        cmd.append("--normalize-symbol-junk")
    if args.normalize_goal_decider_junk:
        cmd.append("--normalize-goal-decider-junk")
    if args.normalize_matchset_junk:
        cmd.append("--normalize-matchset-junk")
    if args.normalize_io_junk:
        cmd.append("--normalize-io-junk")
    if args.normalize_slot_junk:
        cmd.append("--normalize-slot-junk")
    if args.wme_only_recursive:
        cmd.append("--wme-only-recursive")
    if args.symbol_only_recursive:
        cmd.append("--symbol-only-recursive")
    if args.goal_decider_only_recursive:
        cmd.append("--goal-decider-only-recursive")
    if args.matchset_only_recursive:
        cmd.append("--matchset-only-recursive")
    if args.io_only_recursive:
        cmd.append("--io-only-recursive")
    if args.slot_only_recursive:
        cmd.append("--slot-only-recursive")
    if args.run_debug_mode:
        cmd.append("--run-debug-mode")

    result = subprocess.run(
        cmd,
        cwd=repo_root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return result.returncode, result.stdout


def extract_field_diff_summary(diff_report, max_lines):
    if not diff_report.exists():
        return []
    lines = diff_report.read_text(encoding="utf-8", errors="replace").splitlines()
    field_lines = [line for line in lines if line.startswith("+FIELD ") or line.startswith("-FIELD ")]
    return field_lines[:max_lines]


def parse_phase_list(raw):
    phases = [p.strip().lower() for p in raw.split(",") if p.strip()]
    invalid = [p for p in phases if p not in SUPPORTED_PHASES]
    if invalid:
        raise ValueError(
            "Unsupported phase(s): "
            + ", ".join(invalid)
            + ". Supported phases are: "
            + ", ".join(sorted(SUPPORTED_PHASES))
        )
    return phases


def build_step_output_dir(base_output_dir, cycle, phase):
    return base_output_dir / f"dc{cycle:04d}" / phase


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Run phase-by-phase snapshot parity checks across decision cycles. "
            "At each cycle/phase checkpoint, compare vanilla vs snapshot-load state and stop on first diff."
        )
    )
    parser.add_argument("--category", default="FunctionalTests", help="Unit test category")
    parser.add_argument("--test", required=True, help="Vanilla test name, e.g. testWaterJug")
    parser.add_argument(
        "--snapshot-test",
        default=None,
        help="Snapshot test name. Default: [snapshot] <test>",
    )
    parser.add_argument(
        "--phases",
        default=",".join(DEFAULT_PHASES),
        help="Comma-separated phase sequence, default: input,propose,apply,output",
    )
    parser.add_argument(
        "--start-decision-cycle",
        type=int,
        default=1,
        help="First decision cycle to check (1-based)",
    )
    parser.add_argument(
        "--max-decision-cycles",
        type=int,
        required=True,
        help="Last decision cycle to check (inclusive)",
    )
    parser.add_argument(
        "--output-dir",
        default="/tmp/soar_debug/kernel_state_compare_sweep",
        help="Base directory for all per-step compare artifacts",
    )
    parser.add_argument(
        "--compare-script",
        default=None,
        help="Optional path to compare_kernel_state.py. Defaults to sibling file.",
    )
    parser.add_argument(
        "--unit-tests-bin",
        default="./build/UnitTests/UnitTests",
        help="Path to UnitTests binary",
    )
    parser.add_argument(
        "--gdb-timeout",
        type=int,
        default=20,
        help="Timeout in seconds for each vanilla/snapshot capture",
    )
    parser.add_argument(
        "--run-debug-mode",
        action="store_true",
        help="Pass -r through to compare_kernel_state.py to reduce strict test timeouts under gdb",
    )
    parser.add_argument(
        "--max-wmes",
        type=int,
        default=500,
        help="Max WMEs to include per dump",
    )
    parser.add_argument(
        "--keep-addresses",
        action="store_true",
        help="Keep raw addresses in normalized output",
    )
    parser.add_argument(
        "--normalize-timer-fields",
        action="store_true",
        help=(
            "Normalize volatile timer runtime/accumulator fields "
            "(timers_cpu/kernel/phase, timers_total_*, callback_timers)."
        ),
    )
    parser.add_argument(
        "--normalize-rete-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic RETE recursive noise "
            "(inactive union branches, hash bucket internals, render_error lines)."
        ),
    )
    parser.add_argument(
        "--normalize-wme-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic WME recursive noise "
            "(deep symbol internals and pointer/list linkage under all_wmes_in_rete)."
        ),
    )
    parser.add_argument(
        "--normalize-symbol-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic symbol-manager recursive noise "
            "while preserving SYMBOL_SUMMARY/SYMBOL_SIGNATURE semantic lines."
        ),
    )
    parser.add_argument(
        "--normalize-goal-decider-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic Goal+Decider recursive noise "
            "while preserving GOAL_DECIDER_SUMMARY/GOAL_DECIDER_SIGNATURE lines."
        ),
    )
    parser.add_argument(
        "--normalize-matchset-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic Match-set recursive noise "
            "while preserving MATCHSET_SUMMARY/MATCHSET_SIGNATURE lines."
        ),
    )
    parser.add_argument(
        "--normalize-io-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic IO recursive noise "
            "while preserving IO_SUMMARY/IO_SIGNATURE lines."
        ),
    )
    parser.add_argument(
        "--normalize-slot-junk",
        action="store_true",
        help=(
            "Normalize known non-semantic slot/pref recursive noise "
            "while preserving SLOT_SUMMARY/SLOT_SIGNATURE lines."
        ),
    )
    parser.add_argument(
        "--wme-only-recursive",
        action="store_true",
        help=(
            "When compare script uses recursive dumps, recurse only from WME root "
            "(all_wmes_in_rete)."
        ),
    )
    parser.add_argument(
        "--symbol-only-recursive",
        action="store_true",
        help=(
            "When compare script uses recursive dumps, recurse only from symbol manager root "
            "(agent.symbolManager)."
        ),
    )
    parser.add_argument(
        "--goal-decider-only-recursive",
        action="store_true",
        help=(
            "When compare script uses recursive dumps, recurse only from Goal+Decider roots "
            "(top_goal, bottom_goal, active_goal, Decider)."
        ),
    )
    parser.add_argument(
        "--matchset-only-recursive",
        action="store_true",
        help=(
            "When compare script uses recursive dumps, recurse only from match-set roots "
            "(ms_i_assertions, ms_o_assertions, postponed_assertions)."
        ),
    )
    parser.add_argument(
        "--io-only-recursive",
        action="store_true",
        help=(
            "When compare script uses recursive dumps, recurse only from IO roots "
            "(io_header*, io_header_link, existing_output_links, collected_io_wmes)."
        ),
    )
    parser.add_argument(
        "--slot-only-recursive",
        action="store_true",
        help=(
            "When compare script uses recursive dumps, recurse only from slot ownership roots "
            "(changed_slots, context_slots_with_changed_accept_prefs, slots_for_possible_removal)."
        ),
    )
    parser.add_argument(
        "--show-field-diff-lines",
        type=int,
        default=40,
        help="Max FIELD diff lines to print on first mismatch",
    )
    parser.add_argument(
        "--continue-on-diff",
        action="store_true",
        help="Do not halt on first diff; continue through all checkpoints",
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="Optional repository root path override",
    )

    args = parser.parse_args()

    if args.start_decision_cycle < 1:
        print("--start-decision-cycle must be >= 1", file=sys.stderr)
        return 2
    if args.max_decision_cycles < args.start_decision_cycle:
        print("--max-decision-cycles must be >= --start-decision-cycle", file=sys.stderr)
        return 2

    try:
        phases = parse_phase_list(args.phases)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    script_path = Path(__file__).resolve()
    if args.repo_root:
        repo_root = Path(args.repo_root).resolve()
    else:
        repo_root = script_path.parents[2]

    compare_script = Path(args.compare_script).resolve() if args.compare_script else script_path.with_name("compare_kernel_state.py")
    if not compare_script.exists():
        print(f"compare script not found: {compare_script}", file=sys.stderr)
        return 2

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    snapshot_test = args.snapshot_test or f"[snapshot] {args.test}"

    failures = 0
    completed = 0
    total = (args.max_decision_cycles - args.start_decision_cycle + 1) * len(phases)

    print("Starting sweep")
    print(f"  test={args.test}")
    print(f"  snapshot_test={snapshot_test}")
    print(f"  cycles={args.start_decision_cycle}..{args.max_decision_cycles}")
    print(f"  phases={','.join(phases)}")
    print(f"  output_dir={output_dir}")

    for cycle in range(args.start_decision_cycle, args.max_decision_cycles + 1):
        for phase in phases:
            completed += 1
            step_dir = build_step_output_dir(output_dir, cycle, phase)
            step_dir.mkdir(parents=True, exist_ok=True)

            env = os.environ.copy()
            env["SOAR_SNAPSHOT_STOP_PHASE"] = phase

            call_args = argparse.Namespace(**vars(args))
            call_args.decision_cycle = cycle
            call_args.phase = phase
            call_args.output_dir = step_dir
            call_args.snapshot_test = snapshot_test

            print(f"[{completed}/{total}] dc={cycle} phase={phase}")
            rc, out = run_compare(compare_script, repo_root, call_args, env)
            if rc != 0:
                print(out, end="")
                print(
                    f"ERROR: compare call failed at dc={cycle}, phase={phase}, rc={rc}.",
                    file=sys.stderr,
                )
                return 1

            raw_v = step_dir / f"raw_vanilla_dc{cycle}.txt"
            raw_s = step_dir / f"raw_snapshot_dc{cycle}.txt"
            diff = step_dir / f"diff_report_dc{cycle}.txt"

            raw_v_text = raw_v.read_text(encoding="utf-8", errors="replace") if raw_v.exists() else ""
            raw_s_text = raw_s.read_text(encoding="utf-8", errors="replace") if raw_s.exists() else ""
            if not raw_v_text.strip() or not raw_s_text.strip():
                print(
                    f"ERROR: missing capture block at dc={cycle}, phase={phase}. "
                    f"See {step_dir}.",
                    file=sys.stderr,
                )
                return 1

            diff_size = diff.stat().st_size if diff.exists() else 0
            if diff_size > 0:
                failures += 1
                print(f"DIFF detected at dc={cycle}, phase={phase}: {diff}")
                for line in extract_field_diff_summary(diff, args.show_field_diff_lines):
                    print(line)
                if not args.continue_on_diff:
                    print("Halting on first mismatch.")
                    return 1

    if failures:
        print(f"Sweep completed with {failures} mismatching checkpoint(s).")
        return 1

    print("Sweep completed: all checkpoints matched.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
