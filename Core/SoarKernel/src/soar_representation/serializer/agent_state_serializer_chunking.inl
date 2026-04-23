void export_chunking_runtime_state(agent* thisAgent,
                                   soar::kernel::AgentState* state,
                                   SymbolIdMap& symbol_map)
{
    auto* chunking_runtime = state->mutable_chunking_runtime();
    add_symbol_id_list(thisAgent,
                       thisAgent->explanationBasedChunker->chunky_problem_spaces,
                       state,
                       symbol_map,
                       chunking_runtime->mutable_chunky_problem_spaces());
    add_symbol_id_list(thisAgent,
                       thisAgent->explanationBasedChunker->chunk_free_problem_spaces,
                       state,
                       symbol_map,
                       chunking_runtime->mutable_chunk_free_problem_spaces());
    add_non_bottom_up_goal_ids(thisAgent,
                               state,
                               symbol_map,
                               chunking_runtime->mutable_non_bottom_up_goals());
    for (int setting_index = 0; setting_index < num_ebc_settings; ++setting_index)
    {
        chunking_runtime->add_ebc_settings(thisAgent->explanationBasedChunker->ebc_settings[setting_index]);
    }
    chunking_runtime->set_max_chunks(thisAgent->explanationBasedChunker->max_chunks);
    chunking_runtime->set_max_dupes(thisAgent->explanationBasedChunker->max_dupes);
    export_slot_osk_prefs(thisAgent, state, symbol_map, chunking_runtime);
}

int64_t add_optional_symbol_entry(agent* thisAgent,
                                  Symbol* sym,
                                  soar::kernel::AgentState* state,
                                  SymbolIdMap& symbol_map)
{
    if (!sym)
    {
        return 0;
    }

    return static_cast<int64_t>(add_symbol_entry(thisAgent, sym, state, symbol_map));
}

void export_active_goal_runtime_state(agent* thisAgent,
                                      soar::kernel::AgentState* state,
                                      SymbolIdMap& symbol_map)
{
    auto* active_goal_runtime = state->mutable_active_goal_runtime();
    active_goal_runtime->set_active_goal(add_optional_symbol_entry(thisAgent, thisAgent->active_goal, state, symbol_map));
    active_goal_runtime->set_previous_active_goal(add_optional_symbol_entry(thisAgent, thisAgent->previous_active_goal, state, symbol_map));
    active_goal_runtime->set_highest_active_goal(add_optional_symbol_entry(thisAgent, thisAgent->highest_active_goal, state, symbol_map));
    active_goal_runtime->set_active_level(static_cast<int32_t>(thisAgent->active_level));
    active_goal_runtime->set_previous_active_level(static_cast<int32_t>(thisAgent->previous_active_level));
    active_goal_runtime->set_highest_active_level(static_cast<int32_t>(thisAgent->highest_active_level));
    active_goal_runtime->set_change_level(static_cast<int32_t>(thisAgent->change_level));
    active_goal_runtime->set_next_change_level(static_cast<int32_t>(thisAgent->next_change_level));
}

RestoredActiveGoalRuntimeState restore_active_goal_runtime_state(const soar::kernel::ActiveGoalRuntimeState& active_goal_runtime,
                                                                 const SymbolReverseMap& symbol_map)
{
    RestoredActiveGoalRuntimeState restored_state;
    restored_state.active_goal = symbol_by_id(symbol_map, active_goal_runtime.active_goal());
    restored_state.previous_active_goal = symbol_by_id(symbol_map, active_goal_runtime.previous_active_goal());
    restored_state.highest_active_goal = symbol_by_id(symbol_map, active_goal_runtime.highest_active_goal());
    restored_state.active_level = static_cast<goal_stack_level>(active_goal_runtime.active_level());
    restored_state.previous_active_level = static_cast<goal_stack_level>(active_goal_runtime.previous_active_level());
    restored_state.highest_active_level = static_cast<goal_stack_level>(active_goal_runtime.highest_active_level());
    restored_state.change_level = static_cast<goal_stack_level>(active_goal_runtime.change_level());
    restored_state.next_change_level = static_cast<goal_stack_level>(active_goal_runtime.next_change_level());
    restored_state.valid = true;
    return restored_state;
}

