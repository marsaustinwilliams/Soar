//
//  SoarHelper.cpp
//  Prototype-UnitTesting
//
//  Created by Alex Turner on 6/17/15.
//  Copyright © 2015 University of Michigan – Soar Group. All rights reserved.
//

#include "SoarHelper.hpp"

#include "portability.h"
#include "portable-dirent.h"
#include "sml_ClientAnalyzedXML.h"
#include "TestHelpers.hpp"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <sstream>

std::string SoarHelper::ResourceDirectory = "./SoarUnitTests/";

bool SoarHelper::run_as_unit_test = true;
bool SoarHelper::no_explainer = false;
bool SoarHelper::save_after_action_report = false;
bool SoarHelper::save_logs = false;
bool SoarHelper::no_init_soar = false;
bool SoarHelper::snapshot_every_step = false;

namespace
{

std::string run_self_via_command_line(sml::Agent* agent, int count, sml::smlRunStepSize stepSize)
{
	std::string stepFlag;
	switch (stepSize)
	{
		case sml::sml_DECIDE:
			stepFlag = "-d";
			break;
		case sml::sml_PHASE:
			stepFlag = "-p";
			break;
		case sml::sml_ELABORATION:
			stepFlag = "-e";
			break;
		case sml::sml_UNTIL_OUTPUT:
			stepFlag = "-o";
			break;
		default:
			throw std::runtime_error("Unsupported run step size for command-line execution.");
	}

	const std::string command = "run --self " + stepFlag + " " + std::to_string(count);
	return agent->ExecuteCommandLine(command.c_str());
}

bool run_cli_and_require_success(sml::Agent* agent, const char* command, std::ostream* log)
{
	const std::string result = agent->ExecuteCommandLine(command);
	if (log)
	{
		*log << "\n>>> " << command << "\n" << result << std::endl;
	}
	return agent->GetLastCommandLineResult();
}

int snapshot_step_range_start()
{
	const char* env = std::getenv("SOAR_SNAPSHOT_STEP_START");
	if (!env || !env[0])
	{
		return 1;
	}

	char* parseEnd = nullptr;
	long parsed = std::strtol(env, &parseEnd, 10);
	if ((parseEnd == env) || (parsed < 1))
	{
		return 1;
	}

	return static_cast<int>(parsed);
}

int snapshot_step_range_end()
{
	const char* env = std::getenv("SOAR_SNAPSHOT_STEP_END");
	if (!env || !env[0])
	{
		return -1;
	}

	char* parseEnd = nullptr;
	long parsed = std::strtol(env, &parseEnd, 10);
	if ((parseEnd == env) || (parsed < 1))
	{
		return -1;
	}

	return static_cast<int>(parsed);
}

}

std::string SoarHelper::sanitizeSnapshotStem(const std::string& snapshotStem)
{
	std::string sanitized;
	sanitized.reserve(snapshotStem.size());

	for (char ch : snapshotStem)
	{
		if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')
		{
			sanitized.push_back(ch);
		}
		else
		{
			sanitized.push_back('_');
		}
	}

	if (sanitized.empty())
	{
		sanitized = "agent_snapshot";
	}

	return sanitized;
}

