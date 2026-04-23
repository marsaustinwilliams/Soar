#ifndef AGENT_STATE_SERIALIZER_H
#define AGENT_STATE_SERIALIZER_H

#include "agent.h"

namespace soar
{
namespace kernel
{

class AgentState;

bool serialize_agent_state(agent* thisAgent, const char* filename);
bool deserialize_agent_state(agent* thisAgent, const char* filename);
bool build_agent_state_message(agent* thisAgent, AgentState* state);
bool restore_agent_state_message(agent* thisAgent, const AgentState& state, const char* source_name = nullptr);

Symbol* saveAgentState_rhs(agent* thisAgent, cons* args, void* user_data);
Symbol* loadAgentState_rhs(agent* thisAgent, cons* args, void* user_data);

}
}

#endif // AGENT_STATE_SERIALIZER_H
