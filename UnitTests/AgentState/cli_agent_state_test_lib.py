#!/usr/bin/env python3

import os
import pty
import re
import select
import subprocess
import tempfile
import time
from pathlib import Path


PROMPT = "soar %"
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = Path(os.environ.get("SOAR_SOURCE_DIR", SCRIPT_DIR.parent.parent)).resolve()
AGENT_DIR = REPO_ROOT / "UnitTests" / "SoarTestAgents" / "agent_state_restore"


def normalize(text):
    return ANSI_RE.sub("", text).replace("\r", "")


def command_result(output):
    lines = normalize(output).splitlines()
    if lines:
        lines = lines[1:]
    while lines and lines[-1].strip() == PROMPT:
        lines.pop()
    return re.sub(r"\s+", " ", "\n".join(lines)).strip()


def find_cli_binary():
    candidates = []

    env_cli = os.environ.get("SOAR_CLI_BIN")
    if env_cli:
        candidates.append(Path(env_cli))

    build_dir = os.environ.get("SOAR_BUILD_DIR")
    if build_dir:
        build_root = Path(build_dir)
        candidates.extend([
            build_root / "SoarCLI" / "soar",
            build_root / "soar",
            build_root / "Debug" / "soar",
            build_root / "Release" / "soar",
        ])

    candidates.extend([
        REPO_ROOT / "out" / "soar",
        REPO_ROOT / "build" / "SoarCLI" / "soar",
        REPO_ROOT / "build" / "soar",
    ])

    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate.resolve()

    raise FileNotFoundError("Unable to locate the Soar CLI binary. Set SOAR_CLI_BIN or build the CLI.")


def expect_contains(output, expected_text, description):
    if expected_text not in output:
        raise AssertionError("%s\n%s" % (description, output))


def expect_not_contains(output, unexpected_text, description):
    if unexpected_text in output:
        raise AssertionError("%s\n%s" % (description, output))


def expect_firing_count(output, production_name, expected_count):
    pattern = r"(^|\n)\s*%d:\s+%s(?:\n|$)" % (expected_count, re.escape(production_name))
    if not re.search(pattern, output):
        raise AssertionError(
            "Expected firing count %d for %r, got:\n%s" % (expected_count, production_name, output)
        )


def expect_supported_preference(output, identifier, attribute, support_marker):
    header_pattern = r"Preferences for\s+%s\s+\^%s:" % (re.escape(identifier), re.escape(attribute))
    if not re.search(header_pattern, output):
        raise AssertionError(
            "Expected preferences header for %s ^%s, got:\n%s" % (identifier, attribute, output)
        )

    normalized_output = output.lower()
    marker = support_marker.lower()
    if marker not in normalized_output and marker.replace(":", "") + "-supported" not in normalized_output:
        raise AssertionError(
            "Expected support marker %r for %s ^%s, got:\n%s"
            % (support_marker, identifier, attribute, output)
        )


class SoarCliSession(object):
    def __init__(self, cli_path, working_dir):
        self._cli_path = str(cli_path)
        self._working_dir = str(working_dir)
        self._master_fd = None
        self._proc = None
        self.transcript = []

    def __enter__(self):
        master_fd, slave_fd = pty.openpty()
        self._proc = subprocess.Popen(
            [self._cli_path],
            cwd=self._working_dir,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
        )
        os.close(slave_fd)
        self._master_fd = master_fd
        self.transcript.append(self.read_until_prompt())
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            if self._proc and self._proc.poll() is None:
                self.send_command("quit")
                self._proc.wait(timeout=5.0)
        except Exception:
            if self._proc and self._proc.poll() is None:
                self._proc.kill()
                self._proc.wait(timeout=5.0)
        finally:
            if self._master_fd is not None:
                os.close(self._master_fd)

    def read_until_prompt(self, timeout=15.0):
        deadline = time.time() + timeout
        chunks = []
        while time.time() < deadline:
            remaining = max(0.0, deadline - time.time())
            ready, _, _ = select.select([self._master_fd], [], [], remaining)
            if not ready:
                break

            try:
                data = os.read(self._master_fd, 4096)
            except OSError as exc:
                if exc.errno == 5:
                    break
                raise

            if not data:
                break

            chunks.append(data.decode(errors="replace"))
            normalized_text = normalize("".join(chunks))
            if PROMPT in normalized_text:
                return normalized_text

        raise TimeoutError("Timed out waiting for prompt. Collected:\n%s" % normalize("".join(chunks)))

    def send_command(self, command):
        os.write(self._master_fd, command.encode("utf-8") + b"\n")

    def run(self, command, validator=None, timeout=15.0):
        self.send_command(command)
        output = self.read_until_prompt(timeout=timeout)
        self.transcript.append("\n>>> %s\n%s" % (command, output))
        if validator:
            validator(output)
        return output


class AgentStateRoundtripHarness(object):
    def __init__(self):
        self.cli_path = find_cli_binary()

    def scenario_path(self, name):
        scenario = AGENT_DIR / name
        if not scenario.exists():
            raise FileNotFoundError("Scenario file not found: %s" % scenario)
        return scenario

    def new_session(self):
        temp_dir = tempfile.TemporaryDirectory(prefix="soar-agent-state-")
        session = SoarCliSession(self.cli_path, temp_dir.name)
        return temp_dir, session

    def load_scenario(self, session, scenario_name):
        scenario = self.scenario_path(scenario_name)
        session.run(
            "source %s" % scenario,
            validator=lambda output: expect_not_contains(output, "Error", "Scenario source reported an error."),
        )

    def assert_output_differs(self, before_output, after_output, description):
        if command_result(before_output) == command_result(after_output):
            raise AssertionError("%s\nBefore:\n%s\n\nAfter:\n%s" % (description, before_output, after_output))

    def assert_output_matches(self, expected_output, actual_output, description):
        if command_result(expected_output) != command_result(actual_output):
            raise AssertionError("%s\nExpected:\n%s\n\nActual:\n%s" % (description, expected_output, actual_output))

    def reload_snapshot(self, session, snapshot_name, baseline_prints=None):
        session.run("soar init")
        session.run("production excise --all")
        session.run("run 1")

        baseline_prints = baseline_prints or {}
        for command, baseline_output in baseline_prints.items():
            post_init_output = session.run(command)
            self.assert_output_differs(
                baseline_output,
                post_init_output,
                "Printed working memory did not change after soar init and run 1.",
            )

        session.run(
            "loadKernelState %s" % snapshot_name,
            validator=lambda output: expect_contains(output, "Kernel state loaded.", "Expected snapshot reload confirmation."),
        )

        for command, baseline_output in baseline_prints.items():
            restored_output = session.run(command)
            self.assert_output_matches(
                baseline_output,
                restored_output,
                "Printed working memory did not match the saved snapshot after load.",
            )

    def transcript_text(self, session):
        return "".join(session.transcript)