bool SoarHelper::snapshot_and_restore(sml::Agent* agent, const std::string& snapshotStem, std::ostream* log)
{
	const std::string filename = sanitizeSnapshotStem(snapshotStem) + ".snapshot.bin";
	if (!log)
	{
		std::remove(filename.c_str());
	}

	const std::string saveCommand = "saveKernelState \"" + filename + "\"";
	const std::string saveResult = agent->ExecuteCommandLine(saveCommand.c_str());
	if (log)
	{
		*log << "\n>>> " << saveCommand << "\n" << saveResult << std::endl;
	}
	if (!agent->GetLastCommandLineResult())
	{
		std::cerr << "[snapshot_and_restore] save failed for " << snapshotStem << std::endl;
		std::cerr << "[snapshot_and_restore] save output: " << saveResult << std::endl;
		return false;
	}

	struct stat fileInfo;
	if (stat(filename.c_str(), &fileInfo) != 0)
	{
		if (log)
		{
			*log << "Snapshot file was not created: " << filename << std::endl;
		}
		return false;
	}

	if (log)
	{
		const std::string preservedFilename = sanitizeSnapshotStem(snapshotStem) + ".saved.snapshot.bin";
		std::ifstream src(filename, std::ios::binary);
		std::ofstream dst(preservedFilename, std::ios::binary | std::ios::trunc);
		dst << src.rdbuf();
	}

	for (const char* command : {"soar init", "production excise --all", "run 1"})
	{
		if (!run_cli_and_require_success(agent, command, log))
		{
			std::cerr << "[snapshot_and_restore] reset command failed for " << snapshotStem
			          << ": " << command << std::endl;
			return false;
		}
	}

	const std::string loadCommand = "loadKernelState \"" + filename + "\"";
	const std::string loadResult = agent->ExecuteCommandLine(loadCommand.c_str());
	if (log)
	{
		*log << "\n>>> " << loadCommand << "\n" << loadResult << std::endl;
	}

	std::remove(filename.c_str());

	const bool cmdResult = agent->GetLastCommandLineResult();
	if (!cmdResult)
	{
		std::cerr << "[snapshot_and_restore] GetLastCommandLineResult=false for " << snapshotStem << std::endl;
		std::cerr << "[snapshot_and_restore] load output: " << loadResult << std::endl;
	}
	return cmdResult;
}

bool SoarHelper::should_snapshot_step(int stepNumber)
{
	if (!snapshot_every_step)
	{
		return false;
	}

	const int rangeStart = snapshot_step_range_start();
	const int rangeEnd = snapshot_step_range_end();
	if (stepNumber < rangeStart)
	{
		return false;
	}

	return (rangeEnd < 0) || (stepNumber <= rangeEnd);
}

std::string SoarHelper::run_self(sml::Agent* agent,
							 int count,
							 const std::string& snapshotStem,
							 std::ostream* log,
							 sml::smlRunStepSize stepSize)
{
	if (!snapshot_every_step)
	{
		return agent->RunSelf(count, stepSize);
	}

	return run_self_with_snapshots(agent, count, snapshotStem, log, stepSize);
}

std::string SoarHelper::run_self_forever(sml::Agent* agent,
								 const std::string& snapshotStem,
								 std::ostream* log,
								 sml::smlRunStepSize stepSize)
{
	if (!snapshot_every_step)
	{
		return agent->RunSelfForever();
	}

	std::string lastResult;
	for (int step = 0; ; ++step)
	{
		lastResult = agent->RunSelf(1, stepSize);
		if (log)
		{
			*log << std::endl << lastResult << std::endl;
		}

		const std::string stemBase = snapshotStem.empty() ? "run_forever" : snapshotStem;
		const int stepNumber = step + 1;
		const std::string perStepSnapshot = stemBase + "_step_" + std::to_string(stepNumber);
		if (should_snapshot_step(stepNumber) && !snapshot_and_restore(agent, perStepSnapshot, log))
		{
			throw std::runtime_error("Snapshot rebuild failed for " + perStepSnapshot);
		}

		if (agent->GetRunState() == sml::sml_RUNSTATE_HALTED)
		{
			break;
		}
	}

	return lastResult;
}

std::string SoarHelper::run_all_agents_forever(sml::Kernel* kernel,
								   sml::Agent* checkpointAgent,
								   const std::string& snapshotStem,
								   std::ostream* log,
								   sml::smlRunStepSize interleaveStepSize)
{
	if (!snapshot_every_step)
	{
		return kernel->RunAllAgentsForever(interleaveStepSize);
	}

	std::string lastResult = kernel->RunAllAgentsForever(interleaveStepSize);
	if (log)
	{
		*log << std::endl << lastResult << std::endl;
	}

	if (!checkpointAgent)
	{
		throw std::runtime_error("Snapshot run requested for RunAllAgentsForever without a checkpoint agent.");
	}

	const std::string finalSnapshot = snapshotStem.empty() ? "run_all_agents_forever_before_check" : snapshotStem + "_before_check";
	if (!snapshot_and_restore(checkpointAgent, finalSnapshot, log))
	{
		throw std::runtime_error("Snapshot rebuild failed for " + finalSnapshot);
	}

	return lastResult;
}