RestoredActiveGoalRuntimeState restore_active_goal_runtime_state(const soar::kernel::AgentState& state,
                                                                 const SymbolReverseMap& symbol_map)
{
    RestoredActiveGoalRuntimeState restored_state;
    const auto& trace_settings = state.settings().trace_settings();
    const int extension_index = HIGHEST_SYSPARAM_NUMBER + 1;
    for (int index = extension_index; index + 9 < trace_settings.size(); ++index)
    {
        if ((trace_settings.Get(index) != kSnapshotActiveGoalStateMagic) ||
            (trace_settings.Get(index + 1) != kSnapshotActiveGoalStateVersion))
        {
            continue;
        }

        index += 2;
        restored_state.active_goal = symbol_by_id(symbol_map, static_cast<uint64_t>(trace_settings.Get(index++)));
        restored_state.previous_active_goal = symbol_by_id(symbol_map, static_cast<uint64_t>(trace_settings.Get(index++)));
        restored_state.highest_active_goal = symbol_by_id(symbol_map, static_cast<uint64_t>(trace_settings.Get(index++)));
        restored_state.active_level = static_cast<goal_stack_level>(trace_settings.Get(index++));
        restored_state.previous_active_level = static_cast<goal_stack_level>(trace_settings.Get(index++));
        restored_state.highest_active_level = static_cast<goal_stack_level>(trace_settings.Get(index++));
        restored_state.change_level = static_cast<goal_stack_level>(trace_settings.Get(index++));
        restored_state.next_change_level = static_cast<goal_stack_level>(trace_settings.Get(index++));
        restored_state.valid = true;
        return restored_state;
    }

    return restored_state;
}

void export_pending_ie_assertions(agent* thisAgent,
                                  soar::kernel::AgentState* state,
                                  SymbolIdMap& symbol_map)
{
    for (ms_change* current = thisAgent->ms_i_assertions; current != NIL; current = current->next)
    {
        if (current->p_node && current->p_node->b.p.prod && current->p_node->b.p.prod->name)
        {
            auto* pending_assertion = state->add_pending_ie_assertions();
            pending_assertion->set_production_name_symbol(add_symbol_entry(thisAgent,
                                                                           current->p_node->b.p.prod->name,
                                                                           state,
                                                                           symbol_map));
            pending_assertion->set_match_goal_level(static_cast<int32_t>(current->goal ? current->goal->id->level : current->level));

            std::vector<uint64_t> timetags = capture_match_timetags(thisAgent, current->tok, current->w);
            for (uint64_t timetag : timetags)
            {
                pending_assertion->add_timetags(timetag);
            }
        }
    }
}

void export_pending_pe_assertions(agent* thisAgent,
                                  soar::kernel::AgentState* state,
                                  SymbolIdMap& symbol_map)
{
    for (ms_change* current = thisAgent->ms_o_assertions; current != NIL; current = current->next)
    {
        if (current->p_node && current->p_node->b.p.prod && current->p_node->b.p.prod->name)
        {
            auto* pending_assertion = state->add_pending_pe_assertions();
            pending_assertion->set_production_name_symbol(add_symbol_entry(thisAgent,
                                                                           current->p_node->b.p.prod->name,
                                                                           state,
                                                                           symbol_map));
            pending_assertion->set_match_goal_level(static_cast<int32_t>(current->goal ? current->goal->id->level : current->level));

            std::vector<uint64_t> timetags = capture_match_timetags(thisAgent, current->tok, current->w);
            for (uint64_t timetag : timetags)
            {
                pending_assertion->add_timetags(timetag);
            }
        }
    }
}

void export_goal_identity_sets(agent* thisAgent,
                               soar::kernel::AgentState* state,
                               SymbolIdMap& symbol_map)
{
    (void) thisAgent;
    (void) state;
    (void) symbol_map;
}

