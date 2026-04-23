/////////////////////////////////////////////////////////////////
// agent state save/load command file.
//
// Author: Generated for protobuf agent state serialization
// Date  : 2024
//
/////////////////////////////////////////////////////////////////

#include "portability.h"

#include "cli_CommandLineInterface.h"
#include "agent_state_serializer.h"
#include "run_soar.h"
#include "xml.h"

#include "sml_AgentSML.h"
#include "sml_KernelSML.h"
#include "sml_Names.h"

#undef X
#include <agent_state.pb.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

using namespace cli;
using namespace sml;

bool CommandLineInterface::DoSaveAgentState(const std::string& filename)
{
    agent* thisAgent = m_pAgentSML->GetSoarAgent();
    return soar::kernel::serialize_agent_state(thisAgent, filename.c_str());
}

bool CommandLineInterface::DoLoadAgentState(const std::string& filename)
{
    AgentSML* agentSML = m_pAgentSML;
    std::string oldResult = m_Result.str();
    std::string loadResult;

    soar::kernel::AgentState state;
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open())
    {
        SetError("Unable to open " + filename + " for reading.");
        return false;
    }

    if (!state.ParseFromIstream(&in))
    {
        SetError("Failed to parse agent state from " + filename + ".");
        return false;
    }

    agent* thisAgent = agentSML->GetSoarAgent();

    auto restoreAgentShellState = [&](AgentSML* targetAgentSML, const soar::kernel::AgentState& targetState, const char* sourceName)
    {
        agent* targetAgent = targetAgentSML->GetSoarAgent();

        SetTrapPrintCallbacks(false);

        if (targetAgentSML->GetKernelSML())
        {
            targetAgentSML->GetKernelSML()->FireAgentEvent(targetAgentSML, smlEVENT_BEFORE_AGENT_REINITIALIZED);
        }

        bool restored = soar::kernel::restore_agent_state_message(targetAgent, targetState, sourceName);

        if (restored)
        {
            targetAgentSML->GetOutputListener()->SendOutputInitEvent();
            if (targetAgentSML->GetKernelSML())
            {
                targetAgentSML->GetKernelSML()->FireAgentEvent(targetAgentSML, smlEVENT_AFTER_AGENT_REINITIALIZED);
            }
        }

        xml_invoke_callback(targetAgent);
        targetAgentSML->FlushPrintOutput();
        SetTrapPrintCallbacks(true);

        return restored;
    };

    bool ok = restoreAgentShellState(agentSML, state, filename.c_str());
    loadResult = m_Result.str();

    m_Result.str(oldResult);
    m_Result.clear();

    if (!ok)
    {
        if (!loadResult.empty())
        {
            m_Result << loadResult;
        }
        return false;
    }

    if (m_RawOutput)
    {
        m_Result << "\nAgent state loaded.\n";
    }

    return true;
}

bool CommandLineInterface::DoSaveKernelState(const std::string& filename)
{
    KernelSML* kernel = m_pAgentSML ? m_pAgentSML->GetKernelSML() : nullptr;
    if (!kernel)
    {
        SetError("Kernel state save requires an active kernel.");
        return false;
    }

    soar::kernel::KernelState kernelState;
    auto* settings = kernelState.mutable_settings();
    settings->set_suppress_system_start(kernel->IsSystemStartSuppressed());
    settings->set_suppress_system_stop(kernel->IsSystemStopSuppressedRaw());
    settings->set_require_system_stop(kernel->IsSystemStopRequired());
    settings->set_echo_commands(kernel->GetEchoCommands());
    settings->set_interrupt_check_rate(kernel->GetInterruptCheckRate());
    settings->set_stop_point(static_cast<int32_t>(kernel->GetStopPoint()));
    settings->set_stop_before_phase(static_cast<int32_t>(kernel->GetStopBefore()));

    std::vector<AgentSML*> agents = kernel->GetAllAgentSML();
    std::sort(agents.begin(), agents.end(), [](AgentSML* left, AgentSML* right)
    {
        return std::string(left->GetName()) < std::string(right->GetName());
    });

    for (AgentSML* agentSML : agents)
    {
        if (!soar::kernel::build_agent_state_message(agentSML->GetSoarAgent(), kernelState.add_agents()))
        {
            return false;
        }
    }

    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open())
    {
        SetError("Unable to open " + filename + " for writing.");
        return false;
    }

    if (!kernelState.SerializeToOstream(&out))
    {
        SetError("Failed to serialize kernel state to " + filename + ".");
        return false;
    }

    return true;
}