void SoarHelper::normalize_after_snapshot_testing(sml::Agent* agent, std::ostream* log)
{
	if (!snapshot_every_step || !agent)
	{
		return;
	}

	for (const char* command : {"soar init", "production excise --all"})
	{
		const std::string result = agent->ExecuteCommandLine(command);
		if (log)
		{
			*log << "\n>>> " << command << "\n" << result << std::endl;
		}
	}
}

std::string SoarHelper::run_self_with_snapshots(sml::Agent* agent,
												int count,
												const std::string& snapshotStem,
												std::ostream* log,
												sml::smlRunStepSize stepSize)
{
	std::string lastResult;
	for (int step = 0; step < count; ++step)
	{
		lastResult = run_self_via_command_line(agent, 1, stepSize);
		if (log)
		{
			*log << std::endl << lastResult << std::endl;
		}

		const int stepNumber = step + 1;
		if (should_snapshot_step(stepNumber))
		{
			const std::string perStepSnapshot = snapshotStem + "_step_" + std::to_string(stepNumber);
			if (!snapshot_and_restore(agent, perStepSnapshot, log))
			{
				throw std::runtime_error("Snapshot rebuild failed for " + perStepSnapshot);
			}
		}
	}

	return lastResult;
}

void SoarHelper::agent_command(sml::Agent* agent, const char* pCmd)
{
    agent->ExecuteCommandLine(pCmd, true, false);
}

bool SoarHelper::source(sml::Agent* agent, const std::string& pCategoryName, const std::string& pTestName)
{
    sml::ClientAnalyzedXML response;

    std::string sourceName = pCategoryName + "_" + pTestName + ".soar";
    std::string lPath = GetResource(sourceName);
    if (lPath.empty()) return false;
    agent->ExecuteCommandLineXML(std::string("source \"" + lPath + "\"").c_str(), &response);
    return true;
}

void SoarHelper::check_learning_override(sml::Agent* agent)
{
    #ifdef ALWAYS_LEARN
    if (SoarHelper::save_logs)
    {
            SoarHelper::agent_command(agent,"output log -a Forcing learning on.");
    }
        SoarHelper::agent_command(agent,"chunk always");
    #endif
    #ifdef NEVER_LEARN
        if (SoarHelper::save_logs)
        {
            SoarHelper::agent_command(agent,"output log -a Forcing learning off.");
        }
        SoarHelper::agent_command(agent,"chunk never");
    #endif
}

void SoarHelper::init_check_to_find_refcount_leaks(sml::Agent* agent)
{
    if (!SoarHelper::no_init_soar)
    {
        if (SoarHelper::save_logs)
        {
            SoarHelper::agent_command(agent,"output log -a Testing re-initialization of Soar for memory leaks and crashes.");
        }
        SoarHelper::agent_command(agent,"soar init");
        if (SoarHelper::save_after_action_report)
        {
            SoarHelper::agent_command(agent,"explain after-action-report off");
        }
        SoarHelper::agent_command(agent,"trace 0");
        SoarHelper::agent_command(agent,"run 100");
        SoarHelper::agent_command(agent,"trace 1");
        SoarHelper::agent_command(agent,"soar init");
        std::cout << "✅ ";
        std::cout.flush();
    }
}
void SoarHelper::add_log_dir_if_exists(std::string &lPath)
{
     struct stat sb;

     if (stat("logs", &sb) == 0 && S_ISDIR(sb.st_mode))
     {
         lPath += "logs/";
     }
}