void export_slot_osk_prefs(agent* thisAgent,
                           soar::kernel::AgentState* state,
                           SymbolIdMap& symbol_map,
                           soar::kernel::ChunkingRuntimeState* chunking_runtime)
{
    std::unordered_set<slot*> exported_slots;

    for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
    {
        slot* s = (w->id && w->attr) ? find_slot(w->id, w->attr) : NIL;
        if (!s || !s->OSK_prefs || !exported_slots.insert(s).second)
        {
            continue;
        }

        auto* slot_state = chunking_runtime->add_slot_osk_prefs();
        slot_state->set_id_symbol(add_symbol_entry(thisAgent, s->id, state, symbol_map));
        slot_state->set_attr_symbol(add_symbol_entry(thisAgent, s->attr, state, symbol_map));

        for (cons* osk_pref = s->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
        {
            preference* pref = static_cast<preference*>(osk_pref->first);
            if (!pref)
            {
                continue;
            }

            add_preference_entry(thisAgent,
                                 pref,
                                 state,
                                 symbol_map,
                                 slot_state->add_preferences());
        }

        std::fprintf(stderr,
                 "export_slot_osk_prefs: slot %s ^%s prefs=%llu\n",
                     s->id ? s->id->to_string(true, false, NIL, 0) : "nil",
                     s->attr ? s->attr->to_string(true, false, NIL, 0) : "nil",
                 static_cast<unsigned long long>(slot_state->preferences_size()));
    }
}

void export_goal_saved_firing_types(agent* thisAgent,
                                    soar::kernel::AgentState* state,
                                    SymbolIdMap& symbol_map)
{
    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (!goal->is_sti() || (goal->id->saved_firing_type == NO_SAVED_PRODS))
        {
            continue;
        }

        auto* firing_state = state->add_goal_saved_firing_types();
        firing_state->set_goal_symbol(add_symbol_entry(thisAgent, goal, state, symbol_map));
        firing_state->set_saved_firing_type(goal->id->saved_firing_type);
    }
}

void restore_goal_saved_firing_types(agent* thisAgent,
                                     const google::protobuf::RepeatedPtrField<soar::kernel::GoalSavedFiringTypeState>& goal_saved_firing_types,
                                     const SymbolReverseMap& symbol_map)
{
    for (const auto& firing_state : goal_saved_firing_types)
    {
        Symbol* goal = symbol_by_id(symbol_map, firing_state.goal_symbol());
        if (!goal || !goal->is_sti())
        {
            continue;
        }

        goal->id->saved_firing_type = firing_state.saved_firing_type();
    }
}

bool restore_chunking_symbol_list(agent* thisAgent,
                                  cons*& destination,
                                  const google::protobuf::RepeatedField<int64_t>& trace_settings,
                                  int& index,
                                  const SymbolReverseMap& symbol_map)
{
    if (index >= trace_settings.size())
    {
        return false;
    }

    const int64_t count = trace_settings.Get(index++);
    if (count < 0)
    {
        return false;
    }

    for (int64_t offset = 0; offset < count; ++offset)
    {
        if (index >= trace_settings.size())
        {
            return false;
        }

        Symbol* sym = symbol_by_id(symbol_map, static_cast<uint64_t>(trace_settings.Get(index++)));
        if (sym && sym->is_sti() && !symbol_is_in_cons_list(destination, sym))
        {
            push(thisAgent, sym, destination);
        }
    }

    return true;
}

void restore_non_bottom_up_goal_state(const google::protobuf::RepeatedField<int64_t>& trace_settings,
                                      int& index,
                                      const SymbolReverseMap& symbol_map)
{
    if (index >= trace_settings.size())
    {
        return;
    }

    const int64_t count = trace_settings.Get(index++);
    if (count < 0)
    {
        return;
    }

    for (int64_t offset = 0; offset < count && index < trace_settings.size(); ++offset)
    {
        Symbol* goal = symbol_by_id(symbol_map, static_cast<uint64_t>(trace_settings.Get(index++)));
        if (goal && goal->is_sti())
        {
            goal->id->allow_bottom_up_chunks = false;
        }
    }
}

void restore_chunking_runtime_state(agent* thisAgent,
                                    const soar::kernel::ChunkingRuntimeState& chunking_runtime,
                                    const SymbolReverseMap& symbol_map)
{
    for (int index = 0; index < chunking_runtime.chunky_problem_spaces_size(); ++index)
    {
        Symbol* sym = symbol_by_id(symbol_map, chunking_runtime.chunky_problem_spaces(index));
        if (sym && sym->is_sti() && !symbol_is_in_cons_list(thisAgent->explanationBasedChunker->chunky_problem_spaces, sym))
        {
            push(thisAgent, sym, thisAgent->explanationBasedChunker->chunky_problem_spaces);
        }
    }

    for (int index = 0; index < chunking_runtime.chunk_free_problem_spaces_size(); ++index)
    {
        Symbol* sym = symbol_by_id(symbol_map, chunking_runtime.chunk_free_problem_spaces(index));
        if (sym && sym->is_sti() && !symbol_is_in_cons_list(thisAgent->explanationBasedChunker->chunk_free_problem_spaces, sym))
        {
            push(thisAgent, sym, thisAgent->explanationBasedChunker->chunk_free_problem_spaces);
        }
    }

    for (int index = 0; index < chunking_runtime.non_bottom_up_goals_size(); ++index)
    {
        Symbol* goal = symbol_by_id(symbol_map, chunking_runtime.non_bottom_up_goals(index));
        if (goal && goal->is_sti())
        {
            goal->id->allow_bottom_up_chunks = false;
        }
    }

    const int restored_setting_count = std::min(chunking_runtime.ebc_settings_size(), static_cast<int>(num_ebc_settings));
    for (int setting_index = 0; setting_index < restored_setting_count; ++setting_index)
    {
        thisAgent->explanationBasedChunker->ebc_settings[setting_index] = chunking_runtime.ebc_settings(setting_index);
    }

    if (chunking_runtime.max_chunks() > 0)
    {
        thisAgent->explanationBasedChunker->max_chunks = chunking_runtime.max_chunks();
        thisAgent->explanationBasedChunker->ebc_params->max_chunks->set_value(static_cast<int64_t>(chunking_runtime.max_chunks()));
    }
    if (chunking_runtime.max_dupes() > 0)
    {
        thisAgent->explanationBasedChunker->max_dupes = chunking_runtime.max_dupes();
        thisAgent->explanationBasedChunker->ebc_params->max_dupes->set_value(static_cast<int64_t>(chunking_runtime.max_dupes()));
    }

    thisAgent->explanationBasedChunker->ebc_params->update_params(thisAgent->explanationBasedChunker->ebc_settings);
}

