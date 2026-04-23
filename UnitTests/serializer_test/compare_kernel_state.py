#!/usr/bin/env python3

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


PHASE_ENUM_MAP = {
    "input": 0,
    "propose": 1,
    "decide": 2,
    "apply": 3,
    "output": 4,
}

PHASE_STOP_ENV_MAP = {
    "input": "input",
    "propose": "propose",
    "decide": "decision",
    "apply": "apply",
    "output": "output",
}


def run_cmd(cmd, cwd, timeout_sec, out_path, env=None):
    with open(out_path, "w", encoding="utf-8") as out:
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                stdout=out,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=timeout_sec,
                check=False,
                env=env,
            )
            return result.returncode, None
        except subprocess.TimeoutExpired:
            return 124, f"Timed out after {timeout_sec}s"


def extract_block(log_text, begin_tag, end_tag):
    start = log_text.find(begin_tag)
    if start < 0:
        return ""
    end = log_text.find(end_tag, start)
    if end < 0:
        return ""
    end += len(end_tag)
    return log_text[start:end] + "\n"


def normalize_dump(
    text,
    normalize_timer_fields=False,
    normalize_rete_junk=False,
    normalize_wme_junk=False,
    normalize_symbol_junk=False,
    normalize_goal_decider_junk=False,
    normalize_matchset_junk=False,
    normalize_io_junk=False,
    normalize_slot_junk=False,
):
    out = re.sub(r"0x[0-9a-fA-F]+", "PTR", text)
    out = re.sub(r"BEGIN_[^\n]+", "BEGIN_DUMP", out)
    out = re.sub(r"END_[^\n]+", "END_DUMP", out)
    out = re.sub(
        r"^FIELD run_last_output_count=\d+$",
        "FIELD run_last_output_count={VOLATILE_RUNTIME_COUNTER}",
        out,
        flags=re.MULTILINE,
    )

    if normalize_timer_fields:
        out = re.sub(
            r"^FIELD (timers_cpu|timers_kernel|timers_phase)=\{.*\}$",
            r"FIELD \1={VOLATILE_TIMER_RUNTIME}",
            out,
            flags=re.MULTILINE,
        )
        out = re.sub(
            r"^FIELD (timers_total_cpu_time|timers_total_kernel_time|timers_input_function_cpu_time|timers_output_function_cpu_time)=\{.*\}$",
            r"FIELD \1={VOLATILE_TIMER_ACCUMULATOR}",
            out,
            flags=re.MULTILINE,
        )
        out = re.sub(
            r"^FIELD callback_timers=\{.*\}$",
            "FIELD callback_timers={VOLATILE_CALLBACK_TIMERS}",
            out,
            flags=re.MULTILINE,
        )

    if normalize_rete_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Ignore recursive render errors from inactive union branches.
            if "<render_error:Cannot access memory at address PTR>" in line:
                continue

            # Ignore RETE dummy-top internal union/layout fields that are not semantic state.
            if re.search(r"^RECURSIVE(_PTR|_CYCLE)? agent\.dummy_top_node\.first_child\.", line):
                continue

            # Ignore hash-bucket internals and hash ids (allocation/hash artifacts).
            if re.search(r"RECURSIVE .*\.hash_id=", line):
                continue
            if re.search(r"RECURSIVE .*\.next_in_hash_table", line):
                continue

            # Ignore refcounts and bucket/list linkage that can vary by allocation order.
            if re.search(r"RECURSIVE .*\.reference_count=", line):
                continue
            if re.search(r"RECURSIVE(_PTR|_CYCLE)? .*\.(next_in_bucket|prev_in_bucket|next_in_am|prev_in_am)", line):
                continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    if normalize_wme_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Keep semantic summaries/signatures and top-level WME table lines.
            if line.startswith("WME_SUMMARY ") or line.startswith("WME_SIGNATURE "):
                filtered_lines.append(line)
                continue
            if line.startswith("WME ") or line.startswith("WME_TRUNCATED"):
                filtered_lines.append(line)
                continue

            # Suppress deep recursive symbol internals from WME id/attr/value branches.
            if re.search(r"^RECURSIVE(_PTR|_CYCLE)? agent\.all_wmes_in_rete\..*\.(id|attr|value)\.", line):
                continue

            # Suppress WME branch hash/refcount internals.
            if re.search(r"^RECURSIVE agent\.all_wmes_in_rete\..*\.(reference_count|hash_id)=", line):
                continue
            if re.search(r"^RECURSIVE agent\.all_wmes_in_rete\..*\.next_in_hash_table", line):
                continue

            # Suppress WME branch list-link internals that are allocation-order noise.
            if re.search(
                r"^RECURSIVE(_PTR|_CYCLE)? agent\.all_wmes_in_rete\..*\.(rete_next|rete_prev|next|prev|next_from_wme|prev_from_wme|right_mems|tokens)\b",
                line,
            ):
                continue

            # Suppress right-memory indexing/linkage internals reached from WME roots.
            if line.startswith("RECURSIVE") and ("agent.all_wmes_in_rete" in line) and (".right_mems" in line):
                continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    if normalize_symbol_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Keep symbol semantic lines.
            if line.startswith("SYMBOL_SUMMARY ") or line.startswith("SYMBOL_SIGNATURE "):
                filtered_lines.append(line)
                continue

            # Suppress deep recursive symbol-manager internals.
            if line.startswith("RECURSIVE") and ("agent.symbolManager" in line):
                continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    if normalize_goal_decider_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Keep semantic Goal+Decider lines.
            if line.startswith("GOAL_DECIDER_SUMMARY ") or line.startswith("GOAL_DECIDER_SIGNATURE "):
                filtered_lines.append(line)
                continue

            # Suppress deep recursive noise from goal/decider branch internals.
            if line.startswith("RECURSIVE") and (
                ("agent.top_goal" in line)
                or ("agent.bottom_goal" in line)
                or ("agent.active_goal" in line)
                or ("agent.Decider" in line)
            ):
                if "agent.Decider.thisAgent." in line:
                    continue
                if "agent.Decider.last_dc" in line:
                    continue
                if re.search(r"\.(reference_count|hash_id)=", line):
                    continue
                if ".next_in_hash_table" in line:
                    continue
                if re.search(r"\.(next|prev|higher_goal|lower_goal)\b", line):
                    continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    if normalize_matchset_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Keep semantic Match-set lines.
            if line.startswith("MATCHSET_SUMMARY ") or line.startswith("MATCHSET_SIGNATURE "):
                filtered_lines.append(line)
                continue

            # Suppress deep recursive noise rooted at match-set assertion lists.
            if line.startswith("RECURSIVE") and (
                ("agent.ms_i_assertions" in line)
                or ("agent.ms_o_assertions" in line)
                or ("agent.postponed_assertions" in line)
            ):
                # The p_node/tok/w branches contain many inactive union/layout fields and
                # transient link state that are not stable semantic parity signals.
                if re.search(
                    r"^RECURSIVE(_PTR|_CYCLE)? agent\.(ms_i_assertions|ms_o_assertions|postponed_assertions)\.(p_node|tok|w)\b",
                    line,
                ):
                    continue
                if re.search(r"\.(reference_count|hash_id)=", line):
                    continue
                if ".next_in_hash_table" in line:
                    continue
                if re.search(r"\.(next|prev|next_in_level|prev_in_level|next_from_wme|prev_from_wme)\b", line):
                    continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    if normalize_io_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Keep semantic IO lines.
            if line.startswith("IO_SUMMARY ") or line.startswith("IO_SIGNATURE "):
                filtered_lines.append(line)
                continue

            # Suppress deep recursive noise rooted in IO structures.
            if line.startswith("RECURSIVE") and (
                ("agent.io_header" in line)
                or ("agent.io_header_input" in line)
                or ("agent.io_header_output" in line)
                or ("agent.io_header_link" in line)
                or ("agent.existing_output_links" in line)
                or ("agent.collected_io_wmes" in line)
            ):
                continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    if normalize_slot_junk:
        filtered_lines = []
        for line in out.splitlines():
            # Keep semantic slot ownership lines.
            if line.startswith("SLOT_SUMMARY ") or line.startswith("SLOT_SIGNATURE "):
                filtered_lines.append(line)
                continue

            # Suppress deep recursive noise rooted in slot/pref ownership lists.
            if line.startswith("RECURSIVE") and (
                ("agent.changed_slots" in line)
                or ("agent.context_slots_with_changed_accept_prefs" in line)
                or ("agent.slots_for_possible_removal" in line)
            ):
                continue

            filtered_lines.append(line)

        out = "\n".join(filtered_lines)
        if out and not out.endswith("\n"):
            out += "\n"

    return out