void SoarHelper::start_log(sml::Agent* agent, const char* pTestName)
{
    std::string lCmdName("output log ");
    if (SoarHelper::save_logs)
    {
        SoarHelper::add_log_dir_if_exists(lCmdName);
    }
    lCmdName += pTestName;
    lCmdName += "_log.txt";
    if (SoarHelper::save_logs)
    {
        SoarHelper::agent_command(agent,lCmdName.c_str());
    }
}

void SoarHelper::continue_log(sml::Agent* agent, const char* pTestName)
{
    std::string lCmdName("output log -A ");
    if (SoarHelper::save_logs)
    {
       SoarHelper::add_log_dir_if_exists(lCmdName);
    }
    lCmdName += pTestName;
    lCmdName += "_log.txt";
    if (SoarHelper::save_logs)
    {
        SoarHelper::agent_command(agent,lCmdName.c_str());
    }
}

void SoarHelper::close_log(sml::Agent* agent)
{
    std::string lCmdName("output log -c");
    if (SoarHelper::save_logs)
    {
        SoarHelper::agent_command(agent,lCmdName.c_str());
    }
}
int SoarHelper::getD_CYCLE_COUNT(sml::Agent* agent)
{
	return SoarHelper::parseForCount("decisions", SoarHelper::getStats(agent));
}

int SoarHelper::getDecisionPhasesCount(sml::Agent* agent)
{
	std::stringstream ss(agent->ExecuteCommandLine("stats --decision"));
	int result;
	ss >> result;
	return result;
}

int SoarHelper::getE_CYCLE_COUNT(sml::Agent* agent)
{
	return SoarHelper::parseForCount("elaboration cycles", SoarHelper::getStats(agent));
}

int SoarHelper::getPE_CYCLE_COUNT(sml::Agent* agent)
{
	return SoarHelper::parseForCount("p-elaboration cycles", SoarHelper::getStats(agent));
}

int SoarHelper::getINNER_E_CYCLE_COUNT(sml::Agent* agent)
{
	return SoarHelper::parseForCount("inner elaboration cycles", SoarHelper::getStats(agent));
}

int SoarHelper::getUserProductionCount(sml::Agent* agent)
{
	std::string rules = agent->ExecuteCommandLine("print --user");

	std::stringstream ss(rules);
	std::string line;

	int count = 0;

	while (std::getline(ss, line, '\n'))
	{
		++count;
	}

	return count;
}

int SoarHelper::getChunkProductionCount(sml::Agent* agent)
{
	std::string rules = agent->ExecuteCommandLine("print --chunks");

	std::stringstream ss(rules);
	std::string line;

	int count = 0;

	while (std::getline(ss, line, '\n'))
	{
		++count;
	}

	return count;
}

std::string SoarHelper::getStats(sml::Agent* agent)
{
	return agent->ExecuteCommandLine("stats");
}

int SoarHelper::parseForCount(std::string search, std::string countString)
{
	std::stringstream ss(countString);
	std::string line;

	while (std::getline(ss, line, '\n'))
	{
		if (line.size() > 0 && isdigit(line.front()))
		{
			// Potential match
			// Check for match first
			size_t first_space = line.find(' ');

			if (first_space != std::string::npos)
			{
				++first_space;

				int matchCount = 0;
				for (size_t i = first_space;i < line.size() && line[i] == search[i-first_space];++i,++matchCount);

				if (matchCount == search.size())
				{
					// Match
					// Get the number

					// Get the number first
					std::string s_number = line.substr(0, first_space-1);
					int number;

					std::stringstream ss_number(s_number);
					ss_number >> number;

					return number;
				}
			}
		}
	}

	return -1;
}

