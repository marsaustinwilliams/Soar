#!/usr/bin/env python3

import os
import re
import unittest

from cli_agent_state_test_lib import (
    AgentStateRoundtripHarness,
    expect_contains,
    expect_firing_count,
    expect_not_contains,
    expect_supported_preference,
)


class AgentStateCliRoundtripTests(unittest.TestCase):
    def setUp(self):
        self.harness = AgentStateRoundtripHarness()
        self.temp_dir, self.session = self.harness.new_session()
        self.session.__enter__()

    def tearDown(self):
        try:
            self.session.__exit__(None, None, None)
        finally:
            self.temp_dir.cleanup()

    def extract_identifier(self, output, attribute):
        match = re.search(r"\^%s\s+([A-Z][0-9]+)" % re.escape(attribute), output)
        if not match:
            raise AssertionError("Unable to find identifier for ^%s in output:\n%s" % (attribute, output))
        return match.group(1)

    def test_existing_basic_elaboration_agent_roundtrip(self):
        scenario_path = os.path.abspath(
            os.path.join(
                os.path.dirname(__file__),
                "..",
                "SoarTestAgents",
                "BasicTests_testBasicElaborationAndMatch.soar",
            )
        )

        self.session.run(
            "source %s" % scenario_path,
            lambda output: expect_not_contains(output, "Error", "Existing BasicElaboration scenario source reported an error."),
        )
        self.session.run("run 1")

        saved_s1 = self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^jug J1", "Expected first jug on the top state before snapshot."),
                expect_contains(output, "^jug J2", "Expected second jug on the top state before snapshot."),
            ],
        )
        saved_j1 = self.session.run("p J1", lambda output: expect_contains(output, "^contents 0", "Expected first jug contents before snapshot."))
        saved_j2 = self.session.run("p J2", lambda output: expect_contains(output, "^contents 1", "Expected second jug contents before snapshot."))
        self.session.run("fc init*waterjug", lambda output: expect_firing_count(output, "init*waterjug", 1))
        self.session.run("fc monitor*contents", lambda output: expect_firing_count(output, "monitor*contents", 2))
        self.session.run("saveKernelState existing_basic_elaboration.bin")

        self.harness.reload_snapshot(
            self.session,
            "existing_basic_elaboration.bin",
            {"p S1": saved_s1, "p J1": saved_j1, "p J2": saved_j2},
        )

        self.session.run("p J1", lambda output: expect_contains(output, "^contents 0", "Expected restored first jug contents after snapshot reload."))
        self.session.run("p J2", lambda output: expect_contains(output, "^contents 1", "Expected restored second jug contents after snapshot reload."))
        self.session.run("fc init*waterjug", lambda output: expect_firing_count(output, "init*waterjug", 1))
        self.session.run("fc monitor*contents", lambda output: expect_firing_count(output, "monitor*contents", 2))

    def test_basic_applied_roundtrip(self):
        propose_name = "propose*austin*operator"
        apply_name = "apply*austin*operator"

        self.harness.load_scenario(self.session, "basic_roundtrip.soar")
        self.session.run("run 2")
        saved_s1 = self.session.run("p S1", lambda output: expect_contains(output, "AustinIsAbeast", "Expected apply augmentation before snapshot."))
        self.session.run("preferences S1 onGod", lambda output: expect_supported_preference(output, "S1", "onGod", ":O"))
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))
        self.session.run("saveKernelState basic_applied.bin")

        self.harness.reload_snapshot(self.session, "basic_applied.bin", {"p S1": saved_s1})

        self.session.run("p S1", lambda output: expect_contains(output, "AustinIsAbeast", "Expected restored augmentation after applied-state reload."))
        self.session.run("preferences S1 onGod", lambda output: expect_supported_preference(output, "S1", "onGod", ":O"))
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))
        self.session.run("run 1")
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))

    def test_basic_proposal_roundtrip(self):
        propose_name = "propose*austin*operator"
        apply_name = "apply*austin*operator"

        self.harness.load_scenario(self.session, "basic_roundtrip.soar")
        self.session.run("run -p 3")
        saved_s1 = self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^operator O1", "Expected selected operator before proposal-phase snapshot."),
                expect_not_contains(output, "AustinIsAbeast", "Did not expect apply augmentation before proposal-phase snapshot."),
            ],
        )
        self.session.run("preferences S1 operator", lambda output: expect_supported_preference(output, "S1", "operator", ":I"))
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 0))
        self.session.run("saveKernelState basic_proposal.bin")

        self.harness.reload_snapshot(self.session, "basic_proposal.bin", {"p S1": saved_s1})

        self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^operator O1", "Expected selected operator after proposal-phase restore."),
                expect_not_contains(output, "AustinIsAbeast", "Did not expect apply augmentation immediately after proposal-phase restore."),
            ],
        )
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 0))
        self.session.run("run 1")
        self.session.run("p S1", lambda output: expect_contains(output, "AustinIsAbeast", "Expected apply augmentation after resuming from restored proposal phase."))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))

    def test_multi_operator_preference_roundtrip(self):
        alpha_propose = "propose*alpha*operator"
        beta_propose = "propose*beta*operator"
        alpha_over_beta = "prefer*alpha*over*beta"
        alpha_apply = "apply*alpha*operator"
        beta_apply = "apply*beta*operator"

        self.harness.load_scenario(self.session, "multi_operator_preference.soar")
        self.session.run("run -p 3")
        saved_s1 = self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^operator A1", "Expected selected alpha operator identifier before snapshot."),
                expect_contains(output, "^operator B1 +", "Expected beta operator preference before snapshot."),
                expect_not_contains(output, "^winner alpha", "Did not expect winner marker before apply."),
                expect_not_contains(output, "^winner beta", "Did not expect loser marker before apply."),
            ],
        )
        self.session.run(
            "preferences S1 operator",
            lambda output: [
                expect_supported_preference(output, "S1", "operator", ":I"),
                expect_contains(output, "alpha-op", "Expected alpha operator name in operator preferences before snapshot."),
                expect_contains(output, "beta-op", "Expected beta operator name in operator preferences before snapshot."),
            ],
        )
        self.session.run("fc %s" % alpha_propose, lambda output: expect_firing_count(output, alpha_propose, 1))
        self.session.run("fc %s" % beta_propose, lambda output: expect_firing_count(output, beta_propose, 1))
        self.session.run("fc %s" % alpha_over_beta, lambda output: expect_firing_count(output, alpha_over_beta, 1))
        self.session.run("fc %s" % alpha_apply, lambda output: expect_firing_count(output, alpha_apply, 0))
        self.session.run("fc %s" % beta_apply, lambda output: expect_firing_count(output, beta_apply, 0))
        self.session.run("saveKernelState multi_operator.bin")

        self.harness.reload_snapshot(self.session, "multi_operator.bin", {"p S1": saved_s1})

        self.session.run("fc %s" % alpha_propose, lambda output: expect_firing_count(output, alpha_propose, 1))
        self.session.run("fc %s" % beta_propose, lambda output: expect_firing_count(output, beta_propose, 1))
        self.session.run("fc %s" % alpha_apply, lambda output: expect_firing_count(output, alpha_apply, 0))
        self.session.run("fc %s" % beta_apply, lambda output: expect_firing_count(output, beta_apply, 0))
        self.session.run("run 1")
        self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^winner alpha", "Expected preferred alpha operator to apply after restore."),
                expect_not_contains(output, "^winner beta", "Did not expect non-preferred beta operator to apply after restore."),
            ],
        )
        self.session.run("preferences S1 winner", lambda output: expect_supported_preference(output, "S1", "winner", ":O"))
        self.session.run("fc %s" % alpha_apply, lambda output: expect_firing_count(output, alpha_apply, 1))
        self.session.run("fc %s" % beta_apply, lambda output: expect_firing_count(output, beta_apply, 0))

    def test_multi_apply_chain_roundtrip(self):
        propose_name = "propose*chain*operator"
        mark_name = "apply*chain*mark"
        finish_name = "apply*chain*finish"

        self.harness.load_scenario(self.session, "multi_apply_chain.soar")
        self.session.run("run 2")
        saved_s1 = self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^chain-stage primed", "Expected first apply-stage marker before snapshot."),
                expect_contains(output, "^chain-result complete", "Expected second apply-stage marker before snapshot."),
            ],
        )
        self.session.run("preferences S1 chain-result", lambda output: expect_supported_preference(output, "S1", "chain-result", ":O"))
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % mark_name, lambda output: expect_firing_count(output, mark_name, 1))
        self.session.run("fc %s" % finish_name, lambda output: expect_firing_count(output, finish_name, 1))
        self.session.run("saveKernelState multi_apply.bin")

        self.harness.reload_snapshot(self.session, "multi_apply.bin", {"p S1": saved_s1})

        self.session.run("p S1", lambda output: expect_contains(output, "^chain-result complete", "Expected multi-apply result after restore."))
        self.session.run("fc %s" % mark_name, lambda output: expect_firing_count(output, mark_name, 1))
        self.session.run("fc %s" % finish_name, lambda output: expect_firing_count(output, finish_name, 1))
        self.session.run("run 1")
        self.session.run("fc %s" % mark_name, lambda output: expect_firing_count(output, mark_name, 1))
        self.session.run("fc %s" % finish_name, lambda output: expect_firing_count(output, finish_name, 1))

    def test_halted_learning_roundtrip(self):
        propose_foo = "propose*foo"
        propose_bar = "propose*bar"
        apply_bar = "apply*bar"
        done = "done"

        self.harness.load_scenario(self.session, "halted_learning_roundtrip.soar")
        self.session.run("chunk unflagged")
        self.session.run("run")
        saved_s1 = self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^result jig", "Expected halted top-state result before snapshot."),
                expect_contains(output, "^operator O1", "Expected selected top-state operator before snapshot."),
            ],
        )
        saved_s2 = self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^name foo", "Expected halted substate marker before snapshot."),
                expect_contains(output, "^operator O2", "Expected halted substate operator before snapshot."),
            ],
        )
        self.session.run("fc %s" % propose_foo, lambda output: expect_firing_count(output, propose_foo, 1))
        self.session.run("fc %s" % propose_bar, lambda output: expect_firing_count(output, propose_bar, 1))
        self.session.run("fc %s" % apply_bar, lambda output: expect_firing_count(output, apply_bar, 1))
        self.session.run("fc %s" % done, lambda output: expect_firing_count(output, done, 1))
        self.session.run("saveKernelState halted_learning.bin")

        self.harness.reload_snapshot(
            self.session,
            "halted_learning.bin",
            {"p S1": saved_s1, "p S2": saved_s2},
        )

        self.session.run("fc %s" % propose_foo, lambda output: expect_firing_count(output, propose_foo, 1))
        self.session.run("fc %s" % propose_bar, lambda output: expect_firing_count(output, propose_bar, 1))
        self.session.run("fc %s" % apply_bar, lambda output: expect_firing_count(output, apply_bar, 1))
        self.session.run("fc %s" % done, lambda output: expect_firing_count(output, done, 1))

    def test_stopped_gds_roundtrip(self):
        scenario_path = os.path.abspath(
            os.path.join(
                os.path.dirname(__file__),
                "..",
                "SoarTestAgents",
                "testGDS_Failed_Justification_Crash.soar",
            )
        )

        self.session.run(
            "source %s" % scenario_path,
            lambda output: expect_not_contains(output, "Error", "GDS crash scenario source reported an error."),
        )
        self.session.run("w 0")
        self.session.run("run", timeout=20.0)

        saved_s1 = self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^io I1", "Expected top-state IO link before snapshot."),
                expect_contains(output, "^operator O1", "Expected top-state operator before snapshot."),
            ],
        )
        self.session.run("fc apply*create-output", lambda output: expect_firing_count(output, "apply*create-output", 1))
        self.session.run("saveKernelState stopped_gds.bin")

        self.harness.reload_snapshot(
            self.session,
            "stopped_gds.bin",
            {"p S1": saved_s1},
        )

        self.session.run(
            "p S1",
            lambda output: [
                expect_contains(output, "^io I1", "Expected restored top-state IO link after snapshot reload."),
                expect_contains(output, "^operator O1", "Expected restored top-state operator after snapshot reload."),
            ],
        )
        self.session.run("fc apply*create-output", lambda output: expect_firing_count(output, "apply*create-output", 1))

    def test_tie_impasse_roundtrip(self):
        alpha_propose = "propose*alpha*operator"
        beta_propose = "propose*beta*operator"
        impasse_elaborate = "tie*substate*elaborate"
        resolve_propose = "tie*substate*propose*resolve"
        resolve_apply = "tie*substate*apply*resolve"

        self.harness.load_scenario(self.session, "tie_impasse_substate.soar")
        self.session.run("run 2")
        saved_s2 = self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^impasse tie", "Expected tie impasse substate before snapshot."),
                expect_contains(output, "^superstate S1", "Expected substate to point back to S1 before snapshot."),
                expect_contains(output, "^item A1", "Expected first tied operator to appear in impasse state before snapshot."),
                expect_contains(output, "^item B1", "Expected second tied operator to appear in impasse state before snapshot."),
                expect_contains(output, "^substate-kind tie", "Expected substate elaboration before snapshot."),
                expect_contains(output, "^operator O1", "Expected local substate operator proposal before snapshot."),
                expect_not_contains(output, "^substate-result resolved", "Did not expect substate apply result before impasse snapshot."),
            ],
        )
        saved_s1 = self.session.run("p S1", lambda output: expect_contains(output, "^substate-visible yes", "Expected superstate marker from substate before snapshot."))
        self.session.run("fc %s" % alpha_propose, lambda output: expect_firing_count(output, alpha_propose, 1))
        self.session.run("fc %s" % beta_propose, lambda output: expect_firing_count(output, beta_propose, 1))
        self.session.run("fc %s" % impasse_elaborate, lambda output: expect_firing_count(output, impasse_elaborate, 1))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 0))
        self.session.run("saveKernelState tie_impasse.bin")

        self.harness.reload_snapshot(self.session, "tie_impasse.bin", {"p S1": saved_s1, "p S2": saved_s2})

        self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^impasse tie", "Expected tie impasse substate after restore."),
                expect_contains(output, "^substate-kind tie", "Expected substate elaboration after restore."),
                expect_contains(output, "^operator O1", "Expected local substate operator proposal after restore."),
                expect_not_contains(output, "^substate-result resolved", "Did not expect substate apply result immediately after impasse restore."),
            ],
        )
        self.session.run("p S1", lambda output: expect_contains(output, "^substate-visible yes", "Expected superstate marker after impasse restore."))
        self.session.run("fc %s" % impasse_elaborate, lambda output: expect_firing_count(output, impasse_elaborate, 1))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 0))

    def test_substate_apply_roundtrip(self):
        resolve_propose = "tie*substate*propose*resolve"
        resolve_apply = "tie*substate*apply*resolve"

        self.harness.load_scenario(self.session, "tie_impasse_substate.soar")
        self.session.run("run 3")
        saved_s2 = self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^operator O1", "Expected selected substate operator before snapshot."),
                expect_contains(output, "^substate-result resolved", "Expected substate-local apply result before snapshot."),
            ],
        )
        saved_s1 = self.session.run("p S1", lambda output: expect_contains(output, "^substate-resolution complete", "Expected substate result to propagate to S1 before snapshot."))
        self.session.run("preferences S2 substate-result", lambda output: expect_supported_preference(output, "S2", "substate-result", ":O"))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 1))
        self.session.run("saveKernelState substate_apply.bin")

        self.harness.reload_snapshot(self.session, "substate_apply.bin", {"p S1": saved_s1, "p S2": saved_s2})

        self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^substate-result resolved", "Expected substate-local apply result after restore."),
                expect_contains(output, "^operator O1", "Expected selected substate operator after restore."),
            ],
        )
        self.session.run("p S1", lambda output: expect_contains(output, "^substate-resolution complete", "Expected propagated superstate result after restore."))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 1))
        self.session.run("run 1")
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 1))

    def test_no_change_impasse_roundtrip(self):
        impasse_elaborate = "no-change*substate*elaborate"
        resolve_propose = "no-change*substate*propose*resolve"
        resolve_apply = "no-change*substate*apply*resolve"

        self.harness.load_scenario(self.session, "no_change_impasse_substate.soar")
        self.session.run("run 2")
        saved_s2 = self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^impasse no-change", "Expected no-change impasse substate before snapshot."),
                expect_contains(output, "^attribute state", "Expected no-change impasse to target state selection before snapshot."),
                expect_contains(output, "^superstate S1", "Expected no-change substate to point back to S1 before snapshot."),
                expect_contains(output, "^substate-kind no-change", "Expected no-change substate elaboration before snapshot."),
                expect_contains(output, "^operator O1", "Expected local no-change resolution operator proposal before snapshot."),
                expect_not_contains(output, "^substate-result stabilized", "Did not expect no-change substate apply result before snapshot."),
            ],
        )
        saved_s1 = self.session.run("p S1", lambda output: expect_contains(output, "^nc-substate-visible yes", "Expected superstate marker from no-change substate before snapshot."))
        self.session.run("fc %s" % impasse_elaborate, lambda output: expect_firing_count(output, impasse_elaborate, 1))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 0))
        self.session.run("saveKernelState no_change_impasse.bin")

        self.harness.reload_snapshot(self.session, "no_change_impasse.bin", {"p S1": saved_s1, "p S2": saved_s2})

        self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^impasse no-change", "Expected no-change impasse substate after restore."),
                expect_contains(output, "^substate-kind no-change", "Expected no-change substate elaboration after restore."),
                expect_contains(output, "^operator O1", "Expected local no-change resolution operator proposal after restore."),
                expect_not_contains(output, "^substate-result stabilized", "Did not expect no-change apply result immediately after restore."),
            ],
        )
        self.session.run("p S1", lambda output: expect_contains(output, "^nc-substate-visible yes", "Expected superstate marker after no-change impasse restore."))
        self.session.run("fc %s" % impasse_elaborate, lambda output: expect_firing_count(output, impasse_elaborate, 1))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 0))

    def test_no_change_substate_apply_roundtrip(self):
        resolve_propose = "no-change*substate*propose*resolve"
        resolve_apply = "no-change*substate*apply*resolve"

        self.harness.load_scenario(self.session, "no_change_impasse_substate.soar")
        self.session.run("run 3")
        saved_s2 = self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^impasse no-change", "Expected no-change impasse substate before snapshot."),
                expect_contains(output, "^operator O1", "Expected selected no-change substate operator before snapshot."),
                expect_contains(output, "^substate-result stabilized", "Expected no-change substate apply result before snapshot."),
            ],
        )
        saved_s1 = self.session.run("p S1", lambda output: expect_contains(output, "^no-change-resolution complete", "Expected no-change result to propagate to S1 before snapshot."))
        self.session.run("preferences S2 substate-result", lambda output: expect_supported_preference(output, "S2", "substate-result", ":O"))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 1))
        self.session.run("saveKernelState no_change_substate_apply.bin")

        self.harness.reload_snapshot(self.session, "no_change_substate_apply.bin", {"p S1": saved_s1, "p S2": saved_s2})

        self.session.run(
            "p S2",
            lambda output: [
                expect_contains(output, "^substate-result stabilized", "Expected no-change substate apply result after restore."),
                expect_contains(output, "^operator O1", "Expected selected no-change substate operator after restore."),
            ],
        )
        self.session.run("p S1", lambda output: expect_contains(output, "^no-change-resolution complete", "Expected propagated no-change result after restore."))
        self.session.run("fc %s" % resolve_propose, lambda output: expect_firing_count(output, resolve_propose, 1))
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 1))
        self.session.run("run 1")
        self.session.run("fc %s" % resolve_apply, lambda output: expect_firing_count(output, resolve_apply, 1))

    def test_nested_s3_roundtrip(self):
        self.harness.load_scenario(self.session, "nested_s3_substate.soar")
        self.session.run("run 3")
        saved_s3 = self.session.run(
            "p S3",
            lambda output: [
                expect_contains(output, "^impasse tie", "Expected nested S3 tie impasse before snapshot."),
                expect_contains(output, "^superstate S2", "Expected S3 to point back to S2 before snapshot."),
                expect_contains(output, "^stack-depth three", "Expected S3 elaboration marker before snapshot."),
                expect_contains(output, "^item O1", "Expected inherited S2 operator to appear in the nested S3 impasse before snapshot."),
                expect_contains(output, "^operator A3 +", "Expected nested S3 operator proposals before snapshot."),
                expect_contains(output, "^operator B3 +", "Expected nested S3 operator proposals before snapshot."),
                expect_not_contains(output, "^deep-result resolved", "Did not expect S3 apply result before snapshot."),
            ],
        )
        saved_s2 = self.session.run("p S2", lambda output: expect_contains(output, "^mid-stack yes", "Expected S2 marker while S3 is active before snapshot."))
        self.session.run("saveKernelState nested_s3.bin")

        self.harness.reload_snapshot(self.session, "nested_s3.bin", {"p S2": saved_s2, "p S3": saved_s3})

        self.session.run(
            "p S3",
            lambda output: [
                expect_contains(output, "^impasse tie", "Expected nested S3 tie impasse after restore."),
                expect_contains(output, "^stack-depth three", "Expected S3 elaboration marker after restore."),
                expect_contains(output, "^operator A3 +", "Expected nested S3 operator proposals after restore."),
                expect_contains(output, "^operator B3 +", "Expected nested S3 operator proposals after restore."),
                expect_not_contains(output, "^deep-result resolved", "Did not expect S3 apply result immediately after restore."),
            ],
        )

    def test_io_roundtrip(self):
        propose_name = "io*propose*react"
        apply_name = "io*apply*react"

        self.harness.load_scenario(self.session, "io_roundtrip.soar")
        self.session.run("wm add I2 ^sensor ready")
        self.session.run("run 2")
        saved_i2 = self.session.run("p I2", lambda output: expect_contains(output, "^sensor ready", "Expected input-link WME before snapshot."))
        saved_i3 = self.session.run(
            "p I3",
            lambda output: [
                expect_contains(output, "^status emitted", "Expected output-link status before snapshot."),
                expect_contains(output, "^command C", "Expected output-link command identifier before snapshot."),
            ],
        )
        command_id = self.extract_identifier(saved_i3, "command")
        saved_command = self.session.run("p %s" % command_id, lambda output: [expect_contains(output, "^name ping", "Expected output command name before snapshot."), expect_contains(output, "^payload ready", "Expected output command payload before snapshot.")])
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))
        self.session.run("saveKernelState io_roundtrip.bin")

        self.harness.reload_snapshot(self.session, "io_roundtrip.bin", {"p I2": saved_i2, "p I3": saved_i3, "p %s" % command_id: saved_command})

        self.session.run("p I2", lambda output: expect_contains(output, "^sensor ready", "Expected restored input-link WME after snapshot reload."))
        self.session.run("p I3", lambda output: expect_contains(output, "^status emitted", "Expected restored output-link status after snapshot reload."))
        self.session.run("p %s" % command_id, lambda output: expect_contains(output, "^payload ready", "Expected restored output command payload after snapshot reload."))
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))
        self.session.run("run 1")
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))

    def test_io_in_substate_roundtrip(self):
        propose_name = "io*substate*propose*emit"
        apply_name = "io*substate*apply*emit"

        self.harness.load_scenario(self.session, "io_substate_roundtrip.soar")
        self.session.run("wm add I2 ^sensor ready")
        self.session.run("run 3")
        saved_s2 = self.session.run("p S2", lambda output: expect_contains(output, "^io-substate-result sent", "Expected substate I/O result before snapshot."))
        saved_i2 = self.session.run("p I2", lambda output: expect_contains(output, "^sensor ready", "Expected input-link WME before snapshot."))
        saved_i3 = self.session.run(
            "p I3",
            lambda output: [
                expect_contains(output, "^substate-status emitted", "Expected substate output-link status before snapshot."),
                expect_contains(output, "^substate-command C", "Expected substate output-link command identifier before snapshot."),
            ],
        )
        command_id = self.extract_identifier(saved_i3, "substate-command")
        saved_command = self.session.run(
            "p %s" % command_id,
            lambda output: [
                expect_contains(output, "^name relay-from-substate", "Expected substate output command name before snapshot."),
                expect_contains(output, "^payload ready", "Expected substate output command payload before snapshot."),
            ],
        )
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))
        self.session.run("saveKernelState io_substate_roundtrip.bin")

        self.harness.reload_snapshot(
            self.session,
            "io_substate_roundtrip.bin",
            {"p S2": saved_s2, "p I2": saved_i2, "p I3": saved_i3, "p %s" % command_id: saved_command},
        )

        self.session.run("p S2", lambda output: expect_contains(output, "^io-substate-result sent", "Expected restored substate I/O result after snapshot reload."))
        self.session.run("p I2", lambda output: expect_contains(output, "^sensor ready", "Expected restored substate input-link WME after snapshot reload."))
        self.session.run("p I3", lambda output: expect_contains(output, "^substate-status emitted", "Expected restored substate output-link status after snapshot reload."))
        self.session.run("p %s" % command_id, lambda output: expect_contains(output, "^payload ready", "Expected restored substate output command payload after snapshot reload."))
        self.session.run("fc %s" % propose_name, lambda output: expect_firing_count(output, propose_name, 1))
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))
        self.session.run("run 1")
        self.session.run("fc %s" % apply_name, lambda output: expect_firing_count(output, apply_name, 1))


if __name__ == "__main__":
    unittest.main(verbosity=2)