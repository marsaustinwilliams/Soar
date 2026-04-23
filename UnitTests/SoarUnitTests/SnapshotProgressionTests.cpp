#include "SnapshotProgressionTests.hpp"

#include "SoarHelper.hpp"
#include "sml_ClientAnalyzedXML.h"

#include <sstream>

namespace
{
std::string resolve_resource_name(const std::string& categoryName, const std::string& testName)
{
    std::string sourceName = categoryName + "_" + testName + ".soar";
    std::string path = SoarHelper::GetResource(sourceName);

    if (path.empty())
    {
        sourceName = testName + ".soar";
        path = SoarHelper::GetResource(sourceName);
    }

    if (path.empty() && testName.find("test") == 0)
    {
        sourceName = testName.substr(std::string("test").size()) + ".soar";
        path = SoarHelper::GetResource(sourceName);
    }

    return path;
}
}

void SnapshotProgressionTests::runHarnessTest(const std::string& categoryName, const std::string& testName, int expectedDecisions)
{
    const std::string path = resolve_resource_name(categoryName, testName);
    assertNonZeroSize_msg("Could not find test file '" + categoryName + "_" + testName + ".soar'", path);
    const char* result = agent->ExecuteCommandLine(("source \"" + path + "\"").c_str());

    runner->output << "Loaded Productions for " << categoryName << "_" << testName << ":" << std::endl;
    runner->output << result << std::endl;

    result = agent->ExecuteCommandLine("soar stop-phase apply");
    runner->output << "Set Stop Phase: " << result << std::endl;

    SoarHelper::check_learning_override(agent);

    const std::string snapshotStem = categoryName + "_" + testName;
    if (expectedDecisions >= 0)
    {
        runDecisionSteps(expectedDecisions + 1, snapshotStem);
        result = "";
    }
    else
    {
        if (SoarHelper::snapshot_every_step && snapshotRebuildSupported())
        {
            SoarHelper::run_self_forever(agent, snapshotStem, &runner->output);
            result = "";
        }
        else
        {
            result = agent->RunSelfForever();
        }
    }

    runner->output << std::endl << result << std::endl;

    assertTrue_msg(testName + " functional test did not halt", halted);
    assertFalse_msg(testName + " functional test failed", failed);
    if (expectedDecisions >= 0)
    {
        assertEquals(expectedDecisions, SoarHelper::getDecisionPhasesCount(agent));
    }

    agent->ExecuteCommandLine("stats");
}

void SnapshotProgressionTests::saveChunks(const char* testName, bool directSourceChunks)
{
    const char* command = directSourceChunks ? "output command-to-file temp_chunks.soar print -fcri"
                                             : "output command-to-file temp_chunks.soar print -fcr";
    SoarHelper::agent_command(agent, command);
}

void SnapshotProgressionTests::sourceSavedChunks(const char* testName)
{
    (void) testName;
    SoarHelper::agent_command(agent, "source temp_chunks.soar");
}

void SnapshotProgressionTests::verifyChunk(const char* categoryName, const char* testName, int64_t expectedChunks, bool directSourceChunks)
{
    SoarHelper::close_log(agent);
    saveChunks(testName, directSourceChunks);
    if (SoarHelper::save_after_action_report)
    {
        SoarHelper::agent_command(agent, "explain after-action-report on");
    }
    SoarHelper::init_check_to_find_refcount_leaks(agent);
    tearDown(false);
    setUp();
    SoarHelper::continue_log(agent, testName);
    sourceSavedChunks(testName);

    sml::ClientAnalyzedXML response;
    const std::string sourceName = std::string(categoryName) + "_" + testName + "_expected.soar";
    const std::string path = SoarHelper::GetResource(sourceName);
    assertNonZeroSize_msg("Could not find test file '" + sourceName + "'", path);

    agent->ExecuteCommandLineXML(("source \"" + path + "\"").c_str(), &response);
    const int ignored = response.GetArgInt(sml::sml_Names::kParamIgnoredProductionCount, -1);

    std::ostringstream message;
    if (ignored < expectedChunks)
    {
        message << "Only learned " << ignored << " of the expected " << expectedChunks << ".";
    }
    else
    {
        std::cout << " " << ignored << "/" << expectedChunks << " ";
    }

    assertTrue_msg(message.str().c_str(), ignored >= expectedChunks);
    SoarHelper::close_log(agent);
}