std::tuple<SoarHelper::StopPhase, bool> SoarHelper::getStopPhase(sml::Agent* agent)
{
	std::istringstream iss(agent->ExecuteCommandLine("soar stop-phase"));

	std::string s_phase;
	std::string temp;

	iss >> temp >> temp >> s_phase;

	StopPhase phase = StopPhase::INPUT;
	if (s_phase == "input")
		phase = StopPhase::INPUT;
	else if (s_phase == "propose")
		phase = StopPhase::PROPOSE;
	else if (s_phase == "decision")
		phase = StopPhase::DECIDE;
	else if (s_phase == "apply")
		phase = StopPhase::APPLY;
	else if (s_phase == "output")
		phase = StopPhase::OUTPUT;

	return std::make_tuple(phase, true);
}

void SoarHelper::setStopPhase(sml::Agent* agent, StopPhase phase, bool before)
{
	std::string s_phase = "--input";

	if (phase == StopPhase::INPUT)
		s_phase = "input";
	else if (phase == StopPhase::PROPOSE)
		s_phase = "propose";
	else if (phase == StopPhase::DECIDE)
		s_phase = "decide";
	else if (phase == StopPhase::APPLY)
		s_phase = "apply";
	else if (phase == StopPhase::OUTPUT)
		s_phase = "output";

	agent->ExecuteCommandLine(std::string("soar stop-phase " + s_phase).c_str());
}

std::vector<std::string> SoarHelper::getGoalStack(sml::Agent* agent)
{
	std::string s_result = agent->ExecuteCommandLine("p --stack");

	std::stringstream ss(s_result);

	std::vector<std::string> v_result;
	std::string line;

	while (std::getline(ss, line, '\n'))
	{
		size_t start = line.find("==>S: ")+6;
		size_t end = line.find(" ", start);

		if (end == std::string::npos)
			end = line.size();

		std::string goal = line.substr(start, end-start);

		v_result.push_back(goal);
	}

	return v_result;
}

std::ostream& operator<<(std::ostream& os, SoarHelper::StopPhase phase)
{
	switch (phase)
	{
		case SoarHelper::StopPhase::INPUT: os << "INPUT";
		case SoarHelper::StopPhase::PROPOSE: os << "PROPOSE";
		case SoarHelper::StopPhase::DECIDE: os << "DECIDE";
		case SoarHelper::StopPhase::APPLY: os << "APPLY";
		case SoarHelper::StopPhase::OUTPUT: os << "OUTPUT";
	}

	return os;
}

std::string SoarHelper::GetResource(std::string resource)
{
	std::string path =  FindFile(resource, ResourceDirectory);
	const char* workingDirectory = getenv("WORKING_DIRECTORY");
	const char* soarHome = getenv("SOAR_HOME");

	if (path.length() == 0 && workingDirectory)
	{
		path = FindFile(resource, workingDirectory);
	}

	if (path.length() == 0 && soarHome)
	{
		path = FindFile(resource, soarHome);
	}

	if (path.length() == 0)
	{
		path = FindFile(resource, "./");
	}

	return path;
}

std::string SoarHelper::FindFile(std::string filename, std::string path)
{
	DIR* dir = opendir(path.c_str());

	if (!dir)
		return "";

	struct dirent* entry;

	while ((entry = readdir(dir)) != nullptr)
	{
#ifndef _WIN32
		if (entry->d_type == DT_UNKNOWN)
		{
			struct stat file_info;

			lstat((path + std::string(entry->d_name)).c_str(), &file_info);

			if (S_ISDIR(file_info.st_mode))
			{
				entry->d_type = DT_DIR;
			}
			else if (S_ISREG(file_info.st_mode))
			{
				entry->d_type = DT_REG;
			}
		}
#endif

		if (entry->d_type == DT_DIR)
		{
			// Another directory
			std::string directory = entry->d_name;

			if (directory == "." || directory == "..")
				continue;

			std::string result = FindFile(filename, path + directory + "/");

			if (result.size() != 0)
			{
				closedir(dir);

				return result;
			}
		}
		else
		{
			std::string result = entry->d_name;

			if (filename == result)
			{
				closedir(dir);

				return path + result;
			}
		}
	}

	closedir(dir);

	return "";
}