void restore_inst_identity_map(agent* thisAgent,
                               const soar::kernel::ChunkingRuntimeState& chunking_runtime)
{
    for (const auto& entry : chunking_runtime.inst_identity_map())
    {
        if (!entry.inst_identity() || !entry.identity_id())
        {
            continue;
        }

        if (Identity* identity = find_identity_by_id(thisAgent, entry.identity_id()))
        {
            thisAgent->explanationBasedChunker->force_id_to_identity_mapping(entry.inst_identity(), identity);
        }
    }
}

void restore_chunking_runtime_state(agent* thisAgent,
                                    const soar::kernel::AgentState& state,
                                    const SymbolReverseMap& symbol_map)
{
    const auto& trace_settings = state.settings().trace_settings();
    const int extension_index = HIGHEST_SYSPARAM_NUMBER + 1;
    if (trace_settings.size() <= extension_index)
    {
        return;
    }

    int index = extension_index;
    if ((trace_settings.Get(index++) != kSnapshotChunkingStateMagic) ||
        (index >= trace_settings.size()) ||
        (trace_settings.Get(index++) != kSnapshotChunkingStateVersion))
    {
        return;
    }

    restore_chunking_symbol_list(thisAgent,
                                 thisAgent->explanationBasedChunker->chunky_problem_spaces,
                                 trace_settings,
                                 index,
                                 symbol_map);
    restore_chunking_symbol_list(thisAgent,
                                 thisAgent->explanationBasedChunker->chunk_free_problem_spaces,
                                 trace_settings,
                                 index,
                                 symbol_map);
    restore_non_bottom_up_goal_state(trace_settings, index, symbol_map);
}