void SnapshotProgressionTests::checkChunk(const char* categoryName, const char* testName, int64_t decisions, int64_t expectedChunks, bool directSourceChunks)
{
    SoarHelper::start_log(agent, testName);
    assertTrue_msg(std::string("Could not find ") + categoryName + " test file '" + testName + "'",
                   SoarHelper::source(agent, categoryName, testName));
    if (!SoarHelper::no_explainer)
    {
        SoarHelper::agent_command(agent, "explain all on");
        SoarHelper::agent_command(agent, "explain just on");
    }

    SoarHelper::check_learning_override(agent);
    SoarHelper::run_self(agent, static_cast<int>(decisions), std::string(categoryName) + "_" + testName, &runner->output, sml::sml_DECIDE);
    assertTrue_msg(agent->GetLastErrorDescription(), agent->GetLastCommandLineResult());

    if (std::string(testName) == "All_Test_Types")
    {
        for (const char* prod_name : {"init-superstate", "propose*top", "init-substate", "propose*test", "apply2"})
        {
            runner->output << "\n>>> fc " << prod_name << "\n"
                           << agent->ExecuteCommandLine((std::string("fc ") + prod_name).c_str()) << std::endl;
        }
        runner->output << "\n>>> print --stack\n" << agent->ExecuteCommandLine("print --stack") << std::endl;
    }

    verifyChunk(categoryName, testName, expectedChunks, directSourceChunks);
}

void SnapshotProgressionTests::testGetGoalStack()
{
    SoarHelper::run_self(agent, 3, "AgentTest_testGetGoalStack", &runner->output);
    const std::vector<std::string> goalStack = SoarHelper::getGoalStack(agent);
    assertEquals(size_t(4), goalStack.size());
    assertEquals_vector(goalStack, std::vector<std::string>({"S1", "S2", "S3", "S4"}));
}

void SnapshotProgressionTests::testBasicElaborationAndMatch()
{
    runHarnessTest("BasicTests", "testBasicElaborationAndMatch", 0);
}

void SnapshotProgressionTests::testInitialState()
{
    runHarnessTest("BasicTests", "testInitialState", 0);
}

void SnapshotProgressionTests::testString()
{
    runHarnessTest("BuiltinRHSTests", "testString", 0);
}

void SnapshotProgressionTests::All_Test_Types()
{
    checkChunk("ChunkingTests", "All_Test_Types", 4, 1);
}

void SnapshotProgressionTests::Chunk_Superstate_Operator_Preference()
{
    checkChunk("ChunkingTests", "Chunk_Superstate_Operator_Preference", 3, 1);
}

void SnapshotProgressionTests::Justifications_Get_New_Identities()
{
    checkChunk("ChunkingTests", "Justifications_Get_New_Identities", 4, 1);
}

void SnapshotProgressionTests::Singletons()
{
    checkChunk("ChunkingTests", "Singletons", 3, 2);
}

void SnapshotProgressionTests::Operator_Selection_Knowledge_Ghost_Operator()
{
    checkChunk("ChunkingTests", "Operator_Selection_Knowledge_Ghost_Operator", 4, 1);
}

void SnapshotProgressionTests::All_Test_Types_FinalRoundtrip()
{
    const bool previous_snapshot_mode = SoarHelper::snapshot_every_step;
    SoarHelper::snapshot_every_step = false;

    SoarHelper::start_log(agent, "All_Test_Types_FinalRoundtrip");
    assertTrue_msg("Could not find ChunkingTests test file 'All_Test_Types'",
                   SoarHelper::source(agent, "ChunkingTests", "All_Test_Types"));
    if (!SoarHelper::no_explainer)
    {
        SoarHelper::agent_command(agent, "explain all on");
        SoarHelper::agent_command(agent, "explain just on");
    }
    SoarHelper::check_learning_override(agent);
    SoarHelper::run_self(agent, 4, "ChunkingTests_All_Test_Types_final_roundtrip", &runner->output, sml::sml_DECIDE);
    assertTrue_msg(agent->GetLastErrorDescription(), agent->GetLastCommandLineResult());
    assertTrue_msg("Final roundtrip failed for All_Test_Types",
                   SoarHelper::snapshot_and_restore(agent, "All_Test_Types_final_roundtrip", &runner->output));
    verifyChunk("ChunkingTests", "All_Test_Types", 1, false);

    SoarHelper::snapshot_every_step = previous_snapshot_mode;
}