bool CommandLineInterface::DoLoadKernelState(const std::string& filename)
{
    KernelSML* kernel = m_pAgentSML ? m_pAgentSML->GetKernelSML() : nullptr;
    if (!kernel)
    {
        SetError("Kernel state load requires an active kernel.");
        return false;
    }

    soar::kernel::KernelState kernelState;
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open())
    {
        SetError("Unable to open " + filename + " for reading.");
        return false;
    }

    if (!kernelState.ParseFromIstream(&in))
    {
        SetError("Failed to parse kernel state from " + filename + ".");
        return false;
    }

    auto restoreAgentShellState = [&](AgentSML* targetAgentSML, const soar::kernel::AgentState& targetState, const char* sourceName)
    {
        agent* targetAgent = targetAgentSML->GetSoarAgent();

        SetTrapPrintCallbacks(false);

        if (targetAgentSML->GetKernelSML())
        {
            targetAgentSML->GetKernelSML()->FireAgentEvent(targetAgentSML, smlEVENT_BEFORE_AGENT_REINITIALIZED);
        }

        bool restored = soar::kernel::restore_agent_state_message(targetAgent, targetState, sourceName);

        if (restored)
        {
            targetAgentSML->GetOutputListener()->SendOutputInitEvent();
            if (targetAgentSML->GetKernelSML())
            {
                targetAgentSML->GetKernelSML()->FireAgentEvent(targetAgentSML, smlEVENT_AFTER_AGENT_REINITIALIZED);
            }
        }

        xml_invoke_callback(targetAgent);
        targetAgentSML->FlushPrintOutput();
        SetTrapPrintCallbacks(true);

        return restored;
    };

    std::unordered_map<std::string, AgentSML*> existingAgents;
    for (AgentSML* agentSML : kernel->GetAllAgentSML())
    {
        existingAgents.emplace(agentSML->GetName(), agentSML);
    }

    std::unordered_set<std::string> desiredAgents;
    desiredAgents.reserve(kernelState.agents_size());
    for (const auto& agentState : kernelState.agents())
    {
        desiredAgents.insert(agentState.agent_name());
    }

    if (m_pAgentSML && desiredAgents.find(m_pAgentSML->GetName()) == desiredAgents.end())
    {
        SetError("The active CLI agent is not present in the saved kernel state.");
        return false;
    }

    std::vector<AgentSML*> agentsToDestroy;
    for (const auto& entry : existingAgents)
    {
        if (desiredAgents.find(entry.first) == desiredAgents.end())
        {
            agentsToDestroy.push_back(entry.second);
        }
    }

    for (AgentSML* agentSML : agentsToDestroy)
    {
        if (!kernel->DestroyAgentSML(agentSML))
        {
            SetError("Failed to destroy agent during kernel state restore.");
            return false;
        }
    }

    std::unordered_map<std::string, AgentSML*> restoredAgents;
    for (const auto& agentState : kernelState.agents())
    {
        AgentSML* agentSML = nullptr;
        auto existingIter = existingAgents.find(agentState.agent_name());
        if (existingIter != existingAgents.end())
        {
            agentSML = existingIter->second;
        }
        else
        {
            agentSML = kernel->CreateAgentSML(agentState.agent_name().c_str(), false, false);
        }

        if (!agentSML)
        {
            SetError("Failed to create agent shell for kernel state restore.");
            return false;
        }

        if (!restoreAgentShellState(agentSML, agentState, filename.c_str()))
        {
            SetError("Failed to restore agent state for " + agentState.agent_name() + ".");
            return false;
        }

        restoredAgents[agentState.agent_name()] = agentSML;
    }

    if (kernelState.has_settings())
    {
        const auto& settings = kernelState.settings();
        kernel->SetSuppressSystemStart(settings.suppress_system_start());
        kernel->SetSuppressSystemStop(settings.suppress_system_stop());
        kernel->RequireSystemStop(settings.require_system_stop());
        kernel->SetEchoCommands(settings.echo_commands());
        kernel->SetInterruptCheckRate(settings.interrupt_check_rate());
        kernel->SetStopBefore(static_cast<smlPhase>(settings.stop_before_phase()));
    }

    if (m_pAgentSML)
    {
        auto restoredIter = restoredAgents.find(m_pAgentSML->GetName());
        if (restoredIter != restoredAgents.end())
        {
            m_pAgentSML = restoredIter->second;
        }
    }

    if (m_RawOutput)
    {
        m_Result << "\nKernel state loaded.\n";
    }

    return true;
}