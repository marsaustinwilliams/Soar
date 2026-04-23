//
//  SoarHelper.hpp
//  Prototype-UnitTesting
//
//  Created by Alex Turner on 6/17/15.
//  Copyright © 2015 University of Michigan – Soar Group. All rights reserved.
//

#ifndef SoarHelper_cpp
#define SoarHelper_cpp

//#define NEVER_LEARN                     // Overrides learning settings in many unit tests
//#define ALWAYS_LEARN

#include "sml_ClientAgent.h"
#include "sml_ClientKernel.h"
#include <ostream>
#include <string>

class SoarHelper
{
public:
	static int getDecisionPhasesCount(sml::Agent* agent);
	static int getD_CYCLE_COUNT(sml::Agent* agent);
	static int getE_CYCLE_COUNT(sml::Agent* agent);
	static int getPE_CYCLE_COUNT(sml::Agent* agent);
	static int getINNER_E_CYCLE_COUNT(sml::Agent* agent);

	static int getUserProductionCount(sml::Agent* agent);
	static int getChunkProductionCount(sml::Agent* agent);

	enum class StopPhase
	{
		INPUT,
		PROPOSE,
		DECIDE,
		APPLY,
		OUTPUT
	};

	// StopPhase, before=true/false
	static std::tuple<StopPhase, bool> getStopPhase(sml::Agent* agent);
	static void setStopPhase(sml::Agent* agent, StopPhase phase, bool before = true);

	static std::vector<std::string> getGoalStack(sml::Agent* agent);

	static std::string ResourceDirectory;
	static std::string GetResource(std::string resource);

    static bool source(sml::Agent* agent, const std::string& pCategoryName, const std::string& pTestName);
    static void init_check_to_find_refcount_leaks(sml::Agent* agent);
    static void check_learning_override(sml::Agent* agent);
    static void agent_command(sml::Agent* agent, const char* pCmd);
    static void add_log_dir_if_exists(std::string &lPath);
    static void start_log(sml::Agent* agent, const char* path);
    static void continue_log(sml::Agent* agent, const char* path);
    static void close_log(sml::Agent* agent);

    static bool no_explainer;
    static bool save_after_action_report;
    static bool save_logs;
    static bool no_init_soar;
    static bool run_as_unit_test;
	static bool snapshot_every_step;

	static std::string run_self(sml::Agent* agent,
						   int count,
						   const std::string& snapshotStem = "",
						   std::ostream* log = nullptr,
						   sml::smlRunStepSize stepSize = sml::sml_DECIDE);
	static std::string run_self_forever(sml::Agent* agent,
							  const std::string& snapshotStem = "",
							  std::ostream* log = nullptr,
							  sml::smlRunStepSize stepSize = sml::sml_DECIDE);
	static std::string run_all_agents_forever(sml::Kernel* kernel,
								   sml::Agent* checkpointAgent,
								   const std::string& snapshotStem = "",
								   std::ostream* log = nullptr,
								   sml::smlRunStepSize interleaveStepSize = sml::sml_PHASE);
	static bool should_snapshot_step(int stepNumber);
	static bool snapshot_and_restore(sml::Agent* agent, const std::string& snapshotStem, std::ostream* log = nullptr);
	static void normalize_after_snapshot_testing(sml::Agent* agent, std::ostream* log = nullptr);
	static std::string run_self_with_snapshots(sml::Agent* agent,
											   int count,
											   const std::string& snapshotStem,
											   std::ostream* log = nullptr,
											   sml::smlRunStepSize stepSize = sml::sml_DECIDE);

private:
	static std::string FindFile(std::string filename, std::string path);
	static std::string sanitizeSnapshotStem(const std::string& snapshotStem);

	static std::string getStats(sml::Agent* agent);
	static int parseForCount(std::string search, std::string countString);
};

std::ostream& operator<<(std::ostream& os, SoarHelper::StopPhase);

#endif /* SoarHelper_cpp */