void restore_slot_osk_prefs(agent* thisAgent,
                            const soar::kernel::ChunkingRuntimeState& chunking_runtime,
                            const SymbolReverseMap& symbol_map,
                            RestoredPreferenceMap& preference_map,
                            RestoredPreferenceCloneMap& preference_clone_map)
{
    const bool debug_osk_ref_track = (std::getenv("SOAR_DEBUG_OSK_REF_TRACK") != nullptr);
    auto is_tracked_identifier = [](Symbol* sym)
    {
        if (!sym || !sym->is_sti())
        {
            return false;
        }
        const char letter = sym->id->name_letter;
        const uint64_t number = sym->id->name_number;
        return ((letter == 'I') && ((number == 2) || (number == 3))) ||
               ((letter == 'O') && (number == 1)) ||
               ((letter == 'S') && ((number == 1) || (number == 2)));
    };

    for (const auto& slot_state : chunking_runtime.slot_osk_prefs())
    {
        Symbol* id = symbol_by_id(symbol_map, slot_state.id_symbol());
        Symbol* attr = symbol_by_id(symbol_map, slot_state.attr_symbol());
        if (!id || !attr)
        {
            continue;
        }

        slot* s = find_slot(id, attr);
        if (!s)
        {
            continue;
        }

        for (const auto& pref_entry : slot_state.preferences())
        {
            Symbol* pref_id = symbol_by_id(symbol_map, pref_entry.id());
            Symbol* pref_attr = symbol_by_id(symbol_map, pref_entry.attr());
            Symbol* pref_value = symbol_by_id(symbol_map, pref_entry.value());
            if (!pref_id || !pref_attr || !pref_value)
            {
                continue;
            }

            const bool tracked_value = is_tracked_identifier(pref_value);
            const bool trace_this = debug_osk_ref_track && tracked_value;
            const uint64_t value_ref_before_pref = tracked_value ? pref_value->reference_count : 0;

            preference* pref = get_or_create_restored_preference(thisAgent,
                                                                 pref_entry,
                                                                 pref_id,
                                                                 pref_attr,
                                                                 pref_value,
                                                                 symbol_map,
                                                                 preference_map,
                                                                 preference_clone_map);
            if (pref)
            {
                if (trace_this)
                {
                    std::fprintf(stderr,
                                 "restore_osk_ref: stage=post-get-or-create slot=%c%llu^%c value=%c%llu ref=%llu(%+lld) pref_type=%d\n",
                                 (s->id && s->id->is_sti()) ? s->id->id->name_letter : '?',
                                 static_cast<unsigned long long>((s->id && s->id->is_sti()) ? s->id->id->name_number : 0),
                                 (s->attr && s->attr->is_sti()) ? s->attr->id->name_letter : '?',
                                 pref_value->id->name_letter,
                                 static_cast<unsigned long long>(pref_value->id->name_number),
                                 static_cast<unsigned long long>(pref_value->reference_count),
                                 static_cast<long long>(pref_value->reference_count) - static_cast<long long>(value_ref_before_pref),
                                 static_cast<int>(pref->type));
                }

                const uint64_t value_ref_before_add_osk = tracked_value ? pref_value->reference_count : 0;
                thisAgent->explanationBasedChunker->add_to_OSK(s, pref, false);
                if (trace_this)
                {
                    std::fprintf(stderr,
                                 "restore_osk_ref: stage=post-add-to-osk slot=%c%llu^%c value=%c%llu ref=%llu(%+lld)\n",
                                 (s->id && s->id->is_sti()) ? s->id->id->name_letter : '?',
                                 static_cast<unsigned long long>((s->id && s->id->is_sti()) ? s->id->id->name_number : 0),
                                 (s->attr && s->attr->is_sti()) ? s->attr->id->name_letter : '?',
                                 pref_value->id->name_letter,
                                 static_cast<unsigned long long>(pref_value->id->name_number),
                                 static_cast<unsigned long long>(pref_value->reference_count),
                                 static_cast<long long>(pref_value->reference_count) - static_cast<long long>(value_ref_before_add_osk));
                }
            }
        }

        std::fprintf(stderr,
                 "restore_slot_osk_prefs: slot %s ^%s prefs=%llu\n",
                     s->id ? s->id->to_string(true, false, NIL, 0) : "nil",
                     s->attr ? s->attr->to_string(true, false, NIL, 0) : "nil",
                 static_cast<unsigned long long>(slot_state.preferences_size()));

        if (s->id && s->attr && s->id->is_sti() && s->attr->is_constant() &&
            (s->id->to_string(true, false, NIL, 0) == std::string("S2")) &&
            (s->attr->to_string(true, false, NIL, 0) == std::string("operator")))
        {
            for (cons* osk_pref = s->OSK_prefs; osk_pref != NIL; osk_pref = osk_pref->rest)
            {
                preference* pref = static_cast<preference*>(osk_pref->first);
                std::fprintf(stderr,
                             "restore_slot_osk_pref_inst: type=%d inst=%s\n",
                             pref ? static_cast<int>(pref->type) : -1,
                             (pref && pref->inst && pref->inst->prod_name && pref->inst->prod_name->is_string()) ?
                                 pref->inst->prod_name->sc->name : "<arch>");
            }
        }
    }
}

void clear_restored_slot_osk_prefs(agent* thisAgent,
                                   const soar::kernel::ChunkingRuntimeState& chunking_runtime,
                                   const SymbolReverseMap& symbol_map)
{
    for (const auto& slot_state : chunking_runtime.slot_osk_prefs())
    {
        Symbol* id = symbol_by_id(symbol_map, slot_state.id_symbol());
        Symbol* attr = symbol_by_id(symbol_map, slot_state.attr_symbol());
        if (!id || !attr)
        {
            continue;
        }

        slot* s = find_slot(id, attr);
        if (!s || !s->OSK_prefs)
        {
            continue;
        }

        clear_preference_list(thisAgent, s->OSK_prefs);
        s->OSK_prefs = NIL;
    }
}