void SnapshotProgressionTests::runAllTestTypesWithSingleRoundtrip(int roundtripAfterStep)
{
    const bool previous_snapshot_mode = SoarHelper::snapshot_every_step;
    SoarHelper::snapshot_every_step = false;

    SoarHelper::start_log(agent, "All_Test_Types_SingleRoundtrip");
    assertTrue_msg("Could not find ChunkingTests test file 'All_Test_Types'",
                   SoarHelper::source(agent, "ChunkingTests", "All_Test_Types"));
    if (!SoarHelper::no_explainer)
    {
        SoarHelper::agent_command(agent, "explain all on");
        SoarHelper::agent_command(agent, "explain just on");
    }

    auto logFiringCounts = [this]()
    {
        for (const char* prod_name : {"init-superstate", "propose*top", "init-substate", "propose*test", "apply2"})
        {
            runner->output << "\n>>> fc " << prod_name << "\n"
                           << agent->ExecuteCommandLine((std::string("fc ") + prod_name).c_str()) << std::endl;
        }
    };

    auto logOperatorPreferences = [this]()
    {
        for (const char* command : {"preferences S1 operator", "preferences S2 operator"})
        {
            runner->output << "\n>>> " << command << "\n"
                           << agent->ExecuteCommandLine(command) << std::endl;
        }
    };

    auto logProductionMatches = [this]()
    {
        for (const char* prod_name : {"propose*test", "apply2"})
        {
            runner->output << "\n>>> production matches " << prod_name << "\n"
                           << agent->ExecuteCommandLine((std::string("production matches ") + prod_name).c_str()) << std::endl;
        }
    };

    SoarHelper::check_learning_override(agent);

    SoarHelper::run_self(agent,
                         roundtripAfterStep,
                         "ChunkingTests_All_Test_Types_pre_roundtrip",
                         &runner->output,
                         sml::sml_DECIDE);
    assertTrue_msg(agent->GetLastErrorDescription(), agent->GetLastCommandLineResult());
    logFiringCounts();
    logOperatorPreferences();
    logProductionMatches();
    runner->output << "\n>>> print --stack\n" << agent->ExecuteCommandLine("print --stack") << std::endl;
    runner->output << "\n>>> production matches --assertions --names\n"
                   << agent->ExecuteCommandLine("production matches --assertions --names") << std::endl;
    runner->output << "\n>>> production matches init-substate\n"
                   << agent->ExecuteCommandLine("production matches init-substate") << std::endl;
    assertTrue_msg("Intermediate roundtrip failed for All_Test_Types",
                   SoarHelper::snapshot_and_restore(agent,
                                                    "All_Test_Types_roundtrip_after_step_" + std::to_string(roundtripAfterStep),
                                                    &runner->output));
    logFiringCounts();
    logOperatorPreferences();
    logProductionMatches();
    runner->output << "\n>>> print --stack\n" << agent->ExecuteCommandLine("print --stack") << std::endl;
    runner->output << "\n>>> production matches --assertions --names\n"
                   << agent->ExecuteCommandLine("production matches --assertions --names") << std::endl;
    runner->output << "\n>>> production matches init-substate\n"
                   << agent->ExecuteCommandLine("production matches init-substate") << std::endl;
    SoarHelper::run_self(agent,
                         4 - roundtripAfterStep,
                         "ChunkingTests_All_Test_Types_post_roundtrip",
                         &runner->output,
                         sml::sml_DECIDE);
    assertTrue_msg(agent->GetLastErrorDescription(), agent->GetLastCommandLineResult());
    logFiringCounts();
    logOperatorPreferences();
    logProductionMatches();
    verifyChunk("ChunkingTests", "All_Test_Types", 1, false);

    SoarHelper::snapshot_every_step = previous_snapshot_mode;
}

void SnapshotProgressionTests::All_Test_Types_RoundtripAfterStep1()
{
    runAllTestTypesWithSingleRoundtrip(1);
}

void SnapshotProgressionTests::All_Test_Types_RoundtripAfterStep2()
{
    runAllTestTypesWithSingleRoundtrip(2);
}

void SnapshotProgressionTests::All_Test_Types_RoundtripAfterStep3()
{
    runAllTestTypesWithSingleRoundtrip(3);
}