def write_text(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def gdb_helper_py(
    max_wmes,
    recursive_depth,
    recursive_max_nodes,
    rete_only_recursive=False,
    wme_only_recursive=False,
    symbol_only_recursive=False,
    goal_decider_only_recursive=False,
    matchset_only_recursive=False,
    io_only_recursive=False,
    slot_only_recursive=False,
):
    return f"""import gdb

RECURSIVE_DEPTH_LIMIT = {recursive_depth}
RECURSIVE_MAX_NODES = {recursive_max_nodes}
RECURSIVE_RETE_ONLY = {1 if rete_only_recursive else 0}
RECURSIVE_WME_ONLY = {1 if wme_only_recursive else 0}
RECURSIVE_SYMBOL_ONLY = {1 if symbol_only_recursive else 0}
RECURSIVE_GOAL_DECIDER_ONLY = {1 if goal_decider_only_recursive else 0}
RECURSIVE_MATCHSET_ONLY = {1 if matchset_only_recursive else 0}
RECURSIVE_IO_ONLY = {1 if io_only_recursive else 0}
RECURSIVE_SLOT_ONLY = {1 if slot_only_recursive else 0}
_recursive_nodes = 0

_SCALAR_CODES = {{
    gdb.TYPE_CODE_INT,
    gdb.TYPE_CODE_FLT,
    gdb.TYPE_CODE_BOOL,
    gdb.TYPE_CODE_CHAR,
    gdb.TYPE_CODE_ENUM,
}}

def _ptr_is_null(value):
    try:
        return int(value) == 0
    except Exception:
        return False

def _count_ms_changes(head):
    count = 0
    cur = head
    while not _ptr_is_null(cur):
        count += 1
        cur = cur['next']
    return count

def _count_alpha_mems(agent):
    total = 0
    try:
        alpha_tables = agent['alpha_hash_tables']
        for i in range(16):
            total += int(alpha_tables[i]['count'])
    except Exception:
        return -1
    return total

def _count_right_mems(agent):
    total = 0
    w = agent['all_wmes_in_rete']
    while not _ptr_is_null(w):
        rm = w['right_mems']
        while not _ptr_is_null(rm):
            total += 1
            rm = rm['next_from_wme']
        w = w['rete_next']
    return total

def _sym_name(sym):
    def _sanitize_text(s):
        if s is None:
            return "<?>"
        # Strip NUL/control chars that can break GDB Python print output.
        cleaned = []
        for ch in str(s):
            code = ord(ch)
            if code == 0:
                continue
            if 32 <= code <= 126:
                cleaned.append(ch)
            else:
                cleaned.append("?")
        return "".join(cleaned)

    try:
        if _ptr_is_null(sym):
            return "NULL"
        return _sanitize_text(_render_value(sym))
    except Exception:
        return "<?>"

def _collect_wme_signature(agent, limit=64):
    sigs = []
    w = agent['all_wmes_in_rete']
    while (not _ptr_is_null(w)) and (len(sigs) < limit):
        try:
            timetag = _safe_int(w['timetag'])
            acc = 1 if _safe_int(w['acceptable']) else 0
            has_out = 0 if _ptr_is_null(w['output_link']) else 1
            id_name = _sym_name(w['id'])
            attr_name = _sym_name(w['attr'])
            value_name = _sym_name(w['value'])
            sigs.append(f"{{timetag}}:{{id_name}}:^{{attr_name}}:{{value_name}}:a{{acc}}:o{{has_out}}")
        except Exception:
            sigs.append("<wme_error>")
        w = w['rete_next']
    return ",".join(sigs)

def _count_output_link_wmes(agent):
    count = 0
    w = agent['all_wmes_in_rete']
    while not _ptr_is_null(w):
        if not _ptr_is_null(w['output_link']):
            count += 1
        w = w['rete_next']
    return count

def _count_acceptable_wmes(agent):
    count = 0
    w = agent['all_wmes_in_rete']
    while not _ptr_is_null(w):
        if _safe_int(w['acceptable']):
            count += 1
        w = w['rete_next']
    return count

def _collect_unique_wme_symbols(agent):
    symbols = []
    seen = set()
    w = agent['all_wmes_in_rete']
    while not _ptr_is_null(w):
        for sym in (w['id'], w['attr'], w['value']):
            ptr = _safe_int(sym)
            if (ptr is None) or (ptr == 0):
                continue
            if ptr in seen:
                continue
            seen.add(ptr)
            symbols.append(sym)
        w = w['rete_next']
    return symbols

def _symbol_type(sym):
    try:
        return _safe_int(sym['symbol_type'])
    except Exception:
        return -1

def _is_goal_identifier(sym):
    try:
        if _symbol_type(sym) != 2:
            return False
        return bool(_safe_int(sym['id']['isa_goal']))
    except Exception:
        return False

def _identifier_name(sym):
    try:
        raw_letter = _safe_int(sym['id']['name_letter'])
        if (raw_letter is None) or not (65 <= raw_letter <= 90):
            letter = "?"
        else:
            letter = chr(raw_letter)
        number = _safe_int(sym['id']['name_number'])
        return f"{{letter}}{{number}}"
    except Exception:
        return "?"

def _symbol_label(sym):
    stype = _symbol_type(sym)
    if stype == 2:
        return "IDG" if _is_goal_identifier(sym) else "IDN"
    if stype == 3:
        return "STR"
    if stype == 4:
        return "INT"
    if stype == 5:
        return "FLOAT"
    return "OTHER"

def _collect_symbol_signature(agent, limit=128):
    symbols = _collect_unique_wme_symbols(agent)
    try:
        symbols.sort(key=lambda s: (_symbol_type(s), _symbol_label(s), _safe_int(s) or 0))
    except Exception:
        pass

    sigs = []
    for sym in symbols:
        if len(sigs) >= limit:
            break
        stype = _symbol_type(sym)
        label = _symbol_label(sym)
        sigs.append(f"{{stype}}:{{label}}")
    return ",".join(sigs)

def _count_symbol_types_in_wmes(agent):
    counts = {{
        "id": 0,
        "id_goal": 0,
        "id_non_goal": 0,
        "str": 0,
        "int": 0,
        "float": 0,
        "other": 0,
    }}
    for sym in _collect_unique_wme_symbols(agent):
        stype = _symbol_type(sym)
        if stype == 2:
            counts["id"] += 1
            if _is_goal_identifier(sym):
                counts["id_goal"] += 1
            else:
                counts["id_non_goal"] += 1
        elif stype == 3:
            counts["str"] += 1
        elif stype == 4:
            counts["int"] += 1
        elif stype == 5:
            counts["float"] += 1
        else:
            counts["other"] += 1
    return counts

def _collect_goals(top_goal, limit=64):
    goals = []
    cur = top_goal
    while (not _ptr_is_null(cur)) and (len(goals) < limit):
        goals.append(cur)
        cur = cur['id']['lower_goal']
    return goals

def _goal_name(goal):
    try:
        letter = _safe_int(goal['id']['name_letter'])
        number = _safe_int(goal['id']['name_number'])
        if (letter is None) or (number is None):
            return "?"
        if 65 <= letter <= 90:
            return f"{{chr(letter)}}{{number}}"
    except Exception:
        pass
    return "?"

def _collect_goal_signature(agent, limit=64):
    sigs = []
    goals = _collect_goals(agent['top_goal'], limit=limit)
    for goal in goals:
        try:
            name = _goal_name(goal)
            level = _safe_int(goal['id']['level'])
            is_impasse = 1 if _safe_int(goal['id']['isa_impasse']) else 0
            has_op_slot = 0 if _ptr_is_null(goal['id']['operator_slot']) else 1
            sigs.append(f"{{name}}:L{{level}}:I{{is_impasse}}:O{{has_op_slot}}")
        except Exception:
            sigs.append("<goal_error>")
    return ",".join(sigs)

def _count_impasse_goals(agent):
    count = 0
    for goal in _collect_goals(agent['top_goal']):
        if _safe_int(goal['id']['isa_impasse']):
            count += 1
    return count

def _collect_ms_node_ids(head, limit=64):
    sigs = []
    cur = head
    while (not _ptr_is_null(cur)) and (len(sigs) < limit):
        node_id = -1
        level = -1
        timetag = -1

        try:
            p_node = cur['p_node']
            if not _ptr_is_null(p_node):
                node_id = _safe_int(p_node['node_id'])
        except Exception:
            pass

        try:
            level = _safe_int(cur['level'])
        except Exception:
            pass

        try:
            w = cur['w']
            if not _ptr_is_null(w):
                timetag = _safe_int(w['timetag'])
        except Exception:
            pass

        sigs.append(f"{{node_id}}:{{level}}:{{timetag}}")
        cur = cur['next']

    return ",".join(sigs)

def _count_output_links(head):
    count = 0
    cur = head
    while not _ptr_is_null(cur):
        count += 1
        cur = cur['next']
    return count

def _count_io_wmes(head):
    count = 0
    cur = head
    while not _ptr_is_null(cur):
        count += 1
        cur = cur['next']
    return count

def _collect_output_link_signature(head, limit=64):
    sigs = []
    cur = head
    while (not _ptr_is_null(cur)) and (len(sigs) < limit):
        timetag = -1
        try:
            link_wme = cur['link_wme']
            if not _ptr_is_null(link_wme):
                timetag = _safe_int(link_wme['timetag'])
        except Exception:
            pass
        sigs.append(f"t{{timetag}}")
        cur = cur['next']
    return ",".join(sigs)

def _count_dl_list(head):
    count = 0
    cur = head
    while not _ptr_is_null(cur):
        count += 1
        cur = cur['next']
    return count

def _count_cons_list(head):
    count = 0
    cur = head
    while not _ptr_is_null(cur):
        count += 1
        cur = cur['rest']
    return count

def _collect_dl_slot_signature(head, limit=64):
    sigs = []
    cur = head
    while (not _ptr_is_null(cur)) and (len(sigs) < limit):
        label = "?"
        try:
            slot_ptr = cur['item']
            if not _ptr_is_null(slot_ptr):
                slot_val = slot_ptr.dereference()
                label = f"{{_sym_name(slot_val['id'])}}:^{{_sym_name(slot_val['attr'])}}"
        except Exception:
            pass
        sigs.append(label)
        cur = cur['next']
    return ",".join(sigs)

def _collect_cons_slot_signature(head, limit=64):
    sigs = []
    cur = head
    while (not _ptr_is_null(cur)) and (len(sigs) < limit):
        label = "?"
        try:
            slot_ptr = cur['first']
            if not _ptr_is_null(slot_ptr):
                slot_val = slot_ptr.dereference()
                label = f"{{_sym_name(slot_val['id'])}}:^{{_sym_name(slot_val['attr'])}}"
        except Exception:
            pass
        sigs.append(label)
        cur = cur['rest']
    return ",".join(sigs)

def _safe_int(value):
    try:
        return int(value)
    except Exception:
        return None

def _render_value(value):
    try:
        return f"{{value}}"
    except Exception as e:
        return f"<render_error:{{e}}>"

def _dump_recursive_value(path, value, depth, visited):
    global _recursive_nodes

    if _recursive_nodes >= RECURSIVE_MAX_NODES:
        print(f"RECURSIVE_TRUNCATED path={{path}} reason=max_nodes")
        return

    try:
        t = value.type.strip_typedefs()
        code = t.code
    except Exception as e:
        print(f"RECURSIVE_ERROR path={{path}} err={{e}}")
        return

    if code in _SCALAR_CODES:
        print(f"RECURSIVE {{path}}={{_render_value(value)}}")
        return

    if code == gdb.TYPE_CODE_ARRAY:
        try:
            n = int(t.sizeof / t.target().sizeof)
        except Exception:
            n = 0
        max_items = min(n, 8)
        print(f"RECURSIVE_ARRAY {{path}} size={{n}} showing={{max_items}}")
        for i in range(max_items):
            try:
                _dump_recursive_value(f"{{path}}[{{i}}]", value[i], depth, visited)
            except Exception as e:
                print(f"RECURSIVE_ERROR path={{path}}[{{i}}] err={{e}}")
        if n > max_items:
            print(f"RECURSIVE_ARRAY_TRUNCATED {{path}} remaining={{n-max_items}}")
        return

    if code == gdb.TYPE_CODE_PTR:
        addr = _safe_int(value)
        if (addr is None) or (addr == 0):
            print(f"RECURSIVE_PTR {{path}}=NULL")
            return

        target = t.target().strip_typedefs()
        target_code = target.code
        print(f"RECURSIVE_PTR {{path}}={{_render_value(value)}} target={{target}}")

        if depth >= RECURSIVE_DEPTH_LIMIT:
            return

        if target_code not in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
            return

        visit_key = (addr, str(target))
        if visit_key in visited:
            print(f"RECURSIVE_CYCLE {{path}} target={{target}}")
            return

        visited.add(visit_key)
        _recursive_nodes += 1

        try:
            pointee = value.dereference()
        except Exception as e:
            print(f"RECURSIVE_ERROR path={{path}} err={{e}}")
            return

        _dump_recursive_struct(path, pointee, depth + 1, visited)
        return

    if code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        _dump_recursive_struct(path, value, depth, visited)
        return

    print(f"RECURSIVE {{path}}={{_render_value(value)}}")

def _dump_recursive_struct(path, struct_value, depth, visited):
    if depth > RECURSIVE_DEPTH_LIMIT:
        return

    try:
        t = struct_value.type.strip_typedefs()
        fields = t.fields()
    except Exception as e:
        print(f"RECURSIVE_ERROR path={{path}} err={{e}}")
        return

    for field in fields:
        name = field.name
        if not name:
            continue
        child_path = f"{{path}}.{{name}}"
        try:
            child = struct_value[name]
        except Exception as e:
            print(f"RECURSIVE_ERROR path={{child_path}} err={{e}}")
            continue

        _dump_recursive_value(child_path, child, depth, visited)

def _dump_recursive_agent(agent):
    global _recursive_nodes
    _recursive_nodes = 0
    visited = set()
    _dump_recursive_struct("agent", agent, 0, visited)
    print(f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}}")

def dump_agent(var_name, tag):
    agent_ptr = _resolve_agent_ptr(var_name)
    agent = agent_ptr.dereference()

    print(f"BEGIN_{{tag}}")

    t = agent.type.strip_typedefs()
    for field in t.fields():
        name = field.name
        if not name:
            continue
        try:
            print(f"FIELD {{name}}={{agent[name]}}")
        except Exception as e:
            print(f"FIELD {{name}}=<error:{{e}}>")

    try:
        w = agent['all_wmes_in_rete']
        i = 0
        while (not _ptr_is_null(w)) and i < {max_wmes}:
            i += 1
            try:
                print(
                    f"WME {{i}} id={{w['id']}} attr={{w['attr']}} value={{w['value']}} "
                    f"timetag={{w['timetag']}} acceptable={{w['acceptable']}} "
                    f"pref={{w['preference']}} output_link={{w['output_link']}}"
                )
            except Exception as e:
                print(f"WME {{i}} <error:{{e}}>")
            w = w['rete_next']
        if not _ptr_is_null(w):
            print(f"WME_TRUNCATED remaining_after={max_wmes}")
    except Exception as e:
        print(f"WME_DUMP_ERROR {{e}}")

    try:
        ms_i = _count_ms_changes(agent['ms_i_assertions'])
        ms_o = _count_ms_changes(agent['ms_o_assertions'])
        ms_p = _count_ms_changes(agent['postponed_assertions'])
        print(f"MS_COUNTS ms_i={{ms_i}} ms_o={{ms_o}} ms_postponed={{ms_p}}")
    except Exception as e:
        print(f"MS_COUNTS_ERROR {{e}}")

    try:
        ms_i = _count_ms_changes(agent['ms_i_assertions'])
        ms_o = _count_ms_changes(agent['ms_o_assertions'])
        ms_p = _count_ms_changes(agent['postponed_assertions'])
        print(f"MATCHSET_SUMMARY ms_i={{ms_i}} ms_o={{ms_o}} ms_postponed={{ms_p}} total={{ms_i + ms_o + ms_p}}")
        print(
            f"MATCHSET_SIGNATURE i={{_collect_ms_node_ids(agent['ms_i_assertions'])}} "
            f"o={{_collect_ms_node_ids(agent['ms_o_assertions'])}} "
            f"p={{_collect_ms_node_ids(agent['postponed_assertions'])}}"
        )
    except Exception as e:
        print(f"MATCHSET_SUMMARY_ERROR {{e}}")

    try:
        alpha_mems = _count_alpha_mems(agent)
        right_mems = _count_right_mems(agent)
        num_wmes_in_rete = _safe_int(agent['num_wmes_in_rete'])
        alpha_mem_id_counter = _safe_int(agent['alpha_mem_id_counter'])
        beta_node_id_counter = _safe_int(agent['beta_node_id_counter'])
        print(
            f"RETE_SUMMARY alpha_mems={{alpha_mems}} right_mems={{right_mems}} "
            f"num_wmes_in_rete={{num_wmes_in_rete}} alpha_mem_id_counter={{alpha_mem_id_counter}} "
            f"beta_node_id_counter={{beta_node_id_counter}}"
        )
        print(
            f"RETE_MS_NODE_IDS i={{_collect_ms_node_ids(agent['ms_i_assertions'])}} "
            f"o={{_collect_ms_node_ids(agent['ms_o_assertions'])}} "
            f"p={{_collect_ms_node_ids(agent['postponed_assertions'])}}"
        )
    except Exception as e:
        print(f"RETE_SUMMARY_ERROR {{e}}")

    try:
        num_wmes_in_rete = _safe_int(agent['num_wmes_in_rete'])
        acceptable_wmes = _count_acceptable_wmes(agent)
        output_link_wmes = _count_output_link_wmes(agent)
        print(
            f"WME_SUMMARY num_wmes_in_rete={{num_wmes_in_rete}} "
            f"acceptable_wmes={{acceptable_wmes}} output_link_wmes={{output_link_wmes}}"
        )
        print(f"WME_SIGNATURE {{_collect_wme_signature(agent)}}")
    except Exception as e:
        print(f"WME_SUMMARY_ERROR {{e}}")

    try:
        symbol_counts = _count_symbol_types_in_wmes(agent)
        symbol_total = symbol_counts["id"] + symbol_counts["str"] + symbol_counts["int"] + symbol_counts["float"] + symbol_counts["other"]
        print(
            f"SYMBOL_SUMMARY total={{symbol_total}} id={{symbol_counts['id']}} "
            f"id_goal={{symbol_counts['id_goal']}} id_non_goal={{symbol_counts['id_non_goal']}} "
            f"str={{symbol_counts['str']}} "
            f"int={{symbol_counts['int']}} float={{symbol_counts['float']}} other={{symbol_counts['other']}} "
            f"wme_unique_symbols={{symbol_total}}"
        )
        print(f"SYMBOL_SIGNATURE {{_collect_symbol_signature(agent)}}")
    except Exception as e:
        print(f"SYMBOL_SUMMARY_ERROR {{e}}")

    try:
        goals = _collect_goals(agent['top_goal'])
        goal_count = len(goals)
        impasse_goals = _count_impasse_goals(agent)
        active_goal_name = _goal_name(agent['active_goal']) if not _ptr_is_null(agent['active_goal']) else "NULL"
        top_goal_name = _goal_name(agent['top_goal']) if not _ptr_is_null(agent['top_goal']) else "NULL"
        bottom_goal_name = _goal_name(agent['bottom_goal']) if not _ptr_is_null(agent['bottom_goal']) else "NULL"
        print(
            f"GOAL_DECIDER_SUMMARY goal_count={{goal_count}} impasse_goals={{impasse_goals}} "
            f"top={{top_goal_name}} bottom={{bottom_goal_name}} active={{active_goal_name}} "
            f"goal_chain_depth={{goal_count}}"
        )
        print(f"GOAL_DECIDER_SIGNATURE {{_collect_goal_signature(agent)}}")
    except Exception as e:
        print(f"GOAL_DECIDER_SUMMARY_ERROR {{e}}")

    try:
        io_header = _sym_name(agent['io_header']) if not _ptr_is_null(agent['io_header']) else "NULL"
        io_header_input = _sym_name(agent['io_header_input']) if not _ptr_is_null(agent['io_header_input']) else "NULL"
        io_header_output = _sym_name(agent['io_header_output']) if not _ptr_is_null(agent['io_header_output']) else "NULL"
        has_io_header_link = 0 if _ptr_is_null(agent['io_header_link']) else 1
        output_links = _count_output_links(agent['existing_output_links'])
        output_link_changed = 1 if _safe_int(agent['output_link_changed']) else 0
        print(
            f"IO_SUMMARY io_header={{io_header}} io_input={{io_header_input}} io_output={{io_header_output}} "
            f"has_io_header_link={{has_io_header_link}} output_links={{output_links}} "
            f"output_link_changed={{output_link_changed}}"
        )
        print(f"IO_SIGNATURE ol={{_collect_output_link_signature(agent['existing_output_links'])}}")
    except Exception as e:
        print(f"IO_SUMMARY_ERROR {{e}}")

    try:
        changed_slots = _count_dl_list(agent['changed_slots'])
        context_changed_slots = _count_dl_list(agent['context_slots_with_changed_accept_prefs'])
        possible_removal_slots = _count_cons_list(agent['slots_for_possible_removal'])
        print(
            f"SLOT_SUMMARY changed_slots={{changed_slots}} "
            f"context_accept_changed={{context_changed_slots}} "
            f"possible_removal={{possible_removal_slots}}"
        )
        print(
            f"SLOT_SIGNATURE changed={{_collect_dl_slot_signature(agent['changed_slots'])}} "
            f"context={{_collect_dl_slot_signature(agent['context_slots_with_changed_accept_prefs'])}} "
            f"remove={{_collect_cons_slot_signature(agent['slots_for_possible_removal'])}}"
        )
    except Exception as e:
        print(f"SLOT_SUMMARY_ERROR {{e}}")

    if RECURSIVE_DEPTH_LIMIT > 0:
        try:
            if RECURSIVE_RETE_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.all_wmes_in_rete", agent['all_wmes_in_rete'], 0, visited)
                _dump_recursive_value("agent.dummy_top_node", agent['dummy_top_node'].address, 0, visited)
                _dump_recursive_value("agent.ms_i_assertions", agent['ms_i_assertions'], 0, visited)
                _dump_recursive_value("agent.ms_o_assertions", agent['ms_o_assertions'], 0, visited)
                _dump_recursive_value("agent.postponed_assertions", agent['postponed_assertions'], 0, visited)
                _dump_recursive_value("agent.alpha_hash_tables", agent['alpha_hash_tables'].address, 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} rete_only=1"
                )
            elif RECURSIVE_WME_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.all_wmes_in_rete", agent['all_wmes_in_rete'], 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} wme_only=1"
                )
            elif RECURSIVE_SYMBOL_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.symbolManager", agent['symbolManager'], 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} symbol_only=1"
                )
            elif RECURSIVE_GOAL_DECIDER_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.top_goal", agent['top_goal'], 0, visited)
                _dump_recursive_value("agent.bottom_goal", agent['bottom_goal'], 0, visited)
                _dump_recursive_value("agent.active_goal", agent['active_goal'], 0, visited)
                _dump_recursive_value("agent.Decider", agent['Decider'], 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} goal_decider_only=1"
                )
            elif RECURSIVE_MATCHSET_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.ms_i_assertions", agent['ms_i_assertions'], 0, visited)
                _dump_recursive_value("agent.ms_o_assertions", agent['ms_o_assertions'], 0, visited)
                _dump_recursive_value("agent.postponed_assertions", agent['postponed_assertions'], 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} matchset_only=1"
                )
            elif RECURSIVE_IO_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.io_header", agent['io_header'], 0, visited)
                _dump_recursive_value("agent.io_header_input", agent['io_header_input'], 0, visited)
                _dump_recursive_value("agent.io_header_output", agent['io_header_output'], 0, visited)
                _dump_recursive_value("agent.io_header_link", agent['io_header_link'], 0, visited)
                _dump_recursive_value("agent.existing_output_links", agent['existing_output_links'], 0, visited)
                _dump_recursive_value("agent.collected_io_wmes", agent['collected_io_wmes'], 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} io_only=1"
                )
            elif RECURSIVE_SLOT_ONLY:
                _recursive_nodes = 0
                visited = set()
                _dump_recursive_value("agent.changed_slots", agent['changed_slots'], 0, visited)
                _dump_recursive_value("agent.context_slots_with_changed_accept_prefs", agent['context_slots_with_changed_accept_prefs'], 0, visited)
                _dump_recursive_value("agent.slots_for_possible_removal", agent['slots_for_possible_removal'], 0, visited)
                print(
                    f"RECURSIVE_SUMMARY nodes={{_recursive_nodes}} "
                    f"max={{RECURSIVE_MAX_NODES}} depth={{RECURSIVE_DEPTH_LIMIT}} slot_only=1"
                )
            else:
                _dump_recursive_agent(agent)
        except Exception as e:
            print(f"RECURSIVE_DUMP_ERROR {{e}}")

    print(f"END_{{tag}}")

def _resolve_agent_ptr(var_name):
    value = gdb.parse_and_eval(var_name)

    try:
        value_type = value.type.strip_typedefs()
        if value_type.code == gdb.TYPE_CODE_PTR:
            target_name = str(value_type.target().strip_typedefs())
            if target_name in ("agent", "agent_struct"):
                return value
    except Exception:
        pass

    for type_name in ("agent", "agent_struct"):
        try:
            return value.cast(gdb.lookup_type(type_name).pointer())
        except Exception:
            pass

    raise gdb.GdbError(f"Unable to cast {{var_name}} to agent pointer")

def maybe_dump_and_quit(var_name, tag, target_cycle=None, target_phase=None):
    try:
        agent_ptr = _resolve_agent_ptr(var_name)
        agent = agent_ptr.dereference()
    except Exception:
        return

    try:
        current_cycle = int(agent['d_cycle_count'])
        current_phase = int(agent['current_phase'])
    except Exception:
        return

    if (target_cycle is not None) and (current_cycle != int(target_cycle)):
        return

    if (target_phase is not None) and (current_phase != int(target_phase)):
        return

    dump_agent(var_name, tag)
    gdb.execute("quit")
"""


def vanilla_gdb(category, test_name, cycle, helper_path, phase_enum, run_debug_mode=False):
    debug_arg = "-r " if run_debug_mode else ""
    if phase_enum:
        return f"""set pagination off
set confirm off
set breakpoint pending off
set debuginfod enabled off
set print pretty off
set print elements 0
set print repeats 0
set args {debug_arg}-l -c \"{category}\" -t \"{test_name}\"
start
sharedlibrary libSoar
source {helper_path}
break do_one_top_level_phase(agent_struct*)
commands
    silent
    set $ag = $rdi
    python maybe_dump_and_quit("$ag", "VANILLA_DC{cycle}", {cycle}, {phase_enum})
    continue
end
continue
"""

    return f"""set pagination off
set confirm off
set breakpoint pending off
set debuginfod enabled off
set print pretty off
set print elements 0
set print repeats 0
set args {debug_arg}-l -c \"{category}\" -t \"{test_name}\"
start
sharedlibrary libSoar
source {helper_path}
break do_one_top_level_phase(agent_struct*)
commands
        silent
        set $ag = $rdi
        python maybe_dump_and_quit("$ag", "VANILLA_DC{cycle}", {cycle}, None)
        continue
end
continue
"""


def snapshot_gdb(
    category,
    snapshot_test_name,
    cycle,
    snapshot_step_start,
    snapshot_step_end,
    helper_path,
    phase_enum,
    snapshot_capture_point,
    run_debug_mode=False,
    phase_name=None,
):
    debug_arg = "-r " if run_debug_mode else ""
    if snapshot_capture_point == "post-restore":
        phase_env = ""
        if phase_enum:
            phase_stop_value = PHASE_STOP_ENV_MAP.get(phase_name, "apply")
            phase_env = f"set env SOAR_SNAPSHOT_STOP_PHASE {phase_stop_value}\n"
        return f"""set pagination off
set confirm off
set breakpoint pending off
set debuginfod enabled off
set print pretty off
set print elements 0
set print repeats 0
set env SOAR_SNAPSHOT_STEP_START {snapshot_step_start}
set env SOAR_SNAPSHOT_STEP_END {snapshot_step_end}
{phase_env}set args {debug_arg}--snapshot-every-step -l -c \"{category}\" -t \"{snapshot_test_name}\"
start
sharedlibrary libSoar
source {helper_path}
break maybe_validate_serializer_post_load_signature
commands
    silent
    set $ag = $rdi
    python maybe_dump_and_quit("$ag", "SNAPSHOT_LOAD_DC{cycle}", {cycle}, None)
    continue
end
continue
"""

    if phase_enum:
        phase_stop_value = PHASE_STOP_ENV_MAP.get(phase_name, "apply")
        return f"""set pagination off
set confirm off
set breakpoint pending off
set debuginfod enabled off
set print pretty off
set print elements 0
set print repeats 0
set env SOAR_SNAPSHOT_STEP_START {snapshot_step_start}
set env SOAR_SNAPSHOT_STEP_END {snapshot_step_end}
set env SOAR_SNAPSHOT_STOP_PHASE {phase_stop_value}
set args {debug_arg}--snapshot-every-step -l -c \"{category}\" -t \"{snapshot_test_name}\"
start
sharedlibrary libSoar
source {helper_path}
set $restored_once = 0
break restore_agent_state_message
commands
    silent
    set $restored_once = 1
    continue
end
break do_one_top_level_phase(agent_struct*)
commands
    silent
    if ($restored_once == 1)
        set $ag = $rdi
        python maybe_dump_and_quit("$ag", "SNAPSHOT_LOAD_DC{cycle}", {cycle}, {phase_enum})
    end
    continue
end
continue
"""
    else:
        capture_condition = f"($restored_once == 1) && ($ag->d_cycle_count == {cycle})"

    return f"""set pagination off
set confirm off
set breakpoint pending off
set debuginfod enabled off
set print pretty off
set print elements 0
set print repeats 0
set env SOAR_SNAPSHOT_STEP_START {snapshot_step_start}
set env SOAR_SNAPSHOT_STEP_END {snapshot_step_end}
set args {debug_arg}--snapshot-every-step -l -c \"{category}\" -t \"{snapshot_test_name}\"
start
sharedlibrary libSoar
source {helper_path}
set $restored_once = 0
break restore_agent_state_message
commands
  silent
  set $restored_once = 1
  continue
end
break do_one_top_level_phase(agent_struct*)
commands
  silent
    if ($restored_once == 1)
        set $ag = $rdi
        python maybe_dump_and_quit("$ag", "SNAPSHOT_LOAD_DC{cycle}", {cycle}, None)
    end
    continue
end
continue
"""


def main():
    parser = argparse.ArgumentParser(
        description="Compare vanilla decision-cycle state against snapshot-reloaded state."
    )
    parser.add_argument("--category", default="FunctionalTests", help="Unit test category")
    parser.add_argument("--test", required=True, help="Vanilla test name, e.g. testWaterJug")
    parser.add_argument(
        "--snapshot-test",
        default=None,
        help="Snapshot test name. Default: [snapshot] <test>",
    )
    parser.add_argument(
        "--decision-cycle",
        type=int,
        required=True,
        help="Decision cycle to compare (1-based)",
    )
    parser.add_argument(
        "--phase",
        choices=["input", "propose", "decide", "apply", "output"],
        default=None,
        help="Optional target phase. When set, both captures require this phase.",
    )
    parser.add_argument(
        "--output-dir",
        default="/tmp/soar_debug/kernel_state_compare",
        help="Directory for generated dumps and reports",
    )
    parser.add_argument(
        "--unit-tests-bin",
        default="./build/UnitTests/UnitTests",
        help="Path to UnitTests binary",
    )
    parser.add_argument(
        "--run-debug-mode",
        action="store_true",
        help="Pass -r to UnitTests to avoid strict unit-test settings/timeouts under gdb",
    )
    parser.add_argument(
        "--snapshot-capture-point",
        choices=["phase", "post-restore"],
        default="phase",
        help=(
            "Where to capture snapshot-side state: at matched decision-cycle/phase "
            "after restore (phase), or immediately after restore rebuild completes (post-restore)."
        ),
    )
    parser.add_argument(
        "--gdb-timeout",
        type=int,
        default=45,
        help="Timeout in seconds for each gdb capture",
    )
    parser.add_argument(
        "--snapshot-step-start",
        type=int,
        default=None,
        help=(
            "Start step for snapshot/reload loop in snapshot mode. "
            "Default: --decision-cycle (one-off snapshot at target cycle)."
        ),
    )
    parser.add_argument(
        "--snapshot-step-end",
        type=int,
        default=None,
        help=(
            "End step for snapshot/reload loop in snapshot mode. "
            "Default: --decision-cycle (one-off snapshot at target cycle)."
        ),
    )
    parser.add_argument(
        "--max-wmes",
        type=int,
        default=500,
        help="Max WMEs to dump from all_wmes_in_rete",
    )
    parser.add_argument(
        "--recursive-depth",
        type=int,
        default=0,
        help="Recursive object dump depth. 0 disables recursive sift.",
    )
    parser.add_argument(
        "--recursive-max-nodes",
        type=int,
        default=2000,
        help="Maximum recursive pointer nodes to visit when recursive dump is enabled.",
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
        help=(
            "When --recursive-depth > 0, recurse only from WME root "
            "(all_wmes_in_rete)."
        ),
    )
    parser.add_argument(
        "--symbol-only-recursive",
        action="store_true",
        help=(
            "When --recursive-depth > 0, recurse only from symbol manager root "
            "(agent.symbolManager)."
        ),
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
    parser.add_argument(
        "--repo-root",
        default=None,
        help="Optional repository root path override",
    )

    args = parser.parse_args()

    if args.decision_cycle < 1:
        print("--decision-cycle must be >= 1", file=sys.stderr)
        return 2

    snapshot_step_start = (
        args.snapshot_step_start
        if args.snapshot_step_start is not None
        else args.decision_cycle
    )
    snapshot_step_end = (
        args.snapshot_step_end
        if args.snapshot_step_end is not None
        else args.decision_cycle
    )
    if snapshot_step_start < 1:
        print("--snapshot-step-start must be >= 1", file=sys.stderr)
        return 2
    if snapshot_step_end < snapshot_step_start:
        print("--snapshot-step-end must be >= --snapshot-step-start", file=sys.stderr)
        return 2

    if args.repo_root:
        repo_root = Path(args.repo_root).resolve()
    else:
        repo_root = Path(__file__).resolve().parents[2]
    out_dir = Path(args.output_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    gdb_bin = shutil.which("gdb")
    if not gdb_bin:
        print("gdb not found on PATH", file=sys.stderr)
        return 2

    snapshot_test = args.snapshot_test or f"[snapshot] {args.test}"
    phase_enum = PHASE_ENUM_MAP.get(args.phase) if args.phase else None

    helper_py = out_dir / "gdb_dump_agent_fields.py"
    vanilla_gdb_file = out_dir / "capture_vanilla.gdb"
    snapshot_gdb_file = out_dir / "capture_snapshot.gdb"

    vanilla_log = out_dir / "gdb_vanilla.log"
    snapshot_log = out_dir / "gdb_snapshot.log"

    raw_vanilla = out_dir / f"raw_vanilla_dc{args.decision_cycle}.txt"
    raw_snapshot = out_dir / f"raw_snapshot_dc{args.decision_cycle}.txt"
    norm_vanilla = out_dir / f"norm_vanilla_dc{args.decision_cycle}.txt"
    norm_snapshot = out_dir / f"norm_snapshot_dc{args.decision_cycle}.txt"
    diff_report = out_dir / f"diff_report_dc{args.decision_cycle}.txt"

    if args.recursive_depth < 0:
        print("--recursive-depth must be >= 0", file=sys.stderr)
        return 2
    if args.recursive_max_nodes < 1:
        print("--recursive-max-nodes must be >= 1", file=sys.stderr)
        return 2

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
            args.decision_cycle,
            helper_py,
            phase_enum,
            run_debug_mode=args.run_debug_mode,
        ),
    )
    write_text(
        snapshot_gdb_file,
        snapshot_gdb(
            args.category,
            snapshot_test,
            args.decision_cycle,
            snapshot_step_start,
            snapshot_step_end,
            helper_py,
            phase_enum,
            args.snapshot_capture_point,
            run_debug_mode=args.run_debug_mode,
            phase_name=args.phase,
        ),
    )

    env = os.environ.copy()
    env.setdefault(
        "LD_LIBRARY_PATH",
        f"{repo_root / 'build' / 'Core'}:{repo_root / 'build'}",
    )

    rc_vanilla, err_vanilla = run_cmd(
        [gdb_bin, "-q", "--batch", "-x", str(vanilla_gdb_file), str(args.unit_tests_bin)],
        cwd=repo_root,
        timeout_sec=args.gdb_timeout,
        out_path=vanilla_log,
        env=env,
    )
    if err_vanilla:
        print(f"vanilla capture failed: {err_vanilla}", file=sys.stderr)
        return 1
    if rc_vanilla != 0:
        print(f"vanilla capture exited with code {rc_vanilla}", file=sys.stderr)

    rc_snapshot, err_snapshot = run_cmd(
        [gdb_bin, "-q", "--batch", "-x", str(snapshot_gdb_file), str(args.unit_tests_bin)],
        cwd=repo_root,
        timeout_sec=args.gdb_timeout,
        out_path=snapshot_log,
        env=env,
    )
    if err_snapshot:
        print(f"snapshot capture failed: {err_snapshot}", file=sys.stderr)
        return 1
    if rc_snapshot != 0:
        print(f"snapshot capture exited with code {rc_snapshot}", file=sys.stderr)

    vanilla_text = vanilla_log.read_text(encoding="utf-8", errors="replace")
    snapshot_text = snapshot_log.read_text(encoding="utf-8", errors="replace")

    begin_v = f"BEGIN_VANILLA_DC{args.decision_cycle}"
    end_v = f"END_VANILLA_DC{args.decision_cycle}"
    begin_s = f"BEGIN_SNAPSHOT_LOAD_DC{args.decision_cycle}"
    end_s = f"END_SNAPSHOT_LOAD_DC{args.decision_cycle}"

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
    write_text(diff_report, "".join(diff_lines))

    if not raw_v.strip():
        print(f"warning: no vanilla dump block found in {vanilla_log}")
    if not raw_s.strip():
        print(f"warning: no snapshot dump block found in {snapshot_log}")

    print("Generated files:")
    print(raw_vanilla)
    print(raw_snapshot)
    print(norm_vanilla)
    print(norm_snapshot)
    print(diff_report)

    return 0


if __name__ == "__main__":
    sys.exit(main())
