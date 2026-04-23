#include "agent_state_serializer.h"
#undef X
#include <agent_state.pb.h>

#include "agent.h"
#include "../interface/callback.h"
#include "../interface/io_link.h"
#include "../decision_process/consistency.h"
#include "../decision_process/run_soar.h"
#include "../shared/soar_rand.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace soar
{
namespace kernel
{
namespace
{

using SymbolIdMap = std::unordered_map<Symbol*, uint64_t>;
using SymbolReverseMap = std::unordered_map<uint64_t, Symbol*>;
using OwnedSymbolRefs = std::vector<Symbol*>;
using ProductionMap = std::unordered_map<std::string, production*>;
using SavedInstantiationSet = std::unordered_set<instantiation*>;
using SavedProductionFiringCountMap = std::unordered_map<std::string, uint64_t>;
using RestoredPreferenceKey = std::string;
using RestoredPreferenceCloneKey = std::string;
using RestoredPreferenceMap = std::unordered_map<RestoredPreferenceKey, preference*>;
using RestoredPreferenceCloneMap = std::unordered_map<RestoredPreferenceCloneKey, preference*>;
using RestoredAssertionKey = std::string;
using RestoredAssertionSet = std::unordered_set<RestoredAssertionKey>;

enum class RestoredWmeOwner
{
    Slot,
    AcceptablePreference,
    Impasse,
    Input
};

// Foundational helpers used broadly across export and restore paths.
#include "serializer/agent_state_serializer_core_helpers.inl"

constexpr int64_t kSnapshotChunkingStateMagic = -913570431LL;
constexpr int64_t kSnapshotChunkingStateVersion = 1;
constexpr int64_t kSnapshotPendingIeAssertionsMagic = -913570432LL;
constexpr int64_t kSnapshotPendingIeAssertionsVersion = 1;
constexpr int64_t kSnapshotActiveGoalStateMagic = -913570433LL;
constexpr int64_t kSnapshotActiveGoalStateVersion = 1;

struct RestoredActiveGoalRuntimeState
{
    Symbol* active_goal = NIL;
    Symbol* previous_active_goal = NIL;
    Symbol* highest_active_goal = NIL;
    goal_stack_level active_level = 0;
    goal_stack_level previous_active_level = 0;
    goal_stack_level highest_active_level = 0;
    goal_stack_level change_level = 0;
    goal_stack_level next_change_level = 0;
    bool valid = false;
};

struct RestoredExplanationIdentityState
{
    uint64_t identity_id = 0;
    uint64_t joined_identity_id = 0;
    uint64_t chunk_inst_identity = 0;
    bool literalized = false;
};

uint64_t add_symbol_entry(agent* thisAgent,
                          Symbol* sym,
                          soar::kernel::AgentState* state,
                          SymbolIdMap& symbol_map);
void add_preference_entry(agent* thisAgent,
                          preference* pref,
                          soar::kernel::AgentState* state,
                          SymbolIdMap& symbol_map,
                          soar::kernel::PreferenceEntry* pref_entry);
std::vector<uint64_t> capture_match_timetags(agent* thisAgent, token* tok, wme* w);
bool match_timetags_equal(agent* thisAgent,
                          token* tok,
                          wme* w,
                          const google::protobuf::RepeatedField<uint64_t>& saved_timetags);
bool capture_instantiated_condition_timetags(instantiation* inst, std::vector<uint64_t>& timetags);
bool instantiation_has_only_detached_condition_wmes(agent* thisAgent, instantiation* inst);
void export_slot_osk_prefs(agent* thisAgent,
                           soar::kernel::AgentState* state,
                           SymbolIdMap& symbol_map,
                           soar::kernel::ChunkingRuntimeState* chunking_runtime);
Symbol* symbol_by_id(const SymbolReverseMap& symbol_map, uint64_t id);
Identity* find_goal_identity_by_id(agent* thisAgent, Symbol* goal, uint64_t identity_id);
Identity* find_identity_by_id(agent* thisAgent, uint64_t identity_id);
wme* find_restored_wme_by_timetag(agent* thisAgent, uint64_t timetag);
wme* create_saved_condition_wme(agent* thisAgent,
                                const soar::kernel::WmeEntry& wme_entry,
                                const SymbolReverseMap& symbol_map,
                                RestoredPreferenceMap& preference_map,
                                RestoredPreferenceCloneMap& preference_clone_map,
                                bool restore_preference = true);
preference* get_or_create_restored_preference(agent* thisAgent,
                                              const soar::kernel::PreferenceEntry& pref_entry,
                                              Symbol* id,
                                              Symbol* attr,
                                              Symbol* value,
                                              const SymbolReverseMap& symbol_map,
                                              RestoredPreferenceMap& preference_map,
                                              RestoredPreferenceCloneMap& preference_clone_map);
preference* get_or_create_restored_preference(agent* thisAgent,
                                              const soar::kernel::WmeEntry& wme_entry,
                                              Symbol* id,
                                              Symbol* attr,
                                              Symbol* value,
                                              const SymbolReverseMap& symbol_map,
                                              RestoredPreferenceMap& preference_map,
                                              RestoredPreferenceCloneMap& preference_clone_map);
Symbol* goal_for_level(agent* thisAgent, goal_stack_level level);

// Saved-condition helpers are shared by restore-side match reconstruction.
#include "serializer/agent_state_serializer_saved_conditions.inl"

template <typename RepeatedField>
void add_symbol_id_list(agent* thisAgent,
                        cons* head,
                        soar::kernel::AgentState* state,
                        SymbolIdMap& symbol_map,
                        RepeatedField* destination)
{
    for (cons* current = head; current != NIL; current = current->rest)
    {
        Symbol* sym = static_cast<Symbol*>(current->first);
        if (sym && sym->is_sti())
        {
            destination->Add(add_symbol_entry(thisAgent, sym, state, symbol_map));
        }
    }
}

template <typename RepeatedField>
void add_non_bottom_up_goal_ids(agent* thisAgent,
                                soar::kernel::AgentState* state,
                                SymbolIdMap& symbol_map,
                                RepeatedField* destination)
{
    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (!goal->id->allow_bottom_up_chunks)
        {
            destination->Add(add_symbol_entry(thisAgent, goal, state, symbol_map));
        }
    }
}

// Chunking helpers span both export and restore, so keep them near shared glue.
#include "serializer/agent_state_serializer_chunking.inl"

// Restore-side helpers are ordered to follow the rough restore pipeline.
#include "serializer/agent_state_serializer_pending_restore.inl"

#include "serializer/agent_state_serializer_identity_wmes.inl"

#include "serializer/agent_state_serializer_runtime.inl"

// Export helpers provide a few shared implementation details used by symbol patching.
#include "serializer/agent_state_serializer_export.inl"

#include "serializer/agent_state_serializer_symbols.inl"

#include "serializer/agent_state_serializer_preferences.inl"

#include "serializer/agent_state_serializer_restore_build.inl"

#include "serializer/agent_state_serializer_restore_wmes.inl"

#include "serializer/agent_state_serializer_instantiations.inl"

#include "serializer/agent_state_serializer_matches.inl"

#include "serializer/agent_state_serializer_rete.inl"

}

// Public serializer entry points remain outside the anonymous namespace.
#include "serializer/agent_state_serializer_api.inl